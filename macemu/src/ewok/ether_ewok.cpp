/*
 *  ether_ewok.cpp - Ethernet backend for EwokOS (SLIRP-style NAT)
 *
 *  Translates Mac guest raw Ethernet frames into EwokOS socket API calls.
 *  The emulator acts as a virtual NAT router:
 *    - Guest IP 10.0.0.2  (virtual, static)
 *    - Gateway IP 10.0.0.1 (virtual, MAC = ether_addr)
 *    - ARP: answered locally for gateway / guest
 *    - UDP: relayed via EwokOS SOCK_DGRAM sockets
 *    - ICMP: relayed via EwokOS SOCK_RAW sockets
 *    - TCP: deferred to Phase 2 (SYN gets RST)
 *
 *  Incoming data from EwokOS sockets is wrapped in Ethernet frames and
 *  queued for delivery via EtherInterrupt() on the 60Hz VBL tick.
 */

#include "sysdeps.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
}

#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "ether.h"
#include "ether_defs.h"

#define DEBUG 1
#include "debug.h"

/* libsocket/libgloss report standard errno values (EAGAIN=11, ETIMEDOUT=116);
 * ewoksys <sys/errno.h> assigns different numbers via its enum, so socket
 * results must be compared against the socket layer's actual values. */
#define EWOK_SOCK_EAGAIN	11
#define EWOK_SOCK_ETIMEDOUT	116

/* ------------------------------------------------------------------ */
/*  Virtual network topology                                          */
/* ------------------------------------------------------------------ */

/* Guest MAC = ether_addr[] (set by ether_init(), used by upper layer) */
static const uint32_t GUEST_IP   = 0x0A000002;  /* 10.0.0.2          */
static const uint32_t GW_IP      = 0x0A000001;  /* 10.0.0.1 (v-gw)   */

/* ------------------------------------------------------------------ */
/*  Ethernet / IP constants                                           */
/* ------------------------------------------------------------------ */

#define ETH_ALEN        6
#define ETH_HLEN        14
#define ETH_P_IP        0x0800
#define ETH_P_ARP       0x0806

/* Virtual gateway MAC (distinct from guest MAC for correct ARP/NAT) */
static const uint8_t gateway_mac[ETH_ALEN] = {
	0x52, 0x54, 0x0A, 0x00, 0x00, 0x01
};

#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQ   8

/* TCP flags / proxy tuning */
#define TCP_MSS          1460
#define TCP_RETRANS_USEC 250000

#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PSH  0x08
#define TH_ACK  0x10

/* Maximum queued inbound frames (prevents unbounded memory growth) */
#define RX_QUEUE_MAX    64

/* A UDP NAT mapping with no inbound traffic expires after this idle
 * period: the receiver thread exits and the session is reaped, so
 * one-shot traffic (DNS, NTP) does not accumulate threads for the
 * whole emulator lifetime.                                        */
#define UDP_IDLE_USEC   (60 * 1000000ull)

/* Grace period for a closing TCP session to drain buffered data to
 * the guest before the real socket is torn down anyway.            */
#define TCP_CLOSE_GRACE_USEC 1000000

/* ------------------------------------------------------------------ */
/*  Byte-order helpers (EwokOS netinet/in.h defines ntohl etc.)      */
/* ------------------------------------------------------------------ */

static inline uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  | p[3];
}
static inline void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)(v);
}
static inline void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)(v);
}

/* Internet checksum over big-endian wire bytes.
 * Must read via rd16(): a native uint16_t load on a little-endian host
 * byte-swaps every word, and combined with the big-endian wr16() store of
 * the result the stored checksum ends up byte-swapped -- the guest IP
 * stack then silently drops every packet we deliver.                   */
static uint16_t ip_checksum(const void *hdr, size_t len)
{
	const uint8_t *p = (const uint8_t *)hdr;
	uint32_t sum = 0;
	while (len > 1) {
		sum += rd16(p);
		p += 2;
		len -= 2;
	}
	if (len == 1)
		sum += (uint16_t)(*p << 8);
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (uint16_t)~sum;
}

/* UDP checksum with pseudo-header (ip_pkt points at the IP header) */
static uint16_t udp_checksum(const uint8_t *ip_pkt, int ip_total)
{
	const uint8_t *udp = ip_pkt + 20;
	int udp_len = ip_total - 20;
	uint32_t sum = 0;
	sum += rd16(ip_pkt + 12) + rd16(ip_pkt + 14);  /* src IP   */
	sum += rd16(ip_pkt + 16) + rd16(ip_pkt + 18);  /* dst IP   */
	sum += IP_PROTO_UDP;
	sum += (uint16_t)udp_len;
	int len = udp_len;
	while (len > 1) {
		sum += rd16(udp);
		udp += 2;
		len -= 2;
	}
	if (len == 1)
		sum += (uint16_t)(*udp << 8);
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	uint16_t ck = (uint16_t)~sum;
	return ck ? ck : 0xffff;  /* 0 means "no checksum" in UDP */
}

static uint64 now_usec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64)tv.tv_sec * 1000000ull + tv.tv_usec;
}

/* ------------------------------------------------------------------ */
/*  State                                                             */
/* ------------------------------------------------------------------ */

/* Inbound frame queue: background threads -> EtherInterrupt -> guest */
struct rx_frame {
	uint8_t data[1514];
	int     len;
	rx_frame *next;
};

static pthread_mutex_t rx_mutex;
static rx_frame *rx_head;
static rx_frame *rx_tail;
static int        rx_count;

/* Protocol handler table (EtherType -> MacOS handler address) */
#define MAX_PROTO_HANDLERS 16
struct proto_entry {
	uint16 type;
	uint32 handler;
};
static proto_entry proto_handlers[MAX_PROTO_HANDLERS];
static int proto_count = 0;

/* Receiver thread control */
static volatile bool net_active = false;

/* UDP "connection" tracking: each outbound UDP (src_port, dst_ip, dst_port)
 * tuple gets a dedicated EwokOS socket and a receiver thread.            */
struct udp_session {
	int         ewok_fd;         /* EwokOS socket fd                    */
	uint16_t    guest_port;      /* Guest source port                   */
	uint32_t    remote_ip;       /* Destination IP (host order)         */
	uint16_t    remote_port;     /* Destination port (host order)       */
	pthread_t   recv_thread;
	bool        active;
	volatile bool thread_done;   /* receiver returned, ready to reap    */
	uint64      last_rx;         /* last datagram arrival (idle reaper) */
	udp_session *next;
};

static pthread_mutex_t udp_mutex;
static udp_session *udp_list;

/* TCP proxy sessions (guest-side TCP terminated here, relayed over a
 * real EwokOS TCP socket).  Stop-and-wait sender + retransmit timer.   */
struct tcp_session {
	int         fd;
	uint16_t    guest_port, remote_port;
	uint32_t    remote_ip;
	uint32_t    guest_isn, our_isn;
	uint32_t    rcv_nxt;                 /* next guest seq expected   */
	uint32_t    snd_una, snd_nxt;        /* our send seq tracking     */
	int         state;
	bool        active, connected;
	bool        fin_pending, fin_sent;
	bool        remote_eof;      /* server closed its side              */
	bool        close_requested; /* wind down once drained / grace lapses */
	volatile bool thread_done;   /* receiver returned, ready to reap    */
	uint64      last_send;
	uint64      close_start;     /* when close_requested was raised     */
	pthread_t   thread;
	pthread_mutex_t lock;
	uint8_t     sbuf[32768];
	int         s_una_off, s_nxt_off, s_tail_off;
	tcp_session *next;
};
enum { TCP_SYN_RECV, TCP_ESTABLISHED, TCP_CLOSED };
static pthread_mutex_t tcp_mutex;
static tcp_session *tcp_list;

/* ICMP raw socket (one shared fd for all ICMP) */
static int icmp_fd = -1;
static pthread_t icmp_recv_thread;

/* ------------------------------------------------------------------ */
/*  Inbound frame queue helpers                                       */
/* ------------------------------------------------------------------ */

static void enqueue_rx(const uint8_t *frame, int len)
{
	if (len < ETH_HLEN || len > 1514)
		return;

	rx_frame *f = (rx_frame *)malloc(sizeof(rx_frame));
	if (!f)
		return;
	memcpy(f->data, frame, len);
	f->len = len;
	f->next = NULL;

	pthread_mutex_lock(&rx_mutex);
	if (rx_count >= RX_QUEUE_MAX) {
		/* Drop oldest to make room */
		rx_frame *old = rx_head;
		if (old) {
			rx_head = old->next;
			if (!rx_head) rx_tail = NULL;
			rx_count--;
			free(old);
		}
	}
	if (rx_tail)
		rx_tail->next = f;
	else
		rx_head = f;
	rx_tail = f;
	rx_count++;
	pthread_mutex_unlock(&rx_mutex);
}

/* Build an Ethernet frame: src/dst MAC + EtherType + payload */
static void build_eth_frame(uint8_t *buf, const uint8_t *dst_mac,
			    const uint8_t *src_mac, uint16_t ethertype,
			    const void *payload, int payload_len)
{
	memcpy(buf, dst_mac, ETH_ALEN);
	memcpy(buf + ETH_ALEN, src_mac, ETH_ALEN);
	wr16(buf + 12, ethertype);
	memcpy(buf + ETH_HLEN, payload, payload_len);
}

/* ------------------------------------------------------------------ */
/*  ARP handling                                                      */
/* ------------------------------------------------------------------ */

/* Handle an inbound ARP request/reply.  Returns true if we generated
 * a reply frame that was enqueued.                                     */
static bool handle_arp(const uint8_t *frame, int len)
{
	if (len < ETH_HLEN + 28)  /* ARP header = 28 bytes */
		return false;

	const uint8_t *arp = frame + ETH_HLEN;
	uint16_t hw_type  = rd16(arp + 0);
	uint16_t proto    = rd16(arp + 2);
	uint16_t opcode   = rd16(arp + 6);

	if (hw_type != 1 || proto != ETH_P_IP)
		return false;

	/* We only handle REQUEST */
	if (opcode != ARP_OP_REQUEST)
		return false;

	/* Target IP (bytes 24-27 of ARP) */
	uint32_t target_ip = rd32(arp + 24);

	/* Never respond for the guest's own IP: Mac OS duplicate-address
	 * detection probes it at startup and would see our gateway MAC as
	 * an address conflict, shutting the interface down.  Proxy-ARP the
	 * gateway (and any other host) so all traffic flows via the NAT. */
	if (target_ip == GUEST_IP)
		return false;

	/* Build ARP reply */
	uint8_t arp_reply[28];
	memset(arp_reply, 0, sizeof(arp_reply));
	wr16(arp_reply + 0, 1);          /* hw type: Ethernet   */
	wr16(arp_reply + 2, ETH_P_IP);   /* proto               */
	arp_reply[4] = ETH_ALEN;         /* hw addr len          */
	arp_reply[5] = 4;                /* proto addr len       */
	wr16(arp_reply + 6, ARP_OP_REPLY);

	/* Sender = the IP being asked about (gateway MAC) */
	memcpy(arp_reply + 8,  gateway_mac, ETH_ALEN);   /* sender hw  */
	wr32(arp_reply + 14, target_ip);                 /* sender ip  */

	/* Target = the original sender */
	memcpy(arp_reply + 18, frame + ETH_HLEN + 8, ETH_ALEN);  /* target hw */
	memcpy(arp_reply + 24, frame + ETH_HLEN + 14, 4);        /* target ip */

	/* Ethernet frame: reply goes back to the requester */
	uint8_t eth[1514];
	build_eth_frame(eth, frame + ETH_ALEN /* dst = requester MAC */,
			gateway_mac /* src = gateway MAC */,
			ETH_P_ARP, arp_reply, 28);
	enqueue_rx(eth, ETH_HLEN + 28);
	return true;
}

/* ------------------------------------------------------------------ */
/*  ICMP forwarding                                                   */
/* ------------------------------------------------------------------ */

/* Ensure the shared ICMP raw socket is open */
static int ensure_icmp_fd(void)
{
	if (icmp_fd >= 0)
		return icmp_fd;
	icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (icmp_fd < 0)
		return icmp_fd;

	/* Receive timeout arms the netd RECV deadline sweep. Without it the
	 * recvfrom() task stays armed forever on a silent socket, and
	 * ether_exit()'s pthread_join() of icmp_recv_thread wedges the whole
	 * shutdown (the process never exits and every netd worker parked on
	 * our sockets leaks). Mirrors the UDP session cadence.              */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 300000;
	setsockopt(icmp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return icmp_fd;
}

/* Receiver thread for ICMP: reads from the EwokOS RAW socket and
 * wraps responses into Ethernet frames for the guest.               */
static void *icmp_recv_func(void *arg)
{
	(void)arg;
	uint8_t buf[1500];
	while (net_active) {
		struct sockaddr_in from;
		uint32_t from_len = sizeof(from);
		memset(&from, 0, sizeof(from));
		int n = recvfrom(icmp_fd, buf, sizeof(buf), 0,
				 (struct sockaddr *)&from, &from_len);
		if (n <= 0) {
			/* SO_RCVTIMEO expiries are the poll cadence, not errors:
			 * re-check net_active immediately so ether_exit()'s join
			 * completes promptly. Real errors back off briefly.      */
			if (n < 0 && errno != EWOK_SOCK_EAGAIN &&
			    errno != EWOK_SOCK_ETIMEDOUT)
				usleep(10000);  /* 10ms back-off on error */
			continue;
		}

		/* The RAW socket gives us the ICMP payload (starting at
		 * the ICMP header).  We need to wrap it in an IP header
		 * and then an Ethernet frame.                             */
		int icmp_len = n;
		int ip_total = 20 + icmp_len;  /* IP header + ICMP */

		uint8_t ip_pkt[1500];
		memset(ip_pkt, 0, 20);
		ip_pkt[0] = 0x45;                          /* ver=4, ihl=5 */
		ip_pkt[2] = (uint8_t)(ip_total >> 8);
		ip_pkt[3] = (uint8_t)(ip_total);
		ip_pkt[8] = 64;                             /* TTL           */
		ip_pkt[9] = IP_PROTO_ICMP;
		/* src = remote host, dst = guest */
		wr32(ip_pkt + 12, ntohl(from.sin_addr.s_un.s_addr));
		wr32(ip_pkt + 16, GUEST_IP);
		uint16_t cksum = ip_checksum(ip_pkt, 20);
		wr16(ip_pkt + 10, cksum);
		memcpy(ip_pkt + 20, buf, icmp_len);

		uint8_t eth[1514];
		build_eth_frame(eth, ether_addr /* dst = guest MAC */,
				gateway_mac /* src = gateway MAC */,
				ETH_P_IP, ip_pkt, ip_total);
		enqueue_rx(eth, ETH_HLEN + ip_total);
	}
	return NULL;
}

/* Forward an ICMP Echo Request from the guest to the real network */
static void forward_icmp(const uint8_t *ip_pkt, int ip_len)
{
	if (ip_len < 20)
		return;

	int ihl = (ip_pkt[0] & 0x0f) * 4;
	if (ihl < 20 || ihl > ip_len)
		return;

	int fd = ensure_icmp_fd();
	if (fd < 0)
		return;

	const uint8_t *icmp_data = ip_pkt + ihl;
	int icmp_len = ip_len - ihl;
	if (icmp_len < 8)  /* minimum ICMP Echo */
		return;

	uint32_t dst_ip = rd32(ip_pkt + 16);  /* dst IP from IP hdr */

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_un.s_addr = htonl(dst_ip);

	/* Send the ICMP payload (starting at ICMP header) via RAW socket */
	sendto(fd, icmp_data, icmp_len, 0,
	       (struct sockaddr *)&dst, sizeof(dst));
}

/* ------------------------------------------------------------------ */
/*  UDP session management                                            */
/* ------------------------------------------------------------------ */

static udp_session *find_udp_session(uint16_t guest_port,
				     uint32_t remote_ip,
				     uint16_t remote_port)
{
	for (udp_session *s = udp_list; s; s = s->next) {
		if (s->active &&
		    s->guest_port  == guest_port &&
		    s->remote_ip   == remote_ip &&
		    s->remote_port == remote_port)
			return s;
	}
	return NULL;
}

/* Receiver thread for a UDP session */
static void *udp_recv_func(void *arg)
{
	udp_session *s = (udp_session *)arg;
	uint8_t buf[1500];

	while (net_active && s->active) {
		struct sockaddr_in from;
		uint32_t from_len = sizeof(from);
		memset(&from, 0, sizeof(from));
		int n = recvfrom(s->ewok_fd, buf, sizeof(buf), 0,
				 (struct sockaddr *)&from, &from_len);
		if (n <= 0) {
			/* Timeouts (SO_RCVTIMEO poll cadence) are expected; re-arm
			 * immediately. Hard errors pause briefly to avoid spinning. */
			if (n < 0 && errno != EWOK_SOCK_EAGAIN &&
			    errno != EWOK_SOCK_ETIMEDOUT)
				usleep(10000);
			/* Idle NAT mapping expiry: no inbound traffic for a long
			 * while means the session is over -- end the thread
			 * instead of polling until emulator exit. The fd is
			 * taken under udp_mutex so a concurrent forward_udp
			 * sendto can never hit a reused fd.                   */
			if (now_usec() - s->last_rx > UDP_IDLE_USEC) {
				pthread_mutex_lock(&udp_mutex);
				s->active = false;
				int fd = s->ewok_fd;
				s->ewok_fd = -1;
				pthread_mutex_unlock(&udp_mutex);
				if (fd >= 0)
					close(fd);
				break;
			}
			continue;
		}

		s->last_rx = now_usec();

		/* Build a UDP/IP/Ethernet frame for the guest */
		int udp_len = 8 + n;
		int ip_total = 20 + udp_len;

		uint8_t ip_pkt[1500];
		memset(ip_pkt, 0, 20);
		ip_pkt[0] = 0x45;
		ip_pkt[2] = (uint8_t)(ip_total >> 8);
		ip_pkt[3] = (uint8_t)(ip_total);
		ip_pkt[8] = 64;
		ip_pkt[9] = IP_PROTO_UDP;
		wr32(ip_pkt + 12, ntohl(from.sin_addr.s_un.s_addr));  /* src = remote */
		wr32(ip_pkt + 16, GUEST_IP);               /* dst = guest  */
		uint16_t ck = ip_checksum(ip_pkt, 20);
		wr16(ip_pkt + 10, ck);

		/* UDP header */
		uint8_t *udp = ip_pkt + 20;
		wr16(udp + 0, s->remote_port);     /* src port = remote  */
		wr16(udp + 2, s->guest_port);      /* dst port = guest   */
		wr16(udp + 4, (uint16_t)udp_len);  /* length             */
		wr16(udp + 6, 0);                  /* checksum (filled below) */
		memcpy(udp + 8, buf, n);
		wr16(udp + 6, udp_checksum(ip_pkt, ip_total));

		uint8_t eth[1514];
		build_eth_frame(eth, ether_addr /* dst = guest MAC */,
				gateway_mac /* src = gateway MAC */,
				ETH_P_IP, ip_pkt, ip_total);
		enqueue_rx(eth, ETH_HLEN + ip_total);
	}
	s->thread_done = true;
	return NULL;
}

/* Reap finished UDP sessions.  Caller holds udp_mutex.  thread_done is
 * published only after the receiver has dropped every lock, so the join
 * cannot deadlock. */
static void reap_udp_locked(void)
{
	udp_session **pp = &udp_list;
	while (*pp) {
		udp_session *s = *pp;
		if (!s->active && s->thread_done) {
			*pp = s->next;
			pthread_join(s->recv_thread, NULL);
			if (s->ewok_fd >= 0)
				close(s->ewok_fd);
			free(s);
		} else {
			pp = &s->next;
		}
	}
}

/* Caller holds udp_mutex */
static udp_session *create_udp_session_locked(uint16_t guest_port,
					      uint32_t remote_ip,
					      uint16_t remote_port)
{
	reap_udp_locked();

	udp_session *s = (udp_session *)calloc(1, sizeof(udp_session));
	if (!s) return NULL;

	s->ewok_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s->ewok_fd < 0) {
		free(s);
		return NULL;
	}

	/* No bind(): mirror EwokOS dns_resolve() - sendto() auto-assigns an
	 * ephemeral local port and registers the PCB so replies are routed
	 * back to this socket.  Binding to port 0 broke reception.          */

	s->guest_port  = guest_port;
	s->remote_ip   = remote_ip;
	s->remote_port = remote_port;
	s->active      = true;
	s->last_rx     = now_usec();

	/* Receive timeout sets the re-arm cadence only. It must NOT be
	 * commensurate with the guest's DNS retransmit period (~1s): a
	 * phase-locked timeout completion collides with the arriving reply,
	 * and replies that land while recvfrom is momentarily unarmed are
	 * stranded. 300ms keeps recvfrom armed ~99% of the time. */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 300000;
	setsockopt(s->ewok_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	pthread_create(&s->recv_thread, NULL, udp_recv_func, s);

	s->next = udp_list;
	udp_list = s;

	return s;
}

static void forward_udp(const uint8_t *ip_pkt, int ip_len)
{
	if (ip_len < 20)
		return;
	int ihl = (ip_pkt[0] & 0x0f) * 4;
	if (ihl < 20 || ihl + 8 > ip_len)
		return;

	const uint8_t *udp = ip_pkt + ihl;
	uint16_t src_port  = rd16(udp + 0);
	uint16_t dst_port  = rd16(udp + 2);
	uint16_t udp_len   = rd16(udp + 4);
	uint32_t dst_ip    = rd32(ip_pkt + 16);

	int payload_len = udp_len - 8;
	if (payload_len < 0 || ihl + udp_len > ip_len)
		return;
	const uint8_t *payload = udp + 8;

	/* Find or create a UDP session, and send, all under udp_mutex so
	 * the idle reaper can never free a session while it is in use.   */
	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_un.s_addr = htonl(dst_ip);
	dst.sin_port = htons(dst_port);

	pthread_mutex_lock(&udp_mutex);
	udp_session *s = find_udp_session(src_port, dst_ip, dst_port);
	if (!s)
		s = create_udp_session_locked(src_port, dst_ip, dst_port);
	if (s && s->ewok_fd >= 0)
		sendto(s->ewok_fd, payload, payload_len, 0,
		       (struct sockaddr *)&dst, sizeof(dst));
	pthread_mutex_unlock(&udp_mutex);
}

/* ------------------------------------------------------------------ */
/*  TCP proxy (Phase 2)                                               */
/* ------------------------------------------------------------------ */

static tcp_session *find_tcp_session(uint16_t guest_port, uint32_t remote_ip,
				     uint16_t remote_port)
{
	for (tcp_session *t = tcp_list; t; t = t->next)
		if (t->active && t->guest_port == guest_port &&
		    t->remote_ip == remote_ip && t->remote_port == remote_port)
			return t;
	return NULL;
}

/* TCP checksum over pseudo-header + segment (checksum field zeroed) */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
			     const uint8_t *seg, int len)
{
	uint8_t buf[1520];
	wr32(buf + 0, src_ip);
	wr32(buf + 4, dst_ip);
	buf[8] = 0;
	buf[9] = IP_PROTO_TCP;
	wr16(buf + 10, (uint16_t)len);
	memcpy(buf + 12, seg, len);
	return ip_checksum(buf, 12 + len);
}

/* Build a TCP segment remote_ip:remote_port -> guest and enqueue it */
static void emit_tcp(tcp_session *t, uint8_t flags, const uint8_t *payload,
		     int plen, uint32_t seq)
{
	uint8_t seg[1500];
	int hlen = 20;
	memset(seg, 0, hlen);
	wr16(seg + 0, t->remote_port);   /* src = remote server        */
	wr16(seg + 2, t->guest_port);
	wr32(seg + 4, seq);
	wr32(seg + 8, t->rcv_nxt);
	seg[12] = (uint8_t)(5 << 4);     /* data offset = 5            */
	seg[13] = flags;
	wr16(seg + 14, 0xffff);          /* advertised window          */
	if (plen)
		memcpy(seg + hlen, payload, plen);
	wr16(seg + 16, tcp_checksum(t->remote_ip, GUEST_IP, seg, hlen + plen));

	int ip_total = 20 + hlen + plen;
	uint8_t ip_pkt[1500];
	memset(ip_pkt, 0, 20);
	ip_pkt[0] = 0x45;
	ip_pkt[2] = (uint8_t)(ip_total >> 8);
	ip_pkt[3] = (uint8_t)(ip_total);
	ip_pkt[8] = 64;
	ip_pkt[9] = IP_PROTO_TCP;
	wr32(ip_pkt + 12, t->remote_ip);
	wr32(ip_pkt + 16, GUEST_IP);
	wr16(ip_pkt + 10, ip_checksum(ip_pkt, 20));
	memcpy(ip_pkt + 20, seg, hlen + plen);

	uint8_t eth[1514];
	build_eth_frame(eth, ether_addr, gateway_mac, ETH_P_IP, ip_pkt, ip_total);
	enqueue_rx(eth, ETH_HLEN + ip_total);
}

/* Sender: push queued bytes / retransmit / FIN (lock held) */
static void tcp_pump_locked(tcp_session *t)
{
	if (!t->connected)
		return;
	int unacked = t->s_nxt_off - t->s_una_off;
	int queued  = t->s_tail_off - t->s_nxt_off;

	if (unacked == 0 && queued > 0) {
		int len = queued > TCP_MSS ? TCP_MSS : queued;
		emit_tcp(t, TH_ACK | TH_PSH, t->sbuf + t->s_nxt_off, len, t->snd_nxt);
		t->snd_nxt += len;
		t->s_nxt_off += len;
		t->last_send = now_usec();
	} else if (unacked > 0) {
		uint64 now = now_usec();
		if (now - t->last_send > TCP_RETRANS_USEC) {
			emit_tcp(t, TH_ACK | TH_PSH, t->sbuf + t->s_una_off,
				 unacked, t->snd_una);
			t->last_send = now;
		}
	} else if (t->fin_pending && !t->fin_sent) {
		emit_tcp(t, TH_FIN | TH_ACK, NULL, 0, t->snd_nxt);
		t->snd_nxt++;
		t->fin_sent = true;
	}
}

static void *tcp_recv_func(void *arg)
{
	tcp_session *t = (tcp_session *)arg;

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_un.s_addr = htonl(t->remote_ip);
	sa.sin_port = htons(t->remote_port);

	/* Short timeout so the pump loop re-runs promptly after guest ACKs */
	struct timeval tv;
	tv.tv_sec = 0; tv.tv_usec = 10000;
	setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	int cr = connect(t->fd, (struct sockaddr *)&sa, sizeof(sa));
	pthread_mutex_lock(&t->lock);
	if (cr < 0) {
		emit_tcp(t, TH_RST, NULL, 0, t->snd_nxt);
		t->active = false;
		int fd = t->fd;
		t->fd = -1;
		pthread_mutex_unlock(&t->lock);
		if (fd >= 0)
			close(fd);
		t->thread_done = true;
		return NULL;
	}
	t->connected = true;
	/* Guest data queued before connect() finished (e.g. the HTTP GET in
	 * the ACK that completed the guest-side handshake): forward it now.
	 * write() happens with the lock dropped; it travels through netd's
	 * write_state worker and may take a while.                        */
	int boot_len = t->s_tail_off - t->s_una_off;
	uint8_t boot_buf[2048];
	if (boot_len > 0 && boot_len <= (int)sizeof(boot_buf))
		memcpy(boot_buf, t->sbuf + t->s_una_off, boot_len);
	int fd = t->fd;
	pthread_mutex_unlock(&t->lock);
	if (boot_len > 0 && fd >= 0)
		write(fd, boot_buf, boot_len);
	pthread_mutex_lock(&t->lock);
	if (!t->active) {
		fd = t->fd;
		t->fd = -1;
		pthread_mutex_unlock(&t->lock);
		if (fd >= 0)
			close(fd);
		t->thread_done = true;
		return NULL;
	}
	pthread_mutex_unlock(&t->lock);

	uint8_t buf[2048];
	for (;;) {
		pthread_mutex_lock(&t->lock);
		if (!t->active) {
			fd = t->fd;
			t->fd = -1;
			pthread_mutex_unlock(&t->lock);
			if (fd >= 0)
				close(fd);
			break;
		}
		tcp_pump_locked(t);
		/* Session winding down (guest FIN or server EOF): once the
		 * buffered data is drained/ACKed -- or the grace period lapses
		 * -- tear down the real socket from this thread (recv() is not
		 * armed on it right now) and exit, instead of polling the
		 * silent socket until emulator exit.                        */
		if (t->close_requested) {
			bool drained = (t->s_nxt_off - t->s_una_off) == 0 &&
				       (t->s_tail_off - t->s_nxt_off) == 0;
			if (drained ||
			    now_usec() - t->close_start > TCP_CLOSE_GRACE_USEC) {
				fd = t->fd;
				t->fd = -1;
				t->active = false;
				pthread_mutex_unlock(&t->lock);
				if (fd >= 0)
					close(fd);
				break;
			}
		}
		if (t->remote_eof) {
			/* EOF is sticky: re-arming recv() would busy-loop. Stay
			 * in a pump-only drain phase until the close above.    */
			pthread_mutex_unlock(&t->lock);
			usleep(10000);
			continue;
		}
		fd = t->fd;
		pthread_mutex_unlock(&t->lock);

		int n = recv(fd, buf, sizeof(buf), 0);
		/* Capture errno now: libewoksys resets the global errno on
		 * successful libc calls, so any comparison must use this
		 * saved copy. */
		int rerr = errno;
		if (n > 0) {
			pthread_mutex_lock(&t->lock);
			if (t->s_tail_off + n <= (int)sizeof(t->sbuf)) {
				memcpy(t->sbuf + t->s_tail_off, buf, n);
				t->s_tail_off += n;
			}
			pthread_mutex_unlock(&t->lock);
		} else if (n == 0) {
			/* Remote closed: send our FIN to the guest and wind the
			 * session down once buffered data is drained.         */
			pthread_mutex_lock(&t->lock);
			t->remote_eof = true;
			t->fin_pending = true;
			if (!t->close_requested) {
				t->close_requested = true;
				t->close_start = now_usec();
			}
			tcp_pump_locked(t);
			pthread_mutex_unlock(&t->lock);
		} else {
			/* SO_RCVTIMEO expiry surfaces as ETIMEDOUT; treat like EAGAIN. */
			if (rerr != EWOK_SOCK_EAGAIN && rerr != EWOK_SOCK_ETIMEDOUT) {
				pthread_mutex_lock(&t->lock);
				t->active = false;
				pthread_mutex_unlock(&t->lock);
			}
		}
	}
	t->thread_done = true;
	return NULL;
}

/* Reap finished TCP sessions.  Caller holds tcp_mutex.  thread_done is
 * published only after the receiver has dropped every lock; taking t->lock
 * here just waits out a forward_tcp that locked the session before it was
 * unlinked, so the join cannot deadlock.  Every other path reaches the
 * session only through find_tcp_session(), which skips inactive ones. */
static void reap_tcp_locked(void)
{
	tcp_session **pp = &tcp_list;
	while (*pp) {
		tcp_session *t = *pp;
		if (!t->active && t->thread_done) {
			*pp = t->next;
			pthread_mutex_lock(&t->lock);
			pthread_mutex_unlock(&t->lock);
			pthread_join(t->thread, NULL);
			if (t->fd >= 0)
				close(t->fd);
			pthread_mutex_destroy(&t->lock);
			free(t);
		} else {
			pp = &t->next;
		}
	}
}

static tcp_session *create_tcp_session(uint16_t guest_port, uint32_t remote_ip,
				       uint16_t remote_port, uint32_t guest_isn)
{
	tcp_session *t = (tcp_session *)calloc(1, sizeof(tcp_session));
	if (!t) return NULL;

	t->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (t->fd < 0) { free(t); return NULL; }

	t->guest_port  = guest_port;
	t->remote_ip   = remote_ip;
	t->remote_port = remote_port;
	t->guest_isn   = guest_isn;
	t->our_isn     = (uint32_t)rand();
	t->rcv_nxt     = guest_isn + 1;
	t->snd_una     = t->our_isn + 1;
	t->snd_nxt     = t->our_isn + 1;
	t->state       = TCP_SYN_RECV;
	t->active      = true;
	pthread_mutex_init(&t->lock, NULL);

	/* SYN-ACK back to the guest */
	emit_tcp(t, TH_SYN | TH_ACK, NULL, 0, t->our_isn);

	pthread_create(&t->thread, NULL, tcp_recv_func, t);

	pthread_mutex_lock(&tcp_mutex);
	reap_tcp_locked();
	t->next = tcp_list;
	tcp_list = t;
	pthread_mutex_unlock(&tcp_mutex);
	return t;
}

static void forward_tcp(const uint8_t *ip_pkt, int ip_len)
{
	if (ip_len < 20)
		return;
	int ihl = (ip_pkt[0] & 0x0f) * 4;
	if (ihl < 20 || ihl + 20 > ip_len)
		return;

	const uint8_t *tcp = ip_pkt + ihl;
	uint16_t src_port = rd16(tcp + 0);
	uint16_t dst_port = rd16(tcp + 2);
	uint32_t seq      = rd32(tcp + 4);
	uint32_t ack      = rd32(tcp + 8);
	int th_off        = ((tcp[12] >> 4) & 0x0f) * 4;
	uint8_t flags     = tcp[13];
	uint32_t dst_ip   = rd32(ip_pkt + 16);
	int plen          = ip_len - ihl - th_off;
	const uint8_t *payload = tcp + th_off;
	if (plen < 0) plen = 0;

	/* New connection request */
	if ((flags & TH_SYN) && !(flags & TH_ACK)) {
		pthread_mutex_lock(&tcp_mutex);
		bool exists = (find_tcp_session(src_port, dst_ip, dst_port) != NULL);
		pthread_mutex_unlock(&tcp_mutex);
		if (!exists)
			create_tcp_session(src_port, dst_ip, dst_port, seq);
		return;
	}

	/* Lookup under tcp_mutex so the session reaper can never free a
	 * session between the find and the session-lock acquisition.     */
	pthread_mutex_lock(&tcp_mutex);
	tcp_session *t = find_tcp_session(src_port, dst_ip, dst_port);
	if (t)
		pthread_mutex_lock(&t->lock);
	pthread_mutex_unlock(&tcp_mutex);
	if (!t)
		return;

	/* Guest ACKs our data */
	if (flags & TH_ACK) {
		int32_t acked = (int32_t)(ack - t->snd_una);
		if (acked > 0) {
			int sent = t->s_nxt_off - t->s_una_off;
			if (acked > sent) acked = sent;
			t->s_una_off += acked;
			t->snd_una   += acked;
			if (t->s_una_off > 0) {
				int move = t->s_tail_off - t->s_una_off;
				memmove(t->sbuf, t->sbuf + t->s_una_off, move);
				t->s_nxt_off  -= t->s_una_off;
				t->s_tail_off -= t->s_una_off;
				t->s_una_off = 0;
			}
		}
	}

	if (flags & TH_RST) {
		t->active = false;
		pthread_mutex_unlock(&t->lock);
		return;
	}

	/* Handshake completion */
	if ((flags & TH_ACK) && t->state == TCP_SYN_RECV &&
	    (int32_t)(ack - (t->our_isn + 1)) == 0)
		t->state = TCP_ESTABLISHED;

	/* Guest data -> real socket.
	 * Queue into sbuf even while connect() is still in flight: the
	 * guest's ACK+GET commonly beats our real connect() completion, and
	 * dropping it (while ACKing the guest) leaves the server silent
	 * forever. tcp_recv_func drains sbuf to the socket after connect.
	 * write() itself is issued with the session lock held -- it goes
	 * through netd's write_state slot so it does not collide with the
	 * recv thread's armed recv(), and the lock keeps sbuf stable.    */
	int wr_len = 0;
	uint8_t wr_buf[1520];
	if (plen > 0) {
		if ((int32_t)(seq - t->rcv_nxt) == 0) {
			t->rcv_nxt += plen;
			if (t->s_tail_off + plen <= (int)sizeof(t->sbuf)) {
				memcpy(t->sbuf + t->s_tail_off, payload, plen);
				t->s_tail_off += plen;
			}
			if (t->connected) {
				wr_len = t->s_tail_off - t->s_una_off;
				if (wr_len > (int)sizeof(wr_buf))
					wr_len = sizeof(wr_buf);
				memcpy(wr_buf, t->sbuf + t->s_una_off, wr_len);
			}
		}
		emit_tcp(t, TH_ACK, NULL, 0, t->snd_nxt);
	}

	/* Guest FIN: ACK it and answer with our own FIN.  The real socket is
	 * NOT closed here -- EwokOS libsocket shutdown() maps to SOCK_CLOSE
	 * (a full close, the 'how' arg is dropped) and would tear it down
	 * while the recv thread still has recv() armed on it.  Instead
	 * close_requested tells the recv thread to drain the remaining data
	 * and close the socket on its own (recv() is unarmed at that point),
	 * so the thread exits once the session is over.                   */
	if (flags & TH_FIN) {
		t->rcv_nxt = seq + plen + 1;
		emit_tcp(t, TH_ACK, NULL, 0, t->snd_nxt);
		t->fin_pending = true;
		if (!t->close_requested) {
			t->close_requested = true;
			t->close_start = now_usec();
		}
	}

	/* Write queued guest->server bytes on the real socket.  fd is -1
	 * once the recv thread has torn the session down.               */
	if (wr_len > 0 && t->fd >= 0)
		write(t->fd, wr_buf, wr_len);

	pthread_mutex_unlock(&t->lock);
}

/* ------------------------------------------------------------------ */
/*  IP packet dispatch (outbound from guest)                          */
/* ------------------------------------------------------------------ */

static void handle_ip_packet(const uint8_t *ip_pkt, int ip_len)
{
	if (ip_len < 20)
		return;
	uint8_t proto = ip_pkt[9];
	/* uint32_t src_ip = rd32(ip_pkt + 12); */
	/* uint32_t dst_ip = rd32(ip_pkt + 16); */

	switch (proto) {
	case IP_PROTO_ICMP:
		forward_icmp(ip_pkt, ip_len);
		break;
	case IP_PROTO_UDP:
		forward_udp(ip_pkt, ip_len);
		break;
	case IP_PROTO_TCP:
		forward_tcp(ip_pkt, ip_len);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/*  ether_write - transmit a raw Ethernet frame from the guest        */
/* ------------------------------------------------------------------ */

int16 ether_write(uint32 wds)
{
	uint8_t pkt[1514];
	int len = ether_wds_to_buffer(wds, pkt);
	if (len < ETH_HLEN)
		return noErr;

	/* Stamp gateway source MAC into the frame */
	memcpy(pkt + ETH_ALEN, gateway_mac, ETH_ALEN);

	uint16_t ethertype = rd16(pkt + 12);

	switch (ethertype) {
	case ETH_P_ARP:
		handle_arp(pkt, len);
		break;
	case ETH_P_IP:
		handle_ip_packet(pkt + ETH_HLEN, len - ETH_HLEN);
		break;
	default:
		break;
	}

	return noErr;
}

/* ------------------------------------------------------------------ */
/*  EtherInterrupt - deliver queued inbound frames to the guest       */
/* ------------------------------------------------------------------ */

void EtherInterrupt(void)
{
	pthread_mutex_lock(&rx_mutex);
	while (rx_head) {
		rx_frame *f = rx_head;
		rx_head = f->next;
		if (!rx_head)
			rx_tail = NULL;
		rx_count--;
		pthread_mutex_unlock(&rx_mutex);

		/* Deliver the frame to the guest via registered protocol
		 * handlers, mirroring the UDP-tunnel path in ether.cpp.   */
		uint32 packet_addr = 0;
		{
			/* Allocate a temporary buffer in MacOS RAM */
			M68kRegisters r;
			r.d[0] = f->len;
			Execute68kTrap(0xa71e, &r);  /* NewPtrSysClear */
			packet_addr = r.a[0];
		}
		if (packet_addr) {
			Host2Mac_memcpy(packet_addr, f->data, f->len);

			/* Determine EtherType and look up handler */
			uint16 type = rd16(f->data + 12);
			uint16 search_type = (type <= 1500 ? 0 : type);

			if (ether_data) {
				uint32 handler = 0;
				for (int i = 0; i < proto_count; i++) {
					if (proto_handlers[i].type == search_type) {
						handler = proto_handlers[i].handler;
						break;
					}
				}
				if (handler) {
					/* Copy header to RHA */
					Mac2Mac_memcpy(ether_data + ed_RHA,
						       packet_addr, 14);
					M68kRegisters r;
					r.d[0] = type;
					r.d[1] = f->len - 14;
					r.a[0] = packet_addr + 14;
					r.a[3] = ether_data + ed_RHA + 14;
					r.a[4] = ether_data + ed_ReadPacket;
					Execute68k(handler, &r);
				}
			}

			/* Free the temporary buffer */
			M68kRegisters r2;
			r2.a[0] = packet_addr;
			Execute68kTrap(0xa01f, &r2);  /* DisposePtr */
		}

		free(f);
		pthread_mutex_lock(&rx_mutex);
	}
	pthread_mutex_unlock(&rx_mutex);
}

/* ------------------------------------------------------------------ */
/*  Protocol handler registration                                     */
/* ------------------------------------------------------------------ */

int16 ether_attach_ph(uint16 type, uint32 handler)
{
	/* Update existing entry if present */
	for (int i = 0; i < proto_count; i++) {
		if (proto_handlers[i].type == type) {
			proto_handlers[i].handler = handler;
			return noErr;
		}
	}
	/* Add new entry */
	if (proto_count < MAX_PROTO_HANDLERS) {
		proto_handlers[proto_count].type = type;
		proto_handlers[proto_count].handler = handler;
		proto_count++;
	}
	return noErr;
}

int16 ether_detach_ph(uint16 type)
{
	for (int i = 0; i < proto_count; i++) {
		if (proto_handlers[i].type == type) {
			/* Shift remaining entries down */
			for (int j = i; j < proto_count - 1; j++)
				proto_handlers[j] = proto_handlers[j + 1];
			proto_count--;
			break;
		}
	}
	return noErr;
}

/* ------------------------------------------------------------------ */
/*  Multicast (no-op for SLIRP)                                       */
/* ------------------------------------------------------------------ */

int16 ether_add_multicast(uint32 pb) { return noErr; }
int16 ether_del_multicast(uint32 pb) { return noErr; }

/* ------------------------------------------------------------------ */
/*  Initialization / Shutdown                                         */
/* ------------------------------------------------------------------ */

bool ether_init(void)
{
	pthread_mutex_init(&rx_mutex, NULL);
	pthread_mutex_init(&udp_mutex, NULL);
	pthread_mutex_init(&tcp_mutex, NULL);
	rx_head = NULL;
	rx_tail = NULL;
	rx_count = 0;
	udp_list = NULL;
	tcp_list = NULL;
	icmp_fd = -1;
	net_active = true;

	/* Generate a locally-administered MAC for the guest if not
	 * already set by the upper layer (EtherInit sets ether_addr). */
	ether_addr[0] = 0x52;  /* 'R' */
	ether_addr[1] = 0x54;  /* 'T' */
	ether_addr[2] = (uint8_t)(GUEST_IP >> 24);
	ether_addr[3] = (uint8_t)(GUEST_IP >> 16);
	ether_addr[4] = (uint8_t)(GUEST_IP >> 8);
	ether_addr[5] = (uint8_t)(GUEST_IP);
	/* gateway_mac is a separate constant (52:54:0A:00:00:01) */

	/* Pre-open the ICMP socket and start its receiver */
	if (ensure_icmp_fd() >= 0) {
		pthread_create(&icmp_recv_thread, NULL, icmp_recv_func, NULL);
	}

	return true;
}

void ether_exit(void)
{
	net_active = false;

	/* Wait for ICMP thread */
	if (icmp_fd >= 0) {
		pthread_join(icmp_recv_thread, NULL);
		close(icmp_fd);
		icmp_fd = -1;
	}

	/* Tear down UDP sessions.  Two phases: the receiver's idle-expiry
	 * path takes udp_mutex itself, so joining while holding it could
	 * deadlock.  Mark and detach everything under the lock, then join
	 * and free outside it; the inactive flag keeps forward_udp away
	 * from the dying sessions.                                       */
	pthread_mutex_lock(&udp_mutex);
	for (udp_session *m = udp_list; m; m = m->next)
		m->active = false;
	udp_session *s = udp_list;
	udp_list = NULL;
	pthread_mutex_unlock(&udp_mutex);
	while (s) {
		udp_session *next = s->next;
		pthread_join(s->recv_thread, NULL);
		if (s->ewok_fd >= 0)
			close(s->ewok_fd);
		free(s);
		s = next;
	}

	/* Tear down TCP sessions.  The fd is captured under the session
	 * lock: the receiver closes it itself on its way out (-1 marks
	 * that), and for a thread still parked inside connect() the close
	 * is exactly what aborts the connect (netd completes it with an
	 * error), so the join stays bounded.                             */
	pthread_mutex_lock(&tcp_mutex);
	tcp_session *t = tcp_list;
	tcp_list = NULL;
	while (t) {
		tcp_session *next = t->next;
		pthread_mutex_lock(&t->lock);
		t->active = false;
		int fd = t->fd;
		t->fd = -1;
		pthread_mutex_unlock(&t->lock);
		if (fd >= 0)
			close(fd);
		pthread_join(t->thread, NULL);
		pthread_mutex_destroy(&t->lock);
		free(t);
		t = next;
	}
	pthread_mutex_unlock(&tcp_mutex);

	/* Drain the RX queue */
	pthread_mutex_lock(&rx_mutex);
	while (rx_head) {
		rx_frame *f = rx_head;
		rx_head = f->next;
		free(f);
	}
	rx_tail = NULL;
	rx_count = 0;
	pthread_mutex_unlock(&rx_mutex);

	pthread_mutex_destroy(&rx_mutex);
	pthread_mutex_destroy(&udp_mutex);
	proto_count = 0;
}

void ether_reset(void)
{
	proto_count = 0;
}

/* ------------------------------------------------------------------ */
/*  UDP tunnel stubs (not used in SLIRP mode)                         */
/* ------------------------------------------------------------------ */

bool ether_start_udp_thread(int socket_fd) { return false; }
void ether_stop_udp_thread(void) { }

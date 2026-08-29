/*
 *  enet_ewok.c - Ethernet backend for EwokOS (SLIRP-style user-mode NAT)
 *
 *  Replaces the vendored libslirp backend (old enet_slirp.c), modeled on
 *  macemu's src/ewok/ether_ewok.cpp.  The emulator acts as a virtual NAT
 *  router between the guest and the EwokOS socket API:
 *    - Guest IP 10.0.0.2  (handed out via BOOTP/DHCP; the actual guest IP
 *      is learned from outbound traffic so static configs work too)
 *    - Gateway IP 10.0.0.1 (virtual, MAC = gateway_mac)
 *    - ARP: answered locally with the gateway MAC (proxy-ARP for every
 *      address but the guest's own, so all traffic flows via the NAT)
 *    - BOOTP/DHCP: built-in server answers the guest's requests with the
 *      fixed NAT topology, so NeXTSTEP needs no manual TCP/IP settings
 *    - UDP: relayed via EwokOS SOCK_DGRAM sockets
 *    - TCP: guest-side terminated here, relayed over EwokOS SOCK_STREAM
 *    - ICMP: relayed via a shared EwokOS SOCK_RAW socket
 *
 *  The guest MAC/IP are not known upfront (the guest driver programs the
 *  controller's node ID from ROM), so both are learned from outbound
 *  frames - the same way libslirp learns client_ethaddr.
 *
 *  Inbound data from EwokOS sockets is wrapped in Ethernet frames and
 *  queued; enet_ewok_queue_poll() (called from ENET_IO_Handler whenever
 *  the emulated RX buffer is empty) injects one queued frame per call
 *  via enet_receive().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "log.h"
#include "ethernet.h"
#include "enet_ewok.h"

/* libsocket/libgloss report standard errno values (EAGAIN=11, ETIMEDOUT=116);
 * ewoksys <sys/errno.h> assigns different numbers via its enum, so socket
 * results must be compared against the socket layer's actual values. */
#define EWOK_SOCK_EAGAIN	11
#define EWOK_SOCK_ETIMEDOUT	116

/* ------------------------------------------------------------------ */
/*  Virtual network topology                                          */
/* ------------------------------------------------------------------ */

#define GUEST_IP_DEFAULT	0x0A000002  /* 10.0.0.2          */
#define GW_IP			0x0A000001  /* 10.0.0.1 (v-gw)   */
#define NET_MASK		0xFFFFFF00  /* 255.255.255.0     */

/* DNS servers handed to the guest via BOOTP/DHCP. The UDP relay forwards
 * the guest's queries to these IPs, so any reachable public resolver
 * works.                                                             */
static const uint32_t dns_servers[2] = {
    0xDE050505,  /* 223.5.5.5 */
    0x08080808   /* 8.8.8.8   */
};

/* ------------------------------------------------------------------ */
/*  Ethernet / IP constants                                           */
/* ------------------------------------------------------------------ */

#define ETH_ALEN        6
#define ETH_HLEN        14
#define ETH_P_IP        0x0800
#define ETH_P_ARP       0x0806

/* Virtual gateway MAC (distinct from the guest MAC for correct ARP/NAT) */
static const uint8_t gateway_mac[ETH_ALEN] = {
    0x52, 0x54, 0x0A, 0x00, 0x00, 0x01
};

#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* BOOTP / DHCP */
#define BOOTP_SERVER_PORT 67
#define BOOTP_CLIENT_PORT 68
#define BOOTP_REQUEST    1
#define BOOTP_REPLY      2
#define DHCP_DISCOVER    1
#define DHCP_OFFER       2
#define DHCP_REQUEST     3
#define DHCP_ACK         5
#define DHCP_INFORM      8
#define DHCP_LEASE_SEC   86400
#define DHCP_MAGIC       0x63825363

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
/*  Byte-order helpers (wire format is big-endian)                    */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | p[3];
}
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}
static void wr32(uint8_t *p, uint32_t v)
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
    int len;
    uint16_t ck;
    sum += rd16(ip_pkt + 12) + rd16(ip_pkt + 14);  /* src IP   */
    sum += rd16(ip_pkt + 16) + rd16(ip_pkt + 18);  /* dst IP   */
    sum += IP_PROTO_UDP;
    sum += (uint16_t)udp_len;
    len = udp_len;
    while (len > 1) {
        sum += rd16(udp);
        udp += 2;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)(*udp << 8);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    ck = (uint16_t)~sum;
    return ck ? ck : 0xffff;  /* 0 means "no checksum" in UDP */
}

static uint64_t now_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
}

/* ------------------------------------------------------------------ */
/*  State                                                             */
/* ------------------------------------------------------------------ */

/* Inbound frame queue: background threads -> queue_poll -> guest */
struct rx_frame {
    uint8_t data[1514];
    int     len;
    struct rx_frame *next;
};

static pthread_mutex_t rx_mutex;
static struct rx_frame *rx_head;
static struct rx_frame *rx_tail;
static int             rx_count;

/* Receiver thread control */
static volatile int net_active = 0;
static int net_started = 0;    /* backend is up (idempotent start/stop) */
static int enet_inited = 0;    /* one-time mutex init                   */

/* Guest identity, learned from outbound frames (libslirp-style). The
 * MAC doubles as the enet node's programmed ID; the IP defaults to the
 * BOOTP/DHCP lease and follows whatever source address the guest uses. */
static uint8_t  guest_mac[ETH_ALEN];
static int      guest_mac_known = 0;
static uint32_t guest_ip = GUEST_IP_DEFAULT;

/* UDP "connection" tracking: each outbound UDP (src_port, dst_ip, dst_port)
 * tuple gets a dedicated EwokOS socket and a receiver thread.            */
struct udp_session {
    int         ewok_fd;         /* EwokOS socket fd                    */
    uint16_t    guest_port;      /* Guest source port                   */
    uint32_t    remote_ip;       /* Destination IP (host order)         */
    uint16_t    remote_port;     /* Destination port (host order)       */
    pthread_t   recv_thread;
    int         active;
    volatile int thread_done;    /* receiver returned, ready to reap    */
    uint64_t    last_rx;         /* last datagram arrival (idle reaper) */
    struct udp_session *next;
};

static pthread_mutex_t udp_mutex;
static struct udp_session *udp_list;

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
    int         active, connected;
    int         fin_pending, fin_sent;
    int         remote_eof;      /* server closed its side              */
    int         close_requested; /* wind down once drained / grace lapses */
    volatile int thread_done;    /* receiver returned, ready to reap    */
    uint64_t    last_send;
    uint64_t    close_start;     /* when close_requested was raised     */
    pthread_t   thread;
    pthread_mutex_t lock;
    uint8_t     sbuf[32768];
    int         s_una_off, s_nxt_off, s_tail_off;
    struct tcp_session *next;
};
enum { TCP_SYN_RECV, TCP_ESTABLISHED, TCP_CLOSED };
static pthread_mutex_t tcp_mutex;
static struct tcp_session *tcp_list;

/* ICMP raw socket (one shared fd for all ICMP) */
static int icmp_fd = -1;
static pthread_t icmp_recv_thread;

/* ------------------------------------------------------------------ */
/*  Inbound frame queue helpers                                       */
/* ------------------------------------------------------------------ */

static void enqueue_rx(const uint8_t *frame, int len)
{
    struct rx_frame *f;

    if (len < ETH_HLEN || len > 1514)
        return;

    f = (struct rx_frame *)malloc(sizeof(struct rx_frame));
    if (!f)
        return;
    memcpy(f->data, frame, len);
    f->len = len;
    f->next = NULL;

    pthread_mutex_lock(&rx_mutex);
    if (rx_count >= RX_QUEUE_MAX) {
        /* Drop oldest to make room */
        struct rx_frame *old = rx_head;
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

/* Queue an IP datagram for delivery to the guest (unicast) */
static void enqueue_to_guest(const void *ip_pkt, int ip_total)
{
    uint8_t eth[1514];
    if (!guest_mac_known)
        return;
    build_eth_frame(eth, guest_mac /* dst = guest MAC */,
            gateway_mac /* src = gateway MAC */,
            ETH_P_IP, ip_pkt, ip_total);
    enqueue_rx(eth, ETH_HLEN + ip_total);
}

/* ------------------------------------------------------------------ */
/*  ARP handling                                                      */
/* ------------------------------------------------------------------ */

/* Handle an inbound ARP request/reply */
static void handle_arp(const uint8_t *frame, int len)
{
    const uint8_t *arp;
    uint16_t hw_type, proto, opcode;
    uint32_t target_ip;
    uint8_t arp_reply[28];
    uint8_t eth[1514];

    if (len < ETH_HLEN + 28)  /* ARP header = 28 bytes */
        return;

    arp = frame + ETH_HLEN;
    hw_type = rd16(arp + 0);
    proto   = rd16(arp + 2);
    opcode  = rd16(arp + 6);

    if (hw_type != 1 || proto != ETH_P_IP)
        return;

    /* We only handle REQUEST */
    if (opcode != ARP_OP_REQUEST)
        return;

    /* Target IP (bytes 24-27 of ARP) */
    target_ip = rd32(arp + 24);

    /* Never respond for the guest's own IP: the guest's duplicate-address
     * probe at startup would see our gateway MAC as an address conflict
     * and shut the interface down.  Proxy-ARP everything else so all
     * traffic flows via the NAT.                                       */
    if (target_ip == guest_ip)
        return;

    /* Build ARP reply */
    memset(arp_reply, 0, sizeof(arp_reply));
    wr16(arp_reply + 0, 1);          /* hw type: Ethernet   */
    wr16(arp_reply + 2, ETH_P_IP);   /* proto               */
    arp_reply[4] = ETH_ALEN;         /* hw addr len         */
    arp_reply[5] = 4;                /* proto addr len      */
    wr16(arp_reply + 6, ARP_OP_REPLY);

    /* Sender = the IP being asked about (gateway MAC) */
    memcpy(arp_reply + 8,  gateway_mac, ETH_ALEN);   /* sender hw  */
    wr32(arp_reply + 14, target_ip);                 /* sender ip  */

    /* Target = the original sender */
    memcpy(arp_reply + 18, frame + ETH_HLEN + 8, ETH_ALEN);  /* target hw */
    memcpy(arp_reply + 24, frame + ETH_HLEN + 14, 4);        /* target ip */

    /* Ethernet frame: reply goes back to the requester */
    build_eth_frame(eth, frame + ETH_ALEN /* dst = requester MAC */,
            gateway_mac /* src = gateway MAC */,
            ETH_P_ARP, arp_reply, 28);
    enqueue_rx(eth, ETH_HLEN + 28);
}

/* ------------------------------------------------------------------ */
/*  ICMP forwarding                                                   */
/* ------------------------------------------------------------------ */

/* Ensure the shared ICMP raw socket is open */
static int ensure_icmp_fd(void)
{
    struct timeval tv;

    if (icmp_fd >= 0)
        return icmp_fd;
    icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_fd < 0)
        return icmp_fd;

    /* Receive timeout arms the netd RECV deadline sweep. Without it the
     * recvfrom() task stays armed forever on a silent socket, and
     * enet_ewok_stop()'s pthread_join() of icmp_recv_thread wedges the
     * whole shutdown (the process never exits and every netd worker
     * parked on our sockets leaks). Mirrors the UDP session cadence.  */
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(icmp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return icmp_fd;
}

/* Receiver thread for ICMP: reads from the EwokOS RAW socket and
 * wraps responses into Ethernet frames for the guest.               */
static void *icmp_recv_func(void *arg)
{
    uint8_t buf[1500];
    (void)arg;

    while (net_active) {
        struct sockaddr_in from;
        uint32_t from_len = sizeof(from);
        int n, icmp_len, ip_total;
        uint8_t ip_pkt[1500];

        memset(&from, 0, sizeof(from));
        n = recvfrom(icmp_fd, buf, sizeof(buf), 0,
                 (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            /* SO_RCVTIMEO expiries are the poll cadence, not errors:
             * re-check net_active immediately so enet_ewok_stop()'s join
             * completes promptly. Real errors back off briefly.         */
            if (n < 0 && errno != EWOK_SOCK_EAGAIN &&
                errno != EWOK_SOCK_ETIMEDOUT)
                usleep(10000);  /* 10ms back-off on error */
            continue;
        }

        /* The RAW socket gives us the ICMP payload (starting at
         * the ICMP header).  We need to wrap it in an IP header
         * and then an Ethernet frame.                             */
        icmp_len = n;
        ip_total = 20 + icmp_len;  /* IP header + ICMP */

        memset(ip_pkt, 0, 20);
        ip_pkt[0] = 0x45;                          /* ver=4, ihl=5 */
        ip_pkt[2] = (uint8_t)(ip_total >> 8);
        ip_pkt[3] = (uint8_t)(ip_total);
        ip_pkt[8] = 64;                            /* TTL          */
        ip_pkt[9] = IP_PROTO_ICMP;
        /* src = remote host, dst = guest */
        wr32(ip_pkt + 12, ntohl(from.sin_addr.s_un.s_addr));
        wr32(ip_pkt + 16, guest_ip);
        wr16(ip_pkt + 10, ip_checksum(ip_pkt, 20));
        memcpy(ip_pkt + 20, buf, icmp_len);

        enqueue_to_guest(ip_pkt, ip_total);
    }
    return NULL;
}

/* Forward an ICMP Echo Request from the guest to the real network */
static void forward_icmp(const uint8_t *ip_pkt, int ip_len)
{
    int ihl, icmp_len, fd;
    const uint8_t *icmp_data;
    uint32_t dst_ip;
    struct sockaddr_in dst;

    if (ip_len < 20)
        return;

    ihl = (ip_pkt[0] & 0x0f) * 4;
    if (ihl < 20 || ihl > ip_len)
        return;

    fd = ensure_icmp_fd();
    if (fd < 0)
        return;

    icmp_data = ip_pkt + ihl;
    icmp_len = ip_len - ihl;
    if (icmp_len < 8)  /* minimum ICMP Echo */
        return;

    dst_ip = rd32(ip_pkt + 16);  /* dst IP from IP hdr */

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

static struct udp_session *find_udp_session(uint16_t guest_port,
                        uint32_t remote_ip,
                        uint16_t remote_port)
{
    struct udp_session *s;
    for (s = udp_list; s; s = s->next) {
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
    struct udp_session *s = (struct udp_session *)arg;
    uint8_t buf[1500];

    while (net_active && s->active) {
        struct sockaddr_in from;
        uint32_t from_len = sizeof(from);
        int n, udp_len, ip_total;
        uint8_t ip_pkt[1500];
        uint8_t *udp;

        memset(&from, 0, sizeof(from));
        n = recvfrom(s->ewok_fd, buf, sizeof(buf), 0,
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
                int fd;
                pthread_mutex_lock(&udp_mutex);
                s->active = 0;
                fd = s->ewok_fd;
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
        udp_len = 8 + n;
        ip_total = 20 + udp_len;

        memset(ip_pkt, 0, 20);
        ip_pkt[0] = 0x45;
        ip_pkt[2] = (uint8_t)(ip_total >> 8);
        ip_pkt[3] = (uint8_t)(ip_total);
        ip_pkt[8] = 64;
        ip_pkt[9] = IP_PROTO_UDP;
        wr32(ip_pkt + 12, ntohl(from.sin_addr.s_un.s_addr));  /* src = remote */
        wr32(ip_pkt + 16, guest_ip);                          /* dst = guest  */
        wr16(ip_pkt + 10, ip_checksum(ip_pkt, 20));

        /* UDP header */
        udp = ip_pkt + 20;
        wr16(udp + 0, s->remote_port);     /* src port = remote  */
        wr16(udp + 2, s->guest_port);      /* dst port = guest   */
        wr16(udp + 4, (uint16_t)udp_len);  /* length             */
        wr16(udp + 6, 0);                  /* checksum (filled below) */
        memcpy(udp + 8, buf, n);
        wr16(udp + 6, udp_checksum(ip_pkt, ip_total));

        enqueue_to_guest(ip_pkt, ip_total);
    }
    s->thread_done = 1;
    return NULL;
}

/* Reap finished UDP sessions.  Caller holds udp_mutex.  thread_done is
 * published only after the receiver has dropped every lock, so the join
 * cannot deadlock. */
static void reap_udp_locked(void)
{
    struct udp_session **pp = &udp_list;
    while (*pp) {
        struct udp_session *s = *pp;
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
static struct udp_session *create_udp_session_locked(uint16_t guest_port,
                          uint32_t remote_ip,
                          uint16_t remote_port)
{
    struct udp_session *s;
    struct timeval tv;

    reap_udp_locked();

    s = (struct udp_session *)calloc(1, sizeof(struct udp_session));
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
    s->active      = 1;
    s->last_rx     = now_usec();

    /* Receive timeout sets the re-arm cadence only. It must NOT be
     * commensurate with the guest's DNS retransmit period (~1s): a
     * phase-locked timeout completion collides with the arriving reply,
     * and replies that land while recvfrom is momentarily unarmed are
     * stranded. 300ms keeps recvfrom armed ~99% of the time. */
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(s->ewok_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    pthread_create(&s->recv_thread, NULL, udp_recv_func, s);

    s->next = udp_list;
    udp_list = s;

    return s;
}

/* ------------------------------------------------------------------ */
/*  BOOTP / DHCP server - guest auto-config                           */
/* ------------------------------------------------------------------ */

/* Minimal stateless BOOTP/DHCP server for the guest: NeXTSTEP's "BOOTP"
 * TCP/IP setting gets the fixed NAT topology (IP / netmask / router /
 * DNS) from here instead of needing manual configuration -- the same
 * idea as qemu's slirp.  There is only one guest, so a single virtual
 * lease exists and every request is ACKed without lease bookkeeping.
 * A request carrying the RFC1048 magic cookie with a DHCP message-type
 * option gets a DHCP reply; a plain RFC951 BOOTP request gets a plain
 * BOOTREPLY.  bootp points at the UDP payload, plen at its length.    */
static void handle_bootp(const uint8_t *bootp, int plen)
{
    uint32_t xid, ciaddr, lease;
    const uint8_t *chaddr;
    uint8_t msg_type = 0, reply_type = 0;
    int has_cookie, inform, oi, o;
    uint8_t rp[300];
    int udp_len, ip_total;
    uint8_t ip_pkt[340];
    uint8_t *udp;
    static const uint8_t bcast_mac[ETH_ALEN] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t eth[1514];

    if (plen < 240 || bootp[0] != BOOTP_REQUEST)
        return;

    xid = rd32(bootp + 4);
    ciaddr = rd32(bootp + 12);
    chaddr = bootp + 28;

    /* RFC951 BOOTP requests predate the magic cookie; DHCP messages
     * always carry it, with the message type in option 53.           */
    has_cookie = (rd32(bootp + 236) == DHCP_MAGIC);
    if (has_cookie) {
        oi = 240;
        while (oi + 2 <= plen) {
            uint8_t code = bootp[oi];
            int olen;
            if (code == 255)
                break;
            if (code == 0) {   /* pad option */
                oi++;
                continue;
            }
            olen = bootp[oi + 1];
            if (oi + 2 + olen > plen)
                break;
            if (code == 53 && olen >= 1)
                msg_type = bootp[oi + 2];
            oi += 2 + olen;
        }

        switch (msg_type) {
        case DHCP_DISCOVER:
            reply_type = DHCP_OFFER;
            break;
        case DHCP_REQUEST:
        case DHCP_INFORM:
            reply_type = DHCP_ACK;
            break;
        default:
            return;   /* RELEASE / DECLINE: nothing to do */
        }
    }
    inform = (has_cookie && msg_type == DHCP_INFORM);

    /* Honor a client that already knows its address (ciaddr), else
     * assign the fixed NAT lease; either way adopt it so our replies
     * are addressed where the guest actually listens.                 */
    lease = ciaddr ? ciaddr : GUEST_IP_DEFAULT;
    guest_ip = lease;

    /* BOOTREPLY carrying the virtual lease */
    memset(rp, 0, sizeof(rp));
    rp[0] = BOOTP_REPLY;
    rp[1] = 1;           /* htype: Ethernet */
    rp[2] = ETH_ALEN;    /* hlen            */
    wr32(rp + 4, xid);
    if (!inform)
        wr32(rp + 16, lease);               /* yiaddr */
    wr32(rp + 20, GW_IP);                   /* siaddr = server */
    memcpy(rp + 28, chaddr, 16);
    wr32(rp + 236, DHCP_MAGIC);

    o = 240;
    if (has_cookie) {
        rp[o++] = 53; rp[o++] = 1; rp[o++] = reply_type;    /* msg type  */
    }
    rp[o++] = 54; rp[o++] = 4; wr32(rp + o, GW_IP); o += 4; /* server id */
    if (!inform) {
        rp[o++] = 51; rp[o++] = 4;                          /* lease     */
        wr32(rp + o, DHCP_LEASE_SEC); o += 4;
    }
    rp[o++] = 1;  rp[o++] = 4; wr32(rp + o, NET_MASK); o += 4;
    rp[o++] = 3;  rp[o++] = 4; wr32(rp + o, GW_IP); o += 4; /* router    */
    rp[o++] = 6;  rp[o++] = 8;                              /* DNS       */
    wr32(rp + o, dns_servers[0]); o += 4;
    wr32(rp + o, dns_servers[1]); o += 4;
    rp[o++] = 255;

    /* UDP server(67) -> client(68) to the IP broadcast address: the
     * client has no address of its own yet, so neither the IP nor the
     * Ethernet destination can be unicast.                          */
    udp_len = 8 + o;
    ip_total = 20 + udp_len;
    memset(ip_pkt, 0, 20);
    ip_pkt[0] = 0x45;
    ip_pkt[2] = (uint8_t)(ip_total >> 8);
    ip_pkt[3] = (uint8_t)(ip_total);
    ip_pkt[8] = 64;
    ip_pkt[9] = IP_PROTO_UDP;
    wr32(ip_pkt + 12, GW_IP);
    wr32(ip_pkt + 16, 0xFFFFFFFF);
    wr16(ip_pkt + 10, ip_checksum(ip_pkt, 20));
    udp = ip_pkt + 20;
    wr16(udp + 0, BOOTP_SERVER_PORT);
    wr16(udp + 2, BOOTP_CLIENT_PORT);
    wr16(udp + 4, (uint16_t)udp_len);
    wr16(udp + 6, 0);
    memcpy(udp + 8, rp, o);
    wr16(udp + 6, udp_checksum(ip_pkt, ip_total));

    build_eth_frame(eth, bcast_mac, gateway_mac, ETH_P_IP, ip_pkt, ip_total);
    enqueue_rx(eth, ETH_HLEN + ip_total);
}

static void forward_udp(const uint8_t *ip_pkt, int ip_len)
{
    int ihl, payload_len;
    const uint8_t *udp, *payload;
    uint16_t src_port, dst_port, udp_len;
    uint32_t dst_ip;
    struct sockaddr_in dst;
    struct udp_session *s;

    if (ip_len < 20)
        return;
    ihl = (ip_pkt[0] & 0x0f) * 4;
    if (ihl < 20 || ihl + 8 > ip_len)
        return;

    udp = ip_pkt + ihl;
    src_port = rd16(udp + 0);
    dst_port = rd16(udp + 2);
    udp_len  = rd16(udp + 4);
    dst_ip   = rd32(ip_pkt + 16);

    payload_len = udp_len - 8;
    if (payload_len < 0 || ihl + udp_len > ip_len)
        return;
    payload = udp + 8;

    /* Guest BOOTP/DHCP client traffic (dst port 67) is answered by our
     * virtual NAT server above -- relaying the broadcast onto the real
     * network would leak the private 10.0.0.x address space and get no
     * useful reply anyway.                                            */
    if (dst_port == BOOTP_SERVER_PORT) {
        handle_bootp(payload, payload_len);
        return;
    }

    /* Find or create a UDP session, and send, all under udp_mutex so
     * the idle reaper can never free a session while it is in use.   */
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_un.s_addr = htonl(dst_ip);
    dst.sin_port = htons(dst_port);

    pthread_mutex_lock(&udp_mutex);
    s = find_udp_session(src_port, dst_ip, dst_port);
    if (!s)
        s = create_udp_session_locked(src_port, dst_ip, dst_port);
    if (s && s->ewok_fd >= 0)
        sendto(s->ewok_fd, payload, payload_len, 0,
               (struct sockaddr *)&dst, sizeof(dst));
    pthread_mutex_unlock(&udp_mutex);
}

/* ------------------------------------------------------------------ */
/*  TCP proxy                                                         */
/* ------------------------------------------------------------------ */

static struct tcp_session *find_tcp_session(uint16_t guest_port, uint32_t remote_ip,
                        uint16_t remote_port)
{
    struct tcp_session *t;
    for (t = tcp_list; t; t = t->next)
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

/* Build a TCP segment remote_ip:remote_port -> guest and queue it */
static void emit_tcp(struct tcp_session *t, uint8_t flags, const uint8_t *payload,
             int plen, uint32_t seq)
{
    uint8_t seg[1500];
    int hlen = 20;
    int ip_total;
    uint8_t ip_pkt[1500];

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
    wr16(seg + 16, tcp_checksum(t->remote_ip, guest_ip, seg, hlen + plen));

    ip_total = 20 + hlen + plen;
    memset(ip_pkt, 0, 20);
    ip_pkt[0] = 0x45;
    ip_pkt[2] = (uint8_t)(ip_total >> 8);
    ip_pkt[3] = (uint8_t)(ip_total);
    ip_pkt[8] = 64;
    ip_pkt[9] = IP_PROTO_TCP;
    wr32(ip_pkt + 12, t->remote_ip);
    wr32(ip_pkt + 16, guest_ip);
    wr16(ip_pkt + 10, ip_checksum(ip_pkt, 20));
    memcpy(ip_pkt + 20, seg, hlen + plen);

    enqueue_to_guest(ip_pkt, ip_total);
}

/* Sender: push queued bytes / retransmit / FIN (lock held) */
static void tcp_pump_locked(struct tcp_session *t)
{
    if (!t->connected)
        return;
    {
        int unacked = t->s_nxt_off - t->s_una_off;
        int queued  = t->s_tail_off - t->s_nxt_off;

        if (unacked == 0 && queued > 0) {
            int len = queued > TCP_MSS ? TCP_MSS : queued;
            emit_tcp(t, TH_ACK | TH_PSH, t->sbuf + t->s_nxt_off, len, t->snd_nxt);
            t->snd_nxt += len;
            t->s_nxt_off += len;
            t->last_send = now_usec();
        } else if (unacked > 0) {
            uint64_t now = now_usec();
            if (now - t->last_send > TCP_RETRANS_USEC) {
                emit_tcp(t, TH_ACK | TH_PSH, t->sbuf + t->s_una_off,
                     unacked, t->snd_una);
                t->last_send = now;
            }
        } else if (t->fin_pending && !t->fin_sent) {
            emit_tcp(t, TH_FIN | TH_ACK, NULL, 0, t->snd_nxt);
            t->snd_nxt++;
            t->fin_sent = 1;
        }
    }
}

static void *tcp_recv_func(void *arg)
{
    struct tcp_session *t = (struct tcp_session *)arg;
    struct sockaddr_in sa;
    struct timeval tv;
    int cr, fd, boot_len;
    uint8_t boot_buf[2048];
    uint8_t buf[2048];

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_un.s_addr = htonl(t->remote_ip);
    sa.sin_port = htons(t->remote_port);

    /* Short timeout so the pump loop re-runs promptly after guest ACKs */
    tv.tv_sec = 0; tv.tv_usec = 10000;
    setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    cr = connect(t->fd, (struct sockaddr *)&sa, sizeof(sa));
    pthread_mutex_lock(&t->lock);
    if (cr < 0) {
        emit_tcp(t, TH_RST, NULL, 0, t->snd_nxt);
        t->active = 0;
        fd = t->fd;
        t->fd = -1;
        pthread_mutex_unlock(&t->lock);
        if (fd >= 0)
            close(fd);
        t->thread_done = 1;
        return NULL;
    }
    t->connected = 1;
    /* Guest data queued before connect() finished (e.g. the HTTP GET in
     * the ACK that completed the guest-side handshake): forward it now.
     * write() happens with the lock dropped; it travels through netd's
     * write_state worker and may take a while.                        */
    boot_len = t->s_tail_off - t->s_una_off;
    if (boot_len > 0 && boot_len <= (int)sizeof(boot_buf))
        memcpy(boot_buf, t->sbuf + t->s_una_off, boot_len);
    fd = t->fd;
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
        t->thread_done = 1;
        return NULL;
    }
    pthread_mutex_unlock(&t->lock);

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
            int drained = (t->s_nxt_off - t->s_una_off) == 0 &&
                      (t->s_tail_off - t->s_nxt_off) == 0;
            if (drained ||
                now_usec() - t->close_start > TCP_CLOSE_GRACE_USEC) {
                fd = t->fd;
                t->fd = -1;
                t->active = 0;
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

        {
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
                t->remote_eof = 1;
                t->fin_pending = 1;
                if (!t->close_requested) {
                    t->close_requested = 1;
                    t->close_start = now_usec();
                }
                tcp_pump_locked(t);
                pthread_mutex_unlock(&t->lock);
            } else {
                /* SO_RCVTIMEO expiry surfaces as ETIMEDOUT; treat like EAGAIN. */
                if (rerr != EWOK_SOCK_EAGAIN && rerr != EWOK_SOCK_ETIMEDOUT) {
                    pthread_mutex_lock(&t->lock);
                    t->active = 0;
                    pthread_mutex_unlock(&t->lock);
                }
            }
        }
    }
    t->thread_done = 1;
    return NULL;
}

/* Reap finished TCP sessions.  Caller holds tcp_mutex.  thread_done is
 * published only after the receiver has dropped every lock; taking t->lock
 * here just waits out a forward_tcp that locked the session before it was
 * unlinked, so the join cannot deadlock.  Every other path reaches the
 * session only through find_tcp_session(), which skips inactive ones. */
static void reap_tcp_locked(void)
{
    struct tcp_session **pp = &tcp_list;
    while (*pp) {
        struct tcp_session *t = *pp;
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

static struct tcp_session *create_tcp_session(uint16_t guest_port, uint32_t remote_ip,
                       uint16_t remote_port, uint32_t guest_isn)
{
    struct tcp_session *t = (struct tcp_session *)calloc(1, sizeof(struct tcp_session));
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
    t->active      = 1;
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
    int ihl, th_off, plen;
    const uint8_t *tcp, *payload;
    uint16_t src_port, dst_port;
    uint32_t seq, ack, dst_ip;
    uint8_t flags;
    struct tcp_session *t;
    int wr_len = 0;
    uint8_t wr_buf[1520];

    if (ip_len < 20)
        return;
    ihl = (ip_pkt[0] & 0x0f) * 4;
    if (ihl < 20 || ihl + 20 > ip_len)
        return;

    tcp = ip_pkt + ihl;
    src_port = rd16(tcp + 0);
    dst_port = rd16(tcp + 2);
    seq      = rd32(tcp + 4);
    ack      = rd32(tcp + 8);
    th_off   = ((tcp[12] >> 4) & 0x0f) * 4;
    flags    = tcp[13];
    dst_ip   = rd32(ip_pkt + 16);
    plen     = ip_len - ihl - th_off;
    payload  = tcp + th_off;
    if (plen < 0) plen = 0;

    /* New connection request */
    if ((flags & TH_SYN) && !(flags & TH_ACK)) {
        int exists;
        pthread_mutex_lock(&tcp_mutex);
        exists = (find_tcp_session(src_port, dst_ip, dst_port) != NULL);
        pthread_mutex_unlock(&tcp_mutex);
        if (!exists)
            create_tcp_session(src_port, dst_ip, dst_port, seq);
        return;
    }

    /* Lookup under tcp_mutex so the session reaper can never free a
     * session between the find and the session-lock acquisition.     */
    pthread_mutex_lock(&tcp_mutex);
    t = find_tcp_session(src_port, dst_ip, dst_port);
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
        t->active = 0;
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
        t->fin_pending = 1;
        if (!t->close_requested) {
            t->close_requested = 1;
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
    uint32_t src_ip;

    if (ip_len < 20)
        return;

    /* Track the guest's source address so replies reach statically
     * configured guests too (the BOOTP/DHCP lease is only a default) */
    src_ip = rd32(ip_pkt + 12);
    if (src_ip != 0 && src_ip != 0xFFFFFFFF)
        guest_ip = src_ip;

    switch (ip_pkt[9]) {
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
/*  Backend interface (called from ethernet.c, main emulation thread) */
/* ------------------------------------------------------------------ */

/* Transmit path: a raw Ethernet frame from the guest */
void enet_ewok_input(Uint8 *pkt, int pkt_len)
{
    uint16_t ethertype;

    if (!net_started)
        return;
    if (pkt_len < ETH_HLEN)
        return;

    /* Learn the guest MAC from its own transmissions (libslirp-style);
     * replies are addressed to it once known.                          */
    memcpy(guest_mac, pkt + ETH_ALEN, ETH_ALEN);
    guest_mac_known = 1;

    ethertype = rd16(pkt + 12);

    switch (ethertype) {
    case ETH_P_ARP:
        handle_arp(pkt, pkt_len);
        break;
    case ETH_P_IP:
        /* The frame may be padded to the 60-byte minimum (plus CRC);
         * trust the IP total length over the raw frame length.        */
        {
            int ip_len = pkt_len - ETH_HLEN;
            int ip_total;
            if (ip_len < 20)
                return;
            ip_total = rd16(pkt + ETH_HLEN + 2);
            if (ip_total >= 20 && ip_total < ip_len)
                ip_len = ip_total;
            handle_ip_packet(pkt + ETH_HLEN, ip_len);
        }
        break;
    default:
        break;
    }
}

/* Periodic poll (ENET_IO_Handler, RX buffer empty): inject one queued
 * inbound frame into the emulated controller.                          */
void enet_ewok_queue_poll(void)
{
    struct rx_frame *f;

    if (!net_started)
        return;

    pthread_mutex_lock(&rx_mutex);
    f = rx_head;
    if (f) {
        rx_head = f->next;
        if (!rx_head)
            rx_tail = NULL;
        rx_count--;
    }
    pthread_mutex_unlock(&rx_mutex);

    if (f) {
        enet_receive(f->data, f->len);
        free(f);
    }
}

void enet_ewok_start(void)
{
    if (!enet_inited) {
        pthread_mutex_init(&rx_mutex, NULL);
        pthread_mutex_init(&udp_mutex, NULL);
        pthread_mutex_init(&tcp_mutex, NULL);
        enet_inited = 1;
    }
    if (net_started)
        return;

    rx_head = rx_tail = NULL;
    rx_count = 0;
    udp_list = NULL;
    tcp_list = NULL;
    icmp_fd = -1;
    guest_ip = GUEST_IP_DEFAULT;
    net_active = 1;

    /* Pre-open the ICMP socket and start its receiver */
    if (ensure_icmp_fd() >= 0)
        pthread_create(&icmp_recv_thread, NULL, icmp_recv_func, NULL);

    net_started = 1;
    Log_Printf(LOG_WARN, "[EN] EwokOS socket NAT started (guest 10.0.0.2, gateway 10.0.0.1)");
}

void enet_ewok_stop(void)
{
    struct udp_session *s;
    struct tcp_session *t;
    int fd;

    if (!net_started)
        return;
    net_active = 0;

    /* Wait for the ICMP thread */
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
    for (s = udp_list; s; s = s->next)
        s->active = 0;
    s = udp_list;
    udp_list = NULL;
    pthread_mutex_unlock(&udp_mutex);
    while (s) {
        struct udp_session *next = s->next;
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
    t = tcp_list;
    tcp_list = NULL;
    while (t) {
        struct tcp_session *next = t->next;
        pthread_mutex_lock(&t->lock);
        t->active = 0;
        fd = t->fd;
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
        struct rx_frame *f = rx_head;
        rx_head = f->next;
        free(f);
    }
    rx_tail = NULL;
    rx_count = 0;
    pthread_mutex_unlock(&rx_mutex);

    net_started = 0;
    Log_Printf(LOG_WARN, "[EN] EwokOS socket NAT stopped");
}

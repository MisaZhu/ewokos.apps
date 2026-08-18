/*
 *  audio_pcm.cpp - Audio support, EwokOS PCM device implementation
 *
 *  Modeled after audio_sdl.cpp, but the feeder is a pthread that
 *  writes fixed periods into the EwokOS PCM device (/dev/sound0)
 *  like minivmac does.  The Mac side delivers big-endian 16-bit
 *  samples, which are swapped to host order before writing.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <ewoksys/proto.h>
#include <ewoksys/vdevice.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/proc.h>

#define DEBUG 0
#include "debug.h"

#define MAC_MAX_VOLUME 0x0100

/*
 *  PCM device support (same interface as the sound driver used by
 *  minivmac: CTRL_PCM_DEV_HW configures, then plain write() streams)
 */

#define CTRL_PCM_DEV_HW			(0xF0)
#define CTRL_PCM_DEV_HW_FREE	(0xF1)
#define CTRL_PCM_DEV_PRPARE		(0xF2)

#define PCM_EPIPE				(32)

struct pcm_config {
	int bit_depth;
	int rate;
	int channels;
	int period_size;
	int period_count;
	int start_threshold;
	int stop_threshold;
};

struct pcm {
	int fd;
	int prepared;
	int running;
	char name[32];
	int framesize;
	struct pcm_config config;
};

static int is_valid_config(struct pcm_config *config)
{
	if (config->bit_depth != 16 || config->channels != 2)
		return 0;
	switch (config->rate) {
	case 8000: case 16000: case 32000:
	case 44100: case 48000: case 96000:
		break;
	default:
		return 0;
	}
	if (config->period_size == 0 || config->period_count == 0)
		return 0;
	if (config->start_threshold == 0)
		config->start_threshold = config->period_size;
	if (config->stop_threshold == 0)
		config->stop_threshold = config->period_size * config->period_count;
	return 1;
}

static int pcm_prepare(struct pcm *pcm)
{
	if (pcm->prepared)
		return 0;

	proto_t in, out;
	PF->init(&in);
	PF->init(&out);
	int ret = dev_cntl(pcm->name, CTRL_PCM_DEV_PRPARE, &in, &out);
	if (ret == 0)
		ret = proto_read_int(&out);
	PF->clear(&in);
	PF->clear(&out);

	if (ret == 0)
		pcm->prepared = 1;
	return ret;
}

static int pcm_try_write(struct pcm *pcm, const void *data, unsigned int count)
{
	if (count == 0)
		return 0;

	if (pcm->running == 0) {
		int err = pcm_prepare(pcm);
		if (err != 0)
			return err;
		int written = write(pcm->fd, data, count);
		if (written > 0)
			pcm->running = 1;
		return written;
	}
	return write(pcm->fd, data, count);
}

/*
 * Blocking period-sized writes: when the driver ring is full write()
 * sleeps in vfsd until the playback loop drains a period, so this
 * loop paces the feeder thread on the device clock.
 */
static int pcm_write(struct pcm *pcm, const void *data, unsigned int count)
{
	if (count == 0)
		return 0;

	int period_bytes = pcm->config.period_size * pcm->framesize;
	int bytes = (int)count;
	int written = 0;
	int offset = 0;
	int xrun_retry = 0;

	while (bytes > 0) {
		int copy_bytes = bytes < period_bytes ? bytes : period_bytes;
		int ret = pcm_try_write(pcm, (const char *)data + offset, copy_bytes);
		if (ret == -PCM_EPIPE) {
			if (xrun_retry++ >= 5)
				break;
			pcm->prepared = 0;
			pcm->running = 0;
			if (pcm_prepare(pcm) != 0)
				proc_usleep(100);
			continue;
		}
		if (ret <= 0)
			break;
		xrun_retry = 0;
		offset += ret;
		written += ret;
		bytes -= ret;
	}
	return written;
}

static struct pcm *pcm_open(const char *name, struct pcm_config *config)
{
	if (!is_valid_config(config))
		return NULL;

	struct pcm *pcm = (struct pcm *)calloc(1, sizeof(struct pcm));
	if (pcm == NULL)
		return NULL;

	strncpy(pcm->name, name, 31);
	memcpy(&pcm->config, config, sizeof(struct pcm_config));
	pcm->framesize = config->channels * config->bit_depth / 8;

	pcm->fd = open(name, O_RDWR);
	if (pcm->fd < 0) {
		free(pcm);
		return NULL;
	}

	proto_t in, out;
	PF->init(&in)->add(&in, config, sizeof(struct pcm_config));
	PF->init(&out);
	int temp = dev_cntl(pcm->name, CTRL_PCM_DEV_HW, &in, &out);
	if (temp == 0)
		temp = proto_read_int(&out);
	PF->clear(&in);
	PF->clear(&out);

	if (temp != 0) {
		close(pcm->fd);
		free(pcm);
		return NULL;
	}
	return pcm;
}

static void pcm_close(struct pcm *pcm)
{
	if (pcm == NULL)
		return;
	close(pcm->fd);
	free(pcm);
}

/*
 *  State
 */

// The PCM driver only handles 16-bit stereo at a fixed set of rates,
// so that is all we advertise to the MacOS sound manager
static struct pcm *sound_pcm = NULL;
static uint8 *audio_mix_buf = NULL;		// Raw Mac data (big-endian 16-bit)
static uint8 *audio_out_buf = NULL;		// Host-order data for the device
static volatile bool feeder_running = false;
static volatile bool irq_ack = false;
static pthread_t feeder_thread = 0;
static int audio_volume = MAC_MAX_VOLUME;
static bool audio_mute = false;

static void *feeder_func(void *arg);

/*
 *  Initialization
 */

// Set AudioStatus to reflect current audio stream format
static void set_audio_status_format(void)
{
	AudioStatus.sample_rate = audio_sample_rates[0];
	AudioStatus.sample_size = audio_sample_sizes[0];
	AudioStatus.channels = audio_channel_counts[0];
}

static bool open_pcm_device(void)
{
	if (audio_sample_sizes.empty()) {
		audio_sample_rates.push_back(44100 << 16);
		audio_sample_sizes.push_back(16);
		audio_channel_counts.push_back(2);
	}

	struct pcm_config config;
	memset(&config, 0, sizeof(config));
	config.bit_depth = 16;
	config.rate = audio_sample_rates[0] >> 16;
	config.channels = 2;
	config.period_size = 1024;
	config.period_count = 4;
	config.start_threshold = 1024 * 2;
	config.stop_threshold = 0;

	// Try different sound device paths
	static const char *sound_devices[] = {
		"/dev/sound0", "/dev/sound", "/dev/pcm0", "/dev/pcm", NULL
	};
	for (int i = 0; sound_devices[i] != NULL; i++) {
		sound_pcm = pcm_open(sound_devices[i], &config);
		if (sound_pcm) {
			printf("Using EwokOS PCM audio output (%s)\n", sound_devices[i]);
			break;
		}
	}
	if (sound_pcm == NULL)
		return false;

	audio_frames_per_block = config.period_size;
	audio_mix_buf = (uint8 *)malloc(audio_frames_per_block * 4);
	audio_out_buf = (uint8 *)malloc(audio_frames_per_block * 4);
	return audio_mix_buf != NULL && audio_out_buf != NULL;
}

static bool open_audio(void)
{
	if (!open_pcm_device()) {
		WarningAlert(GetString(STR_NO_AUDIO_WARN));
		return false;
	}
	set_audio_status_format();

	// Start the feeder thread
	irq_ack = false;
	feeder_running = true;
	if (pthread_create(&feeder_thread, NULL, feeder_func, NULL) != 0) {
		feeder_running = false;
		return false;
	}

	audio_open = true;
	return true;
}

void AudioInit(void)
{
	// Init audio status and feature flags
	AudioStatus.sample_rate = 44100 << 16;
	AudioStatus.sample_size = 16;
	AudioStatus.channels = 2;
	AudioStatus.mixer = 0;
	AudioStatus.num_sources = 0;
	audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

	// Sound disabled in prefs? Then do nothing
	if (PrefsFindBool("nosound"))
		return;

	open_audio();
}

/*
 *  Deinitialization
 */

static void close_audio(void)
{
	if (feeder_running) {
		feeder_running = false;
		if (feeder_thread != 0) {
			pthread_join(feeder_thread, NULL);
			feeder_thread = 0;
		}
	}
	pcm_close(sound_pcm);
	sound_pcm = NULL;
	free(audio_mix_buf);
	audio_mix_buf = NULL;
	free(audio_out_buf);
	audio_out_buf = NULL;
	audio_open = false;
}

void AudioExit(void)
{
	close_audio();
}

/*
 *  First source added, start audio stream
 */

void audio_enter_stream()
{
}

/*
 *  Last source removed, stop audio stream
 */

void audio_exit_stream()
{
}

/*
 *  Streaming function (feeder thread)
 */

static void *feeder_func(void *arg)
{
	(void)arg;

	int block_bytes = audio_frames_per_block * 4;	// 16-bit stereo

	while (feeder_running) {
		if (!AudioStatus.num_sources) {
			// Audio not active, let the device idle
			proc_usleep(20 * 1000);
			continue;
		}

		// Trigger audio interrupt to get new buffer
		D(bug("stream: triggering irq\n"));
		irq_ack = false;
		SetInterruptFlag(INTFLAG_AUDIO);
		TriggerInterrupt();

		// Wait for AudioInterrupt() to deliver the data (poll, with a
		// timeout so shutdown can't wedge the thread)
		uint64_t t0 = kernel_tic_ms(0);
		while (feeder_running && !irq_ack) {
			if (kernel_tic_ms(0) - t0 > 200)
				break;
			proc_usleep(500);
		}
		if (!feeder_running)
			break;
		D(bug("stream: ack received\n"));

		// Get size of audio data
		int out_bytes = block_bytes;
		uint32 apple_stream_info = ReadMacInt32(audio_data + adatStreamInfo);
		if (apple_stream_info && !audio_mute) {
			int work_size = ReadMacInt32(apple_stream_info + scd_sampleCount)
				* (AudioStatus.sample_size >> 3) * AudioStatus.channels;
			D(bug("stream: work_size %d\n", work_size));
			if (work_size > block_bytes)
				work_size = block_bytes;
			if (work_size <= 0)
				goto silence;

			Mac2Host_memcpy(audio_mix_buf, ReadMacInt32(apple_stream_info + scd_buffer), work_size);

			// Big-endian 16-bit Mac samples -> host order, apply volume
			const uint8 *src = audio_mix_buf;
			uint16 *dst = (uint16 *)audio_out_buf;
			int samples = work_size / 2;
			for (int i = 0; i < samples; i++) {
				int16 s = (int16)((src[i * 2] << 8) | src[i * 2 + 1]);
				s = (int16)(s * audio_volume / MAC_MAX_VOLUME);
				dst[i] = (uint16)s;
			}
			out_bytes = samples * 2;
			D(bug("stream: data written\n"));
			pcm_write(sound_pcm, audio_out_buf, out_bytes);
		} else {
			// No data or muted: keep the device paced with silence
silence:
			memset(audio_out_buf, 0, block_bytes);
			pcm_write(sound_pcm, audio_out_buf, block_bytes);
		}
	}
	return NULL;
}

/*
 *  MacOS audio interrupt, read next data block
 */

void AudioInterrupt(void)
{
	D(bug("AudioInterrupt\n"));

	// Get data from apple mixer
	if (AudioStatus.mixer) {
		M68kRegisters r;
		r.a[0] = audio_data + adatStreamInfo;
		r.a[1] = AudioStatus.mixer;
		Execute68k(audio_data + adatGetSourceData, &r);
		D(bug(" GetSourceData() returns %08lx\n", r.d[0]));
	} else
		WriteMacInt32(audio_data + adatStreamInfo, 0);

	// Signal feeder thread
	irq_ack = true;
	D(bug("AudioInterrupt done\n"));
}

/*
 *  Set sampling parameters
 *  "index" is an index into the audio_sample_rates[] etc. vectors
 *  It is guaranteed that AudioStatus.num_sources == 0
 */

bool audio_set_sample_rate(int index)
{
	(void)index;
	return audio_open;
}

bool audio_set_sample_size(int index)
{
	(void)index;
	return audio_open;
}

bool audio_set_channels(int index)
{
	(void)index;
	return audio_open;
}

/*
 *  Get/set volume controls (volume value 0x0100 = maximum)
 */

bool audio_get_main_mute(void)
{
	return audio_mute;
}

uint32 audio_get_main_volume(void)
{
	uint32 chan = audio_mute ? 0 : audio_volume;
	return (chan << 16) + chan;
}

bool audio_get_speaker_mute(void)
{
	return audio_mute;
}

uint32 audio_get_speaker_volume(void)
{
	return audio_get_main_volume();
}

void audio_set_main_mute(bool mute)
{
	audio_mute = mute;
}

void audio_set_main_volume(uint32 vol)
{
	uint32 avg = ((vol >> 16) + (vol & 0xffff)) / 2;
	if (avg > MAC_MAX_VOLUME)
		avg = MAC_MAX_VOLUME;
	audio_volume = avg;
}

void audio_set_speaker_mute(bool mute)
{
}

void audio_set_speaker_volume(uint32 vol)
{
}

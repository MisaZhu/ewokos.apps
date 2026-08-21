#include <WidgetVideo/WidgetVideo.h>
#include <Widget/RootWidget.h>
#include <Widget/WidgetWin.h>
#include <x++/XTheme.h>

#include <ewoksys/devcmd.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/keydef.h>
#include <ewoksys/proc.h>
#include <ewoksys/proto.h>

#include <fcntl.h>
#include <font/font.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace Ewok {

namespace {

static const char* PCM_DEVICES[] = {
	"/dev/sound0",
	"/dev/sound"
};

static const int VIDEO_PRESENT_LEAD_MIN_MS = 4;
static const int VIDEO_PRESENT_LEAD_MAX_MS = 40;
static const int VIDEO_PACKET_HIGH_WATER = 48;
static const int AUDIO_PACKET_HIGH_WATER = 96;
static const size_t AUDIO_QUEUE_CAPACITY = 256 * 1024;
static const size_t AUDIO_QUEUE_CHUNK = 8192;
static const int AUDIO_WAIT_SLEEP_MS = 1;
static const int AUDIO_QUEUE_WAIT_US = 10000;
static const int DEMUX_BACKPRESSURE_US = 5000;

typedef struct {
	WidgetVideo* owner;
	uint32_t present_serial;
	uint32_t painted_serial;
	int32_t last_paint_lag_ms;
	int64_t present_submit_ms;
} widget_video_present_state_t;

static pthread_mutex_t g_present_mutex;
static bool g_present_mutex_ready = false;
static widget_video_present_state_t g_present_state;

static void ensure_present_mutex_ready(void) {
	if(!g_present_mutex_ready) {
		pthread_mutex_init(&g_present_mutex, NULL);
		g_present_mutex_ready = true;
	}
}

typedef struct {
	int bit_depth;
	int rate;
	int channels;
	int period_size;
	int period_count;
	int start_threshold;
	int stop_threshold;
} widget_video_pcm_config_t;

typedef struct {
	int fd;
	int prepared;
	int running;
	char name[32];
	int frame_size;
	widget_video_pcm_config_t config;
} widget_video_pcm_t;

typedef struct {
	WidgetVideo* owner;
	int enabled;
	widget_video_pcm_t* pcm;
	struct SwrContext* swr;
	AVChannelLayout out_layout;
	enum AVSampleFormat out_fmt;
	int out_rate;
	int out_channels;
	pthread_t thread;
	pthread_mutex_t queue_mutex;
	bool queue_mutex_ready;
	bool queue_stop;
	bool queue_thread_running;
	uint8_t* queue_buf;
	size_t queue_capacity;
	size_t queue_start;
	size_t queue_size;
	uint8_t* convert_buf;
	int convert_buf_size;
} widget_video_audio_t;

static bool audio_should_stop(widget_video_audio_t* audio) {
	if(audio == NULL)
		return true;
	if(audio->queue_stop)
		return true;
	if(audio->owner != NULL && audio->owner->isStopRequested())
		return true;
	return false;
}

typedef struct {
	int started;
	int64_t start_ticks_ms;
	int64_t start_pts_ms;
} widget_video_clock_t;

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	AVPacket** packets;
	int capacity;
	int read_pos;
	int write_pos;
	int count;
	bool eof;
	bool abort;
} widget_video_packet_queue_t;

typedef struct {
	WidgetVideo* owner;
	widget_video_packet_queue_t video_queue;
	widget_video_packet_queue_t audio_queue;
	pthread_mutex_t state_mutex;
	bool state_mutex_ready;
	bool abort_requested;
} widget_video_pipeline_t;

typedef struct {
	widget_video_pipeline_t* pipeline;
	AVCodecContext* codec_ctx;
	AVStream* stream;
	widget_video_audio_t* audio;
	widget_video_clock_t clock;
	int delay_ms;
	pthread_t thread;
	bool thread_running;
	int err;
} widget_video_video_worker_t;

typedef struct {
	widget_video_pipeline_t* pipeline;
	AVCodecContext* codec_ctx;
	widget_video_audio_t* audio;
	pthread_t thread;
	bool thread_running;
	int err;
} widget_video_audio_worker_t;

static void* video_decode_thread_entry(void* p);
static void* audio_decode_thread_entry(void* p);

static int64_t now_ms(void) {
	uint64_t usec = 0;
	kernel_tic(NULL, &usec);
	return (int64_t)(usec / 1000ULL);
}

static int packet_queue_init(widget_video_packet_queue_t* queue, int capacity) {
	memset(queue, 0, sizeof(*queue));
	queue->packets = (AVPacket**)calloc((size_t)capacity, sizeof(AVPacket*));
	if(queue->packets == NULL)
		return AVERROR(ENOMEM);
	queue->capacity = capacity;
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_cond_init(&queue->cond, NULL);
	return 0;
}

static void packet_queue_abort(widget_video_packet_queue_t* queue) {
	pthread_mutex_lock(&queue->mutex);
	queue->abort = true;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
}

static void packet_queue_set_eof(widget_video_packet_queue_t* queue) {
	pthread_mutex_lock(&queue->mutex);
	queue->eof = true;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
}

static void packet_queue_destroy(widget_video_packet_queue_t* queue) {
	int i;

	packet_queue_abort(queue);
	pthread_mutex_lock(&queue->mutex);
	for(i = 0; i < queue->count; ++i) {
		int idx = (queue->read_pos + i) % queue->capacity;
		av_packet_free(&queue->packets[idx]);
	}
	free(queue->packets);
	queue->packets = NULL;
	queue->count = 0;
	pthread_mutex_unlock(&queue->mutex);
	pthread_cond_destroy(&queue->cond);
	pthread_mutex_destroy(&queue->mutex);
}

static void packet_queue_reset(widget_video_packet_queue_t* queue) {
	int i;

	pthread_mutex_lock(&queue->mutex);
	for(i = 0; i < queue->count; ++i) {
		int idx = (queue->read_pos + i) % queue->capacity;
		av_packet_free(&queue->packets[idx]);
		queue->packets[idx] = NULL;
	}
	queue->read_pos = 0;
	queue->write_pos = 0;
	queue->count = 0;
	queue->eof = false;
	queue->abort = false;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
}

static int packet_queue_push_clone(widget_video_packet_queue_t* queue, const AVPacket* packet) {
	AVPacket* clone = av_packet_clone(packet);
	if(clone == NULL)
		return AVERROR(ENOMEM);

	pthread_mutex_lock(&queue->mutex);
	while(!queue->abort && queue->count >= queue->capacity)
		pthread_cond_wait(&queue->cond, &queue->mutex);
	if(queue->abort) {
		pthread_mutex_unlock(&queue->mutex);
		av_packet_free(&clone);
		return AVERROR_EXIT;
	}
	queue->packets[queue->write_pos] = clone;
	queue->write_pos = (queue->write_pos + 1) % queue->capacity;
	queue->count++;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

static int packet_queue_pop(widget_video_packet_queue_t* queue, AVPacket** packet) {
	*packet = NULL;

	pthread_mutex_lock(&queue->mutex);
	while(!queue->abort && queue->count == 0 && !queue->eof)
		pthread_cond_wait(&queue->cond, &queue->mutex);
	if(queue->abort) {
		pthread_mutex_unlock(&queue->mutex);
		return AVERROR_EXIT;
	}
	if(queue->count == 0) {
		pthread_mutex_unlock(&queue->mutex);
		return AVERROR_EOF;
	}

	*packet = queue->packets[queue->read_pos];
	queue->packets[queue->read_pos] = NULL;
	queue->read_pos = (queue->read_pos + 1) % queue->capacity;
	queue->count--;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

static int packet_queue_count(widget_video_packet_queue_t* queue) {
	int count;

	pthread_mutex_lock(&queue->mutex);
	count = queue->count;
	pthread_mutex_unlock(&queue->mutex);
	return count;
}

static int pipeline_init(widget_video_pipeline_t* pipeline, WidgetVideo* owner) {
	int err;

	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->owner = owner;
	pthread_mutex_init(&pipeline->state_mutex, NULL);
	pipeline->state_mutex_ready = true;

	err = packet_queue_init(&pipeline->video_queue, 64);
	if(err < 0)
		return err;
	err = packet_queue_init(&pipeline->audio_queue, 128);
	if(err < 0) {
		packet_queue_destroy(&pipeline->video_queue);
		return err;
	}
	return 0;
}

static void pipeline_abort(widget_video_pipeline_t* pipeline) {
	if(pipeline == NULL)
		return;
	if(pipeline->state_mutex_ready) {
		pthread_mutex_lock(&pipeline->state_mutex);
		pipeline->abort_requested = true;
		pthread_mutex_unlock(&pipeline->state_mutex);
	}
	if(pipeline->video_queue.packets != NULL)
		packet_queue_abort(&pipeline->video_queue);
	if(pipeline->audio_queue.packets != NULL)
		packet_queue_abort(&pipeline->audio_queue);
}

static bool pipeline_is_aborted(widget_video_pipeline_t* pipeline) {
	bool aborted = false;

	if(pipeline == NULL || !pipeline->state_mutex_ready)
		return false;
	pthread_mutex_lock(&pipeline->state_mutex);
	aborted = pipeline->abort_requested;
	pthread_mutex_unlock(&pipeline->state_mutex);
	return aborted;
}

static void pipeline_destroy(widget_video_pipeline_t* pipeline) {
	if(pipeline == NULL)
		return;
	if(pipeline->video_queue.packets != NULL)
		packet_queue_destroy(&pipeline->video_queue);
	if(pipeline->audio_queue.packets != NULL)
		packet_queue_destroy(&pipeline->audio_queue);
	if(pipeline->state_mutex_ready)
		pthread_mutex_destroy(&pipeline->state_mutex);
	memset(pipeline, 0, sizeof(*pipeline));
}

static void pipeline_reset(widget_video_pipeline_t* pipeline) {
	if(pipeline == NULL)
		return;
	if(pipeline->state_mutex_ready) {
		pthread_mutex_lock(&pipeline->state_mutex);
		pipeline->abort_requested = false;
		pthread_mutex_unlock(&pipeline->state_mutex);
	}
	if(pipeline->video_queue.packets != NULL)
		packet_queue_reset(&pipeline->video_queue);
	if(pipeline->audio_queue.packets != NULL)
		packet_queue_reset(&pipeline->audio_queue);
}

static string ffmpeg_error_string(int err) {
	char errbuf[128];
	av_strerror(err, errbuf, sizeof(errbuf));
	return string(errbuf);
}

static void pcm_proto_clear(proto_t* in, proto_t* out) {
	PF->clear(in);
	PF->clear(out);
}

static int pcm_param_set(widget_video_pcm_t* pcm, widget_video_pcm_config_t* config) {
	proto_t in, out;
	int ret = 0;

	PF->init(&in)->add(&in, config, sizeof(widget_video_pcm_config_t));
	PF->init(&out);
	ret = dev_cntl(pcm->name, 0xF0, &in, &out);
	if(ret == 0)
		ret = proto_read_int(&out);
	pcm_proto_clear(&in, &out);
	return ret;
}

static int pcm_prepare(widget_video_pcm_t* pcm) {
	proto_t in, out;
	int ret = 0;

	if(pcm->prepared)
		return 0;

	PF->init(&in);
	PF->init(&out);
	ret = dev_cntl(pcm->name, 0xF2, &in, &out);
	if(ret == 0)
		ret = proto_read_int(&out);
	pcm_proto_clear(&in, &out);
	if(ret == 0)
		pcm->prepared = 1;
	return ret;
}

static int pcm_buf_avail(widget_video_pcm_t* pcm) {
	proto_t in, out;
	int ret = 0;

	PF->init(&in);
	PF->init(&out);
	ret = dev_cntl(pcm->name, 0xF3, &in, &out);
	if(ret == 0)
		ret = proto_read_int(&out);
	pcm_proto_clear(&in, &out);
	return ret;
}

static int pcm_wait_avail(widget_video_audio_t* audio, int* avail, int timeout_ms) {
	widget_video_pcm_t* pcm = audio->pcm;
	const int period_bytes = pcm->config.period_size * pcm->frame_size;
	int min_avail = period_bytes / 4;
	const int max_try_count = timeout_ms / AUDIO_WAIT_SLEEP_MS;
	int try_count = 0;
	int ret = 0;

	if(min_avail < pcm->frame_size)
		min_avail = pcm->frame_size;

	*avail = 0;
	for(;;) {
		if(audio_should_stop(audio))
			return -1;
		ret = pcm_buf_avail(pcm);
		if(ret < 0)
			return ret;
		if(ret >= min_avail) {
			*avail = ret;
			return ret;
		}
		if(try_count++ >= max_try_count)
			return ret;
		proc_usleep(AUDIO_WAIT_SLEEP_MS * 1000);
	}
}

static int pcm_write(widget_video_audio_t* audio, const void* data, unsigned int count) {
	widget_video_pcm_t* pcm = audio->pcm;
	const char* p = (const char*)data;
	int period_bytes = pcm->config.period_size * pcm->frame_size;
	int avail = 0;
	int bytes = (int)count;
	int written = 0;
	int offset = 0;

	if(count == 0)
		return 0;

	while(bytes > 0) {
		int chunk;
		int ret;

		if(audio_should_stop(audio))
			return -1;
		ret = pcm_wait_avail(audio, &avail, 2000);
		if(ret < 0 || avail == 0)
			break;

		if(!pcm->running) {
			ret = pcm_prepare(pcm);
			if(ret != 0)
				break;
		}

		chunk = bytes < avail ? bytes : avail;
		if(chunk > period_bytes)
			chunk = period_bytes;
		ret = write(pcm->fd, p + offset, chunk);
		if(ret != chunk)
			break;

		pcm->running = 1;
		offset += chunk;
		bytes -= chunk;
		written += chunk;
	}

	return (written == (int)count) ? 0 : -1;
}

static widget_video_pcm_t* pcm_open_device(const char* name, widget_video_pcm_config_t* config) {
	widget_video_pcm_t* pcm;

	if(config->bit_depth != 16 || config->channels != 2)
		return NULL;

	pcm = (widget_video_pcm_t*)calloc(1, sizeof(widget_video_pcm_t));
	if(pcm == NULL)
		return NULL;

	strncpy(pcm->name, name, sizeof(pcm->name)-1);
	memcpy(&pcm->config, config, sizeof(widget_video_pcm_config_t));
	pcm->frame_size = config->channels * config->bit_depth / 8;
	pcm->fd = open(name, O_RDWR);
	if(pcm->fd < 0) {
		free(pcm);
		return NULL;
	}

	if(pcm_param_set(pcm, &pcm->config) != 0) {
		close(pcm->fd);
		free(pcm);
		return NULL;
	}

	return pcm;
}

static void pcm_close_device(widget_video_pcm_t* pcm) {
	if(pcm == NULL)
		return;
	close(pcm->fd);
	free(pcm);
}

static int find_stream(AVFormatContext* fmt_ctx, enum AVMediaType type) {
	unsigned int i;
	for(i = 0; i < fmt_ctx->nb_streams; ++i) {
		if(fmt_ctx->streams[i]->codecpar->codec_type == type)
			return (int)i;
	}
	return -1;
}

static int choose_audio_rate(int sample_rate) {
	switch(sample_rate) {
	case 8000:
	case 16000:
	case 32000:
	case 44100:
	case 48000:
	case 96000:
		return sample_rate;
	default:
		return 44100;
	}
}

static int open_decoder(AVFormatContext* fmt_ctx, int stream_idx, AVCodecContext** codec_ctx_out) {
	const AVCodec* codec;
	AVCodecContext* codec_ctx;
	AVCodecParameters* codecpar = fmt_ctx->streams[stream_idx]->codecpar;
	int err;

	codec = avcodec_find_decoder(codecpar->codec_id);
	if(codec == NULL)
		return AVERROR_DECODER_NOT_FOUND;

	codec_ctx = avcodec_alloc_context3(codec);
	if(codec_ctx == NULL)
		return AVERROR(ENOMEM);

	err = avcodec_parameters_to_context(codec_ctx, codecpar);
	if(err < 0) {
		avcodec_free_context(&codec_ctx);
		return err;
	}

	if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
		codec_ctx->thread_count = 0;
		codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	}

	err = avcodec_open2(codec_ctx, codec, NULL);
	if(err < 0) {
		avcodec_free_context(&codec_ctx);
		return err;
	}

	*codec_ctx_out = codec_ctx;
	return 0;
}

static int audio_queue_write(widget_video_audio_t* audio, const uint8_t* data, size_t len) {
	size_t offset = 0;

	while(offset < len) {
		size_t chunk = 0;
		size_t first = 0;
		size_t write_pos = 0;
		bool stop = false;

		pthread_mutex_lock(&audio->queue_mutex);
		stop = audio->queue_stop;
		if(!stop) {
			chunk = audio->queue_capacity - audio->queue_size;
			if(chunk > len - offset)
				chunk = len - offset;
			if(chunk > 0) {
				write_pos = (audio->queue_start + audio->queue_size) % audio->queue_capacity;
				first = audio->queue_capacity - write_pos;
				if(first > chunk)
					first = chunk;
				memcpy(audio->queue_buf + write_pos, data + offset, first);
				if(chunk > first) {
					memcpy(audio->queue_buf, data + offset + first, chunk - first);
				}
				audio->queue_size += chunk;
			}
		}
		pthread_mutex_unlock(&audio->queue_mutex);

		if(stop || (audio->owner != NULL && audio->owner->isStopRequested()))
			return -1;
		if(chunk == 0) {
			proc_usleep(AUDIO_QUEUE_WAIT_US);
			continue;
		}
		offset += chunk;
	}
	return 0;
}

static void* audio_output_thread_entry(void* p) {
	widget_video_audio_t* audio = (widget_video_audio_t*)p;
	uint8_t tmp[AUDIO_QUEUE_CHUNK];

	for(;;) {
		size_t chunk = 0;
		size_t first = 0;

		pthread_mutex_lock(&audio->queue_mutex);
		bool stop = audio->queue_stop;
		if(audio->queue_size > 0) {
			chunk = audio->queue_size;
			if(chunk > sizeof(tmp))
				chunk = sizeof(tmp);
			first = audio->queue_capacity - audio->queue_start;
			if(first > chunk)
				first = chunk;
			memcpy(tmp, audio->queue_buf + audio->queue_start, first);
			if(chunk > first) {
				memcpy(tmp + first, audio->queue_buf, chunk - first);
			}
			audio->queue_start = (audio->queue_start + chunk) % audio->queue_capacity;
			audio->queue_size -= chunk;
		}
		pthread_mutex_unlock(&audio->queue_mutex);

		if(chunk > 0) {
			if(pcm_write(audio, tmp, chunk) != 0) {
				pthread_mutex_lock(&audio->queue_mutex);
				audio->queue_stop = true;
				audio->queue_size = 0;
				pthread_mutex_unlock(&audio->queue_mutex);
				break;
			}
			continue;
		}
		if(stop)
			break;
		proc_usleep(AUDIO_QUEUE_WAIT_US);
	}

	pthread_mutex_lock(&audio->queue_mutex);
	audio->queue_thread_running = false;
	pthread_mutex_unlock(&audio->queue_mutex);
	return NULL;
}

static int init_audio_playback(widget_video_audio_t* audio, WidgetVideo* owner, AVCodecContext* codec_ctx) {
	widget_video_pcm_config_t config;
	AVChannelLayout stereo_layout;
	unsigned int i;
	int err;

	memset(audio, 0, sizeof(*audio));
	memset(&stereo_layout, 0, sizeof(stereo_layout));
	audio->owner = owner;
	av_channel_layout_default(&stereo_layout, 2);
	audio->out_layout = stereo_layout;
	audio->out_fmt = AV_SAMPLE_FMT_S16;
	audio->out_rate = choose_audio_rate(codec_ctx->sample_rate);
	audio->out_channels = audio->out_layout.nb_channels;

	err = swr_alloc_set_opts2(&audio->swr,
			&audio->out_layout, audio->out_fmt, audio->out_rate,
			&codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
			0, NULL);
	if(err < 0)
		return err;

	err = swr_init(audio->swr);
	if(err < 0)
		return err;

	memset(&config, 0, sizeof(config));
	config.bit_depth = 16;
	config.rate = audio->out_rate;
	config.channels = audio->out_channels;
	config.period_size = 1024;
	config.period_count = 3;
	config.start_threshold = config.period_size;
	config.stop_threshold = 0;

	audio->pcm = NULL;
	for(i = 0; i < sizeof(PCM_DEVICES) / sizeof(PCM_DEVICES[0]); ++i) {
		audio->pcm = pcm_open_device(PCM_DEVICES[i], &config);
		if(audio->pcm != NULL)
			break;
	}
	if(audio->pcm == NULL)
		return AVERROR_EXTERNAL;

	audio->queue_capacity = AUDIO_QUEUE_CAPACITY;
	audio->queue_buf = (uint8_t*)av_malloc(audio->queue_capacity);
	if(audio->queue_buf == NULL)
		return AVERROR(ENOMEM);
	pthread_mutex_init(&audio->queue_mutex, NULL);
	audio->queue_mutex_ready = true;
	audio->queue_stop = false;
	audio->queue_thread_running = true;
	if(pthread_create(&audio->thread, NULL, audio_output_thread_entry, audio) != 0) {
		audio->queue_thread_running = false;
		return AVERROR_EXTERNAL;
	}

	audio->enabled = 1;
	return 0;
}

static void cleanup_audio_playback(widget_video_audio_t* audio) {
	if(audio == NULL)
		return;
	if(audio->queue_mutex_ready) {
		pthread_mutex_lock(&audio->queue_mutex);
		audio->queue_stop = true;
		pthread_mutex_unlock(&audio->queue_mutex);
	}
	if(audio->queue_thread_running)
		pthread_join(audio->thread, NULL);
	if(audio->queue_mutex_ready)
		pthread_mutex_destroy(&audio->queue_mutex);
	pcm_close_device(audio->pcm);
	swr_free(&audio->swr);
	av_free(audio->queue_buf);
	av_free(audio->convert_buf);
	av_channel_layout_uninit(&audio->out_layout);
	memset(audio, 0, sizeof(*audio));
}

static void disable_audio_playback(widget_video_audio_t* audio, AVCodecContext** codec_ctx) {
	cleanup_audio_playback(audio);
	if(codec_ctx != NULL)
		avcodec_free_context(codec_ctx);
}

static int audio_reset_playback(widget_video_audio_t* audio) {
	int err = 0;

	if(audio == NULL)
		return 0;
	if(audio->queue_mutex_ready) {
		pthread_mutex_lock(&audio->queue_mutex);
		audio->queue_start = 0;
		audio->queue_size = 0;
		pthread_mutex_unlock(&audio->queue_mutex);
	}
	if(audio->pcm != NULL) {
		audio->pcm->running = 0;
		audio->pcm->prepared = 0;
	}
	if(audio->swr != NULL) {
		swr_close(audio->swr);
		err = swr_init(audio->swr);
	}
	return err;
}

static int queue_audio_frame(widget_video_audio_t* audio, AVFrame* frame) {
	int out_samples;
	int out_buf_size;
	int converted;
	uint8_t* out_planes[1];
	const uint8_t** in_planes = (const uint8_t**)frame->extended_data;

	out_samples = (int)av_rescale_rnd(
			swr_get_delay(audio->swr, frame->sample_rate) + frame->nb_samples,
			audio->out_rate, frame->sample_rate, AV_ROUND_UP);
	out_buf_size = av_samples_get_buffer_size(
			NULL, audio->out_channels, out_samples, audio->out_fmt, 1);
	if(out_buf_size < 0)
		return out_buf_size;

	if(out_buf_size > audio->convert_buf_size) {
		uint8_t* new_buf = (uint8_t*)av_realloc(audio->convert_buf, out_buf_size);
		if(new_buf == NULL)
			return AVERROR(ENOMEM);
		audio->convert_buf = new_buf;
		audio->convert_buf_size = out_buf_size;
	}

	out_planes[0] = audio->convert_buf;
	converted = swr_convert(audio->swr, out_planes, out_samples, in_planes, frame->nb_samples);
	if(converted < 0)
		return converted;

	out_buf_size = av_samples_get_buffer_size(
			NULL, audio->out_channels, converted, audio->out_fmt, 1);
	if(out_buf_size < 0)
		return out_buf_size;

	return audio_queue_write(audio, audio->convert_buf, (size_t)out_buf_size);
}

static int64_t frame_pts_ms(AVFrame* frame, AVStream* stream) {
	int64_t pts = frame->best_effort_timestamp;
	AVRational ms_base;
	if(pts == AV_NOPTS_VALUE)
		pts = frame->pts;
	if(pts == AV_NOPTS_VALUE)
		pts = frame->pkt_dts;
	if(pts == AV_NOPTS_VALUE)
		return -1;
	ms_base.num = 1;
	ms_base.den = 1000;
	return av_rescale_q(pts, stream->time_base, ms_base);
}

static AVRational stream_fps(AVStream* stream) {
	AVRational fps = {0, 1};

	if(stream == NULL)
		return fps;

	fps = stream->avg_frame_rate;
	if(fps.num <= 0 || fps.den <= 0)
		fps = stream->r_frame_rate;
	return fps;
}

static uint32_t stream_duration_ms(AVFormatContext* fmt_ctx, AVStream* video_stream, AVStream* audio_stream) {
	int64_t duration = AV_NOPTS_VALUE;
	AVStream* stream = NULL;
	AVRational ms_base;
	AVRational fps;

	if(fmt_ctx != NULL && fmt_ctx->duration > 0)
		return (uint32_t)(fmt_ctx->duration / 1000);

	if(video_stream != NULL && video_stream->duration > 0) {
		duration = video_stream->duration;
		stream = video_stream;
	}
	else if(audio_stream != NULL && audio_stream->duration > 0) {
		duration = audio_stream->duration;
		stream = audio_stream;
	}

	if(stream == NULL || duration <= 0)
		goto frames_fallback;

	ms_base.num = 1;
	ms_base.den = 1000;
	return (uint32_t)av_rescale_q(duration, stream->time_base, ms_base);

frames_fallback:
	if(video_stream != NULL && video_stream->nb_frames > 0) {
		fps = stream_fps(video_stream);
		if(fps.num > 0 && fps.den > 0) {
			double ms = ((double)video_stream->nb_frames * 1000.0 * (double)fps.den) / (double)fps.num;
			if(ms > 0.0)
				return (uint32_t)ms;
		}
	}
	return 0;
}

static uint32_t video_clock_now_ms(widget_video_clock_t* clock) {
	int64_t cur;

	if(clock == NULL || !clock->started)
		return 0;

	cur = clock->start_pts_ms + (now_ms() - clock->start_ticks_ms);
	if(cur < 0)
		cur = 0;
	return (uint32_t)cur;
}

static int widget_video_seek(AVFormatContext* fmt_ctx, int video_stream_idx,
		AVStream* video_stream, uint32_t target_ms) {
	AVRational ms_base;
	int64_t ts;
	int err;

	ms_base.num = 1;
	ms_base.den = 1000;
	if(video_stream != NULL) {
		ts = av_rescale_q((int64_t)target_ms, ms_base, video_stream->time_base);
		err = av_seek_frame(fmt_ctx, video_stream_idx, ts, AVSEEK_FLAG_BACKWARD);
		if(err >= 0)
			return 0;
	}

	ts = (int64_t)target_ms * 1000LL;
	return av_seek_frame(fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
}

static int widget_video_get_present_lead_ms(WidgetVideo* video) {
	int lead = VIDEO_PRESENT_LEAD_MIN_MS;
	int loop_ms = VIDEO_PRESENT_LEAD_MIN_MS;
	WidgetWin* win = video->getWin();

	ensure_present_mutex_ready();
	pthread_mutex_lock(&g_present_mutex);
	if(g_present_state.owner == video)
		lead = g_present_state.last_paint_lag_ms + 2;
	pthread_mutex_unlock(&g_present_mutex);

	if(win != NULL && win->getTimerFPS() > 0) {
		uint32_t loop_fps = win->getTimerFPS() * 2;
		if(loop_fps > 0) {
			loop_ms = (int)(1000 / loop_fps);
			if(loop_ms < VIDEO_PRESENT_LEAD_MIN_MS)
				loop_ms = VIDEO_PRESENT_LEAD_MIN_MS;
		}
	}

	if(lead < loop_ms)
		lead = loop_ms;

	if(lead < VIDEO_PRESENT_LEAD_MIN_MS)
		lead = VIDEO_PRESENT_LEAD_MIN_MS;
	if(lead > VIDEO_PRESENT_LEAD_MAX_MS)
		lead = VIDEO_PRESENT_LEAD_MAX_MS;
	return lead;
}

static void widget_video_queue_present(WidgetVideo* video) {
	ensure_present_mutex_ready();
	pthread_mutex_lock(&g_present_mutex);
	g_present_state.owner = video;
	g_present_state.present_serial++;
	g_present_state.present_submit_ms = now_ms();
	pthread_mutex_unlock(&g_present_mutex);

	video->update();
}

static int ensure_scaler(struct SwsContext** sws_ctx, AVFrame* frame) {
	enum AVPixelFormat src_fmt = (enum AVPixelFormat)frame->format;
	if(src_fmt == AV_PIX_FMT_NONE)
		return AVERROR(EINVAL);

	*sws_ctx = sws_getCachedContext(*sws_ctx,
			frame->width, frame->height, src_fmt,
			frame->width, frame->height, AV_PIX_FMT_BGRA,
			SWS_BILINEAR, NULL, NULL, NULL);
	return (*sws_ctx == NULL) ? AVERROR(EINVAL) : 0;
}

static grect_t fit_rect(const grect_t& dst, int src_w, int src_h) {
	grect_t out = dst;
	int draw_w;
	int draw_h;

	if(src_w <= 0 || src_h <= 0 || dst.w <= 0 || dst.h <= 0)
		return out;

	draw_w = dst.w;
	draw_h = (src_h * dst.w) / src_w;
	if(draw_h > dst.h) {
		draw_h = dst.h;
		draw_w = (src_w * dst.h) / src_h;
	}

	out.x = dst.x + (dst.w - draw_w) / 2;
	out.y = dst.y + (dst.h - draw_h) / 2;
	out.w = draw_w;
	out.h = draw_h;
	return out;
}

static void wait_if_paused(WidgetVideo* video, widget_video_clock_t* clock) {
	int64_t begin;
	int64_t delta;

	if(!video->isPausedState())
		return;

	begin = now_ms();
	while(video->isPausedState() && !video->isStopRequested() && !video->hasPendingSeek())
		proc_usleep(50000);
	delta = now_ms() - begin;
	if(clock != NULL && clock->started)
		clock->start_ticks_ms += delta;
}

static bool sync_video_clock(WidgetVideo* video, widget_video_clock_t* clock,
		widget_video_audio_t* audio, int64_t pts_ms, int fallback_delay_ms) {
	int64_t target_ms;
	int64_t now;
	int lead_ms;

	(void)audio;
	wait_if_paused(video, clock);
	if(video->isStopRequested())
		return false;

	if(pts_ms >= 0) {
		now = now_ms();
		if(!clock->started) {
			clock->started = 1;
			clock->start_pts_ms = pts_ms;
			clock->start_ticks_ms = now;
		}

		lead_ms = widget_video_get_present_lead_ms(video);
		target_ms = clock->start_ticks_ms + (pts_ms - clock->start_pts_ms) - lead_ms;
		while(!video->isStopRequested()) {
			wait_if_paused(video, clock);
			if(video->isStopRequested())
				return false;

			now = now_ms();
			if(target_ms <= now)
				break;
			if(target_ms - now > 10)
				proc_usleep(10000);
			else
				proc_usleep((target_ms - now) * 1000);
		}
		return true;
	}

	while(fallback_delay_ms > 0 && !video->isStopRequested()) {
		wait_if_paused(video, clock);
		if(video->isStopRequested())
			return false;
		if(fallback_delay_ms > 10) {
			proc_usleep(10000);
			fallback_delay_ms -= 10;
		}
		else {
			proc_usleep(fallback_delay_ms * 1000);
			break;
		}
	}
	return !video->isStopRequested();
}

static int frame_delay_ms(AVStream* stream) {
	AVRational fps = stream->avg_frame_rate;

	if(fps.num <= 0 || fps.den <= 0)
		fps = stream->r_frame_rate;

	if(fps.num > 0 && fps.den > 0) {
		double delay = 1000.0 * fps.den / fps.num;
		if(delay >= 1.0)
			return (int)delay;
	}
	return 40;
}

static void join_video_worker(widget_video_video_worker_t* worker) {
	if(worker != NULL && worker->thread_running) {
		pthread_join(worker->thread, NULL);
		worker->thread_running = false;
	}
}

static void join_audio_worker(widget_video_audio_worker_t* worker) {
	if(worker != NULL && worker->thread_running) {
		pthread_join(worker->thread, NULL);
		worker->thread_running = false;
	}
}

static int start_video_worker(widget_video_video_worker_t* worker,
		widget_video_pipeline_t* pipeline, AVCodecContext* codec_ctx,
		AVStream* stream, widget_video_audio_t* audio, int delay_ms) {
	memset(worker, 0, sizeof(*worker));
	worker->pipeline = pipeline;
	worker->codec_ctx = codec_ctx;
	worker->stream = stream;
	worker->audio = audio;
	worker->delay_ms = delay_ms;
	worker->thread_running = true;
	if(pthread_create(&worker->thread, NULL, video_decode_thread_entry, worker) != 0) {
		worker->thread_running = false;
		return AVERROR_EXTERNAL;
	}
	return 0;
}

static int start_audio_worker(widget_video_audio_worker_t* worker,
		widget_video_pipeline_t* pipeline, AVCodecContext* codec_ctx,
		widget_video_audio_t* audio) {
	memset(worker, 0, sizeof(*worker));
	worker->pipeline = pipeline;
	worker->codec_ctx = codec_ctx;
	worker->audio = audio;
	worker->thread_running = true;
	if(pthread_create(&worker->thread, NULL, audio_decode_thread_entry, worker) != 0) {
		worker->thread_running = false;
		return AVERROR_EXTERNAL;
	}
	return 0;
}

static int restart_playback_at_ms(widget_video_pipeline_t* pipeline,
		widget_video_video_worker_t* video_worker,
		widget_video_audio_worker_t* audio_worker,
		widget_video_audio_t* audio,
		AVFormatContext* fmt_ctx, int video_stream_idx, int audio_stream_idx,
		AVStream* video_stream, AVCodecContext** video_codec_ctx,
		AVCodecContext** audio_codec_ctx,
		int delay_ms, uint32_t target_ms, string* warning) {
	int err;

	if(warning != NULL)
		*warning = "";

	packet_queue_abort(&pipeline->video_queue);
	packet_queue_abort(&pipeline->audio_queue);
	join_video_worker(video_worker);
	join_audio_worker(audio_worker);

	err = widget_video_seek(fmt_ctx, video_stream_idx, video_stream, target_ms);
	if(err < 0)
		return err;

	/*
	 * On this target, avcodec_flush_buffers() can crash inside
	 * ff_decode_flush_buffers()->av_bsf_flush(NULL) for some codecs after seek.
	 * Re-open decoder contexts instead of flushing in place.
	 */
	avcodec_free_context(video_codec_ctx);
	err = open_decoder(fmt_ctx, video_stream_idx, video_codec_ctx);
	if(err < 0)
		return err;
	if(audio_codec_ctx != NULL && *audio_codec_ctx != NULL) {
		avcodec_free_context(audio_codec_ctx);
		err = open_decoder(fmt_ctx, audio_stream_idx, audio_codec_ctx);
		if(err < 0) {
			if(warning != NULL)
				*warning = "Audio disabled: reopen audio stream failed: " + ffmpeg_error_string(err);
			*audio_codec_ctx = NULL;
		}
	}

	err = audio_reset_playback(audio);
	if(err < 0 && warning != NULL)
		*warning = "Audio reset failed: " + ffmpeg_error_string(err);

	pipeline_reset(pipeline);

	err = start_video_worker(video_worker, pipeline, *video_codec_ctx, video_stream, audio, delay_ms);
	if(err < 0)
		return err;

	if(err >= 0 && audio->enabled && audio_codec_ctx != NULL && *audio_codec_ctx != NULL) {
		err = start_audio_worker(audio_worker, pipeline, *audio_codec_ctx, audio);
		if(err < 0 && warning != NULL)
			*warning = "Audio disabled: create audio thread failed";
	}

	return 0;
}

static int video_worker_present_frame(widget_video_video_worker_t* worker,
		struct SwsContext** sws_ctx, AVFrame* video_frame, AVFrame* rgba_frame) {
	WidgetVideo* video = worker->pipeline->owner;
	int err;
	graph_t* target_graph = NULL;

	err = ensure_scaler(sws_ctx, video_frame);
	if(err < 0)
		return err;

	pthread_mutex_lock(&video->renderMutex);
	if(video->spareFrameGraph == NULL ||
			video->spareFrameGraph->w != video_frame->width ||
			video->spareFrameGraph->h != video_frame->height) {
		if(video->spareFrameGraph != NULL)
			graph_free(video->spareFrameGraph);
		video->spareFrameGraph = graph_new(NULL, video_frame->width, video_frame->height);
	}
	target_graph = video->spareFrameGraph;
	pthread_mutex_unlock(&video->renderMutex);

	if(target_graph == NULL)
		return AVERROR(ENOMEM);

	err = av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize,
			(uint8_t*)target_graph->buffer, AV_PIX_FMT_BGRA,
			target_graph->w, target_graph->h, 1);
	if(err >= 0) {
		rgba_frame->width = target_graph->w;
		rgba_frame->height = target_graph->h;
		rgba_frame->format = AV_PIX_FMT_BGRA;
		sws_scale(*sws_ctx, (const uint8_t* const*)video_frame->data, video_frame->linesize,
				0, video_frame->height, rgba_frame->data, rgba_frame->linesize);
	}
	if(err < 0)
		return err;

	pthread_mutex_lock(&video->renderMutex);
	if(video->spareFrameGraph != target_graph) {
		pthread_mutex_unlock(&video->renderMutex);
		return 0;
	}
	video->spareFrameGraph = video->frameGraph;
	video->frameGraph = target_graph;
	pthread_mutex_unlock(&video->renderMutex);

	if(!sync_video_clock(video, &worker->clock, worker->audio,
				frame_pts_ms(video_frame, worker->stream), worker->delay_ms)) {
		if(video->isStopRequested())
			return AVERROR_EXIT;
		return 0;
	}

	widget_video_queue_present(video);

	pthread_mutex_lock(&video->stateMutex);
	{
		int64_t current_pts_ms = frame_pts_ms(video_frame, worker->stream);
		if(current_pts_ms < 0)
			current_pts_ms = video_clock_now_ms(&worker->clock);
		video->currentMs = (uint32_t)((current_pts_ms < 0) ? 0 : current_pts_ms);
	}
	pthread_mutex_unlock(&video->stateMutex);
	return 0;
}

static int video_worker_drain_decoder(widget_video_video_worker_t* worker,
		struct SwsContext** sws_ctx, AVFrame* video_frame, AVFrame* rgba_frame) {
	WidgetVideo* video = worker->pipeline->owner;
	int err = avcodec_send_packet(worker->codec_ctx, NULL);
	if(err < 0 && err != AVERROR_EOF)
		return err;

	while(!video->isStopRequested()) {
		err = avcodec_receive_frame(worker->codec_ctx, video_frame);
		if(err == AVERROR(EAGAIN) || err == AVERROR_EOF)
			return 0;
		if(err < 0)
			return err;
		err = video_worker_present_frame(worker, sws_ctx, video_frame, rgba_frame);
		av_frame_unref(video_frame);
		if(err < 0)
			return err;
	}
	return 0;
}

static void* video_decode_thread_entry(void* p) {
	widget_video_video_worker_t* worker = (widget_video_video_worker_t*)p;
	WidgetVideo* video = worker->pipeline->owner;
	struct SwsContext* sws_ctx = NULL;
	AVFrame* video_frame = av_frame_alloc();
	AVFrame* rgba_frame = av_frame_alloc();
	AVPacket* packet = NULL;

	memset(&worker->clock, 0, sizeof(worker->clock));
	if(video_frame == NULL || rgba_frame == NULL) {
		worker->err = AVERROR(ENOMEM);
		pipeline_abort(worker->pipeline);
		goto out;
	}

	while(!video->isStopRequested() && !pipeline_is_aborted(worker->pipeline)) {
		int err = packet_queue_pop(&worker->pipeline->video_queue, &packet);
		if(err == AVERROR_EOF)
			break;
		if(err < 0) {
			worker->err = err;
			break;
		}

		wait_if_paused(video, &worker->clock);
		if(video->isStopRequested()) {
			av_packet_free(&packet);
			break;
		}

		err = avcodec_send_packet(worker->codec_ctx, packet);
		av_packet_free(&packet);
		if(err < 0) {
			worker->err = err;
			video->updateStatus("Send video failed: " + ffmpeg_error_string(err));
			pipeline_abort(worker->pipeline);
			break;
		}

		while(!video->isStopRequested()) {
			err = avcodec_receive_frame(worker->codec_ctx, video_frame);
			if(err == AVERROR(EAGAIN) || err == AVERROR_EOF)
				break;
			if(err < 0) {
				worker->err = err;
				video->updateStatus("Receive video failed: " + ffmpeg_error_string(err));
				pipeline_abort(worker->pipeline);
				goto out;
			}

			err = video_worker_present_frame(worker, &sws_ctx, video_frame, rgba_frame);
			av_frame_unref(video_frame);
			if(err < 0) {
				if(err != AVERROR_EXIT) {
					worker->err = err;
					video->updateStatus("Video present failed: " + ffmpeg_error_string(err));
					pipeline_abort(worker->pipeline);
				}
				goto out;
			}
		}
	}

	if(!video->isStopRequested() && !pipeline_is_aborted(worker->pipeline)) {
		worker->err = video_worker_drain_decoder(worker, &sws_ctx, video_frame, rgba_frame);
		if(worker->err < 0 && worker->err != AVERROR_EXIT) {
			video->updateStatus("Video drain failed: " + ffmpeg_error_string(worker->err));
			pipeline_abort(worker->pipeline);
		}
	}

out:
	av_packet_free(&packet);
	av_frame_free(&video_frame);
	av_frame_free(&rgba_frame);
	sws_freeContext(sws_ctx);
	worker->thread_running = false;
	return NULL;
}

static int audio_worker_drain_decoder(widget_video_audio_worker_t* worker, AVFrame* audio_frame) {
	WidgetVideo* video = worker->pipeline->owner;
	int err = avcodec_send_packet(worker->codec_ctx, NULL);
	if(err < 0 && err != AVERROR_EOF)
		return err;

	while(!video->isStopRequested()) {
		err = avcodec_receive_frame(worker->codec_ctx, audio_frame);
		if(err == AVERROR(EAGAIN) || err == AVERROR_EOF)
			return 0;
		if(err < 0)
			return err;
		wait_if_paused(video, NULL);
		if(video->isStopRequested())
			return 0;
		if(!video->isMutedState()) {
			err = queue_audio_frame(worker->audio, audio_frame);
			if(err < 0)
				return err;
		}
		av_frame_unref(audio_frame);
	}
	return 0;
}

static void* audio_decode_thread_entry(void* p) {
	widget_video_audio_worker_t* worker = (widget_video_audio_worker_t*)p;
	WidgetVideo* video = worker->pipeline->owner;
	AVFrame* audio_frame = av_frame_alloc();
	AVPacket* packet = NULL;

	if(audio_frame == NULL) {
		worker->err = AVERROR(ENOMEM);
		pipeline_abort(worker->pipeline);
		goto out;
	}

	while(!video->isStopRequested() && !pipeline_is_aborted(worker->pipeline)) {
		int err = packet_queue_pop(&worker->pipeline->audio_queue, &packet);
		if(err == AVERROR_EOF)
			break;
		if(err < 0) {
			worker->err = err;
			break;
		}

		wait_if_paused(video, NULL);
		if(video->isStopRequested()) {
			av_packet_free(&packet);
			break;
		}

		err = avcodec_send_packet(worker->codec_ctx, packet);
		av_packet_free(&packet);
		if(err < 0) {
			worker->err = err;
			video->updateStatus("Send audio failed: " + ffmpeg_error_string(err));
			pipeline_abort(worker->pipeline);
			break;
		}

		while(!video->isStopRequested()) {
			err = avcodec_receive_frame(worker->codec_ctx, audio_frame);
			if(err == AVERROR(EAGAIN) || err == AVERROR_EOF)
				break;
			if(err < 0) {
				worker->err = err;
				video->updateStatus("Receive audio failed: " + ffmpeg_error_string(err));
				pipeline_abort(worker->pipeline);
				goto out;
			}

			wait_if_paused(video, NULL);
			if(video->isStopRequested()) {
				av_frame_unref(audio_frame);
				goto out;
			}

			if(!video->isMutedState()) {
				err = queue_audio_frame(worker->audio, audio_frame);
				if(err < 0) {
					worker->err = err;
					video->updateStatus("Audio disabled: write sound device failed");
					pipeline_abort(worker->pipeline);
					av_frame_unref(audio_frame);
					goto out;
				}
			}
			av_frame_unref(audio_frame);
		}
	}

	if(!video->isStopRequested() && !pipeline_is_aborted(worker->pipeline)) {
		worker->err = audio_worker_drain_decoder(worker, audio_frame);
		if(worker->err < 0 && worker->err != AVERROR_EXIT) {
			video->updateStatus("Audio drain failed: " + ffmpeg_error_string(worker->err));
			pipeline_abort(worker->pipeline);
		}
	}

out:
	av_packet_free(&packet);
	av_frame_free(&audio_frame);
	worker->thread_running = false;
	return NULL;
}

}

WidgetVideo::WidgetVideo(const string& file) {
	frameGraph = NULL;
	spareFrameGraph = NULL;
	sourceFile = file;
	statusText = "Idle";
	seekPending = false;
	seekTargetMs = 0;
	currentMs = 0;
	totalMs = 0;

	autoPlay = true;
	loopPlay = false;
	muteAudio = false;
	playing = false;
	paused = false;
	stopRequested = false;
	threadRunning = false;
	eof = false;
	decodeThread = 0;

	pthread_mutex_init(&stateMutex, NULL);
	pthread_mutex_init(&renderMutex, NULL);

	ensure_present_mutex_ready();
	pthread_mutex_lock(&g_present_mutex);
	if(g_present_state.owner == NULL)
		g_present_state.owner = this;
	pthread_mutex_unlock(&g_present_mutex);
}

WidgetVideo::~WidgetVideo(void) {
	stopDecodeThread();

	pthread_mutex_lock(&renderMutex);
	if(frameGraph != NULL) {
		graph_free(frameGraph);
		frameGraph = NULL;
	}
	if(spareFrameGraph != NULL) {
		graph_free(spareFrameGraph);
		spareFrameGraph = NULL;
	}
	pthread_mutex_unlock(&renderMutex);

	ensure_present_mutex_ready();
	pthread_mutex_lock(&g_present_mutex);
	if(g_present_state.owner == this) {
		g_present_state.owner = NULL;
		g_present_state.present_serial = 0;
		g_present_state.painted_serial = 0;
		g_present_state.last_paint_lag_ms = 0;
		g_present_state.present_submit_ms = 0;
	}
	pthread_mutex_unlock(&g_present_mutex);

	pthread_mutex_destroy(&renderMutex);
	pthread_mutex_destroy(&stateMutex);
}

void WidgetVideo::onAdd() {
	if(autoPlay && !sourceFile.empty())
		play();
}

void WidgetVideo::updateStatus(const string& text) {
	pthread_mutex_lock(&stateMutex);
	statusText = text;
	pthread_mutex_unlock(&stateMutex);
	update();
}

string WidgetVideo::resolveSourceFile(void) {
	char fullpath[FS_FULL_NAME_MAX] = {0};
	WidgetWin* win;

	if(sourceFile.empty())
		return string();
	if(sourceFile[0] == '/')
		return sourceFile;

	win = getWin();
	if(win == NULL)
		return sourceFile;

	snprintf(fullpath, sizeof(fullpath), "%s/%s", win->getWorkingDir().c_str(), sourceFile.c_str());
	return string(fullpath);
}

bool WidgetVideo::loadVideo(const string& file) {
	stopDecodeThread();

	pthread_mutex_lock(&stateMutex);
	sourceFile = file;
	eof = false;
	currentMs = 0;
	totalMs = 0;
	pthread_mutex_unlock(&stateMutex);

	pthread_mutex_lock(&renderMutex);
	if(frameGraph != NULL) {
		graph_free(frameGraph);
		frameGraph = NULL;
	}
	if(spareFrameGraph != NULL) {
		graph_free(spareFrameGraph);
		spareFrameGraph = NULL;
	}
	pthread_mutex_unlock(&renderMutex);

	updateStatus(file.empty() ? "Idle" : "Ready");
	if(autoPlay && father != NULL && !file.empty())
		play();
	return !file.empty();
}

void WidgetVideo::setPlaybackState(bool newPlaying, bool newPaused, bool newEof) {
	pthread_mutex_lock(&stateMutex);
	playing = newPlaying;
	paused = newPaused;
	eof = newEof;
	pthread_mutex_unlock(&stateMutex);
}

bool WidgetVideo::isStopRequested(void) {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = stopRequested;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isPausedState(void) {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = paused;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isMutedState(void) {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = muteAudio;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isLoopState(void) {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = loopPlay;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::hasPendingSeek(void) {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = seekPending;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isPlaying() {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = playing;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isPaused() {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = paused;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

bool WidgetVideo::isEOF() {
	bool ret;
	pthread_mutex_lock(&stateMutex);
	ret = eof;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

uint32_t WidgetVideo::getCurrentMs() {
	uint32_t ret;
	pthread_mutex_lock(&stateMutex);
	ret = currentMs;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

uint32_t WidgetVideo::getTotalMs() {
	uint32_t ret;
	pthread_mutex_lock(&stateMutex);
	ret = totalMs;
	pthread_mutex_unlock(&stateMutex);
	return ret;
}

void WidgetVideo::seekToMs(uint32_t ms) {
	pthread_mutex_lock(&stateMutex);
	if(totalMs > 0 && ms > totalMs)
		ms = totalMs;
	seekPending = true;
	seekTargetMs = ms;
	currentMs = ms;
	eof = false;
	pthread_mutex_unlock(&stateMutex);
	update();
}

void WidgetVideo::seekToProgress(float progress) {
	uint32_t total = getTotalMs();
	if(progress < 0.0f)
		progress = 0.0f;
	if(progress > 1.0f)
		progress = 1.0f;
	if(total == 0)
		return;
	seekToMs((uint32_t)((float)total * progress));
}

bool WidgetVideo::takeSeekRequest(uint32_t* target_ms) {
	bool pending = false;

	pthread_mutex_lock(&stateMutex);
	if(seekPending) {
		*target_ms = seekTargetMs;
		seekPending = false;
		eof = false;
		currentMs = seekTargetMs;
		statusText = "";
		pending = true;
	}
	pthread_mutex_unlock(&stateMutex);
	return pending;
}

bool WidgetVideo::isReady() {
	bool ready;
	pthread_mutex_lock(&renderMutex);
	ready = (frameGraph != NULL);
	pthread_mutex_unlock(&renderMutex);
	return ready;
}

void WidgetVideo::setAutoPlay(bool autoplay) {
	autoPlay = autoplay;
}

void WidgetVideo::setLoop(bool loop) {
	pthread_mutex_lock(&stateMutex);
	loopPlay = loop;
	pthread_mutex_unlock(&stateMutex);
}

void WidgetVideo::setMute(bool mute) {
	pthread_mutex_lock(&stateMutex);
	muteAudio = mute;
	pthread_mutex_unlock(&stateMutex);
}

void WidgetVideo::stopDecodeThread() {
	pthread_t tid = 0;
	bool join = false;

	pthread_mutex_lock(&stateMutex);
	if(threadRunning) {
		stopRequested = true;
		tid = decodeThread;
		join = true;
	}
	pthread_mutex_unlock(&stateMutex);

	if(join)
		pthread_join(tid, NULL);

	pthread_mutex_lock(&stateMutex);
	threadRunning = false;
	playing = false;
	paused = false;
	stopRequested = false;
	pthread_mutex_unlock(&stateMutex);
}

void WidgetVideo::play() {
	int ret;

	if(sourceFile.empty()) {
		updateStatus("No source");
		return;
	}

	pthread_mutex_lock(&stateMutex);
	if(threadRunning) {
		if(paused) {
			paused = false;
			playing = true;
			statusText = "";
		}
		pthread_mutex_unlock(&stateMutex);
		update();
		return;
	}
	stopRequested = false;
	paused = false;
	playing = true;
	eof = false;
	statusText = "Loading...";
	threadRunning = true;
	pthread_mutex_unlock(&stateMutex);

	ret = pthread_create(&decodeThread, NULL, WidgetVideo::decodeThreadEntry, this);
	if(ret != 0) {
		pthread_mutex_lock(&stateMutex);
		threadRunning = false;
		playing = false;
		statusText = "Create thread failed";
		pthread_mutex_unlock(&stateMutex);
		update();
	}
}

void WidgetVideo::pause() {
	pthread_mutex_lock(&stateMutex);
	if(threadRunning && playing) {
		paused = true;
		playing = false;
		statusText = "Paused";
	}
	pthread_mutex_unlock(&stateMutex);
	update();
}

void WidgetVideo::stop() {
	stopDecodeThread();
	updateStatus("Stopped");
}

void* WidgetVideo::decodeThreadEntry(void* p) {
	WidgetVideo* video = (WidgetVideo*)p;
	video->decodeLoop();
	return NULL;
}

void WidgetVideo::decodeLoop() {
	string file;
	string audio_status;
	AVDictionary* format_opts = NULL;
	AVFormatContext* fmt_ctx = NULL;
	AVCodecContext* video_codec_ctx = NULL;
	AVCodecContext* audio_codec_ctx = NULL;
	AVPacket* packet = NULL;
	AVStream* video_stream = NULL;
	AVStream* audio_stream = NULL;
	widget_video_audio_t audio;
	widget_video_pipeline_t pipeline;
	widget_video_video_worker_t video_worker;
	widget_video_audio_worker_t audio_worker;
	int video_stream_idx;
	int audio_stream_idx;
	int delay_ms;
	int err = 0;
	uint32_t pending_seek_ms = 0;

	memset(&audio, 0, sizeof(audio));
	memset(&pipeline, 0, sizeof(pipeline));
	memset(&video_worker, 0, sizeof(video_worker));
	memset(&audio_worker, 0, sizeof(audio_worker));

	av_log_set_level(AV_LOG_ERROR);
	file = resolveSourceFile();
	if(file.empty()) {
		updateStatus("No source");
		goto out;
	}

	/*
	 * Some MP4/MOV files trip a data abort in FFmpeg's mov chapter-track
	 * parsing on this target. Disable chapter import so playback can still
	 * open the container without touching the decode path.
	 */
	av_dict_set(&format_opts, "ignore_chapters", "1", 0);
	updateStatus("Loading...");
	err = avformat_open_input(&fmt_ctx, file.c_str(), NULL, &format_opts);
	av_dict_free(&format_opts);
	if(err < 0) {
		updateStatus("Open failed: " + ffmpeg_error_string(err));
		goto out_cleanup;
	}

	err = avformat_find_stream_info(fmt_ctx, NULL);
	if(err < 0) {
		updateStatus("Stream info failed: " + ffmpeg_error_string(err));
		goto out_cleanup;
	}

	video_stream_idx = find_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO);
	if(video_stream_idx < 0) {
		updateStatus("No video stream");
		goto out_cleanup;
	}
	video_stream = fmt_ctx->streams[video_stream_idx];
	audio_stream_idx = find_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO);
	if(audio_stream_idx >= 0)
		audio_stream = fmt_ctx->streams[audio_stream_idx];

	pthread_mutex_lock(&stateMutex);
	totalMs = stream_duration_ms(fmt_ctx, video_stream, audio_stream);
	currentMs = 0;
	pthread_mutex_unlock(&stateMutex);

	err = open_decoder(fmt_ctx, video_stream_idx, &video_codec_ctx);
	if(err < 0) {
		updateStatus("Open video failed: " + ffmpeg_error_string(err));
		goto out_cleanup;
	}

	if(audio_stream != NULL && !isMutedState()) {
		err = open_decoder(fmt_ctx, audio_stream_idx, &audio_codec_ctx);
		if(err < 0) {
			audio_status = "Audio disabled: open audio stream failed: " + ffmpeg_error_string(err);
		}

		if(audio_codec_ctx != NULL) {
			err = init_audio_playback(&audio, this, audio_codec_ctx);
			if(err < 0) {
				audio_status = "Audio disabled: open sound device failed: " + ffmpeg_error_string(err);
				disable_audio_playback(&audio, &audio_codec_ctx);
			}
		}
	}

	err = pipeline_init(&pipeline, this);
	if(err < 0) {
		updateStatus("Pipeline init failed: " + ffmpeg_error_string(err));
		goto out_cleanup;
	}

	delay_ms = frame_delay_ms(video_stream);
	err = start_video_worker(&video_worker, &pipeline, video_codec_ctx, video_stream, &audio, delay_ms);
	if(err < 0) {
		updateStatus("Create video thread failed");
		goto out_cleanup;
	}

	if(audio.enabled && audio_codec_ctx != NULL) {
		err = start_audio_worker(&audio_worker, &pipeline, audio_codec_ctx, &audio);
		if(err < 0)
			audio_status = "Audio disabled: create audio thread failed";
	}

	packet = av_packet_alloc();
	if(packet == NULL) {
		updateStatus("ffmpeg alloc failed");
		pipeline_abort(&pipeline);
		goto out_cleanup;
	}

	if(audio_status.empty())
		updateStatus("");
	else
		updateStatus(audio_status);

	while(!isStopRequested()) {
		int video_backlog;
		int audio_backlog;
		string restart_warning;

		if(pipeline_is_aborted(&pipeline))
			break;

		if(takeSeekRequest(&pending_seek_ms)) {
			err = restart_playback_at_ms(&pipeline, &video_worker, &audio_worker, &audio,
					fmt_ctx, video_stream_idx, audio_stream_idx, video_stream,
					&video_codec_ctx, &audio_codec_ctx, delay_ms, pending_seek_ms,
					&restart_warning);
			if(err < 0) {
				updateStatus("Seek failed: " + ffmpeg_error_string(err));
				pipeline_abort(&pipeline);
				break;
			}

			pthread_mutex_lock(&stateMutex);
			currentMs = pending_seek_ms;
			eof = false;
			pthread_mutex_unlock(&stateMutex);

			if(restart_warning.empty())
				updateStatus("");
			else
				updateStatus(restart_warning);
			continue;
		}

		wait_if_paused(this, NULL);
		if(isStopRequested() || pipeline_is_aborted(&pipeline))
			break;

		video_backlog = packet_queue_count(&pipeline.video_queue);
		audio_backlog = audio_worker.thread_running ?
				packet_queue_count(&pipeline.audio_queue) : 0;
		if(video_backlog >= VIDEO_PACKET_HIGH_WATER ||
				(audio_worker.thread_running &&
				 audio_backlog >= AUDIO_PACKET_HIGH_WATER)) {
			proc_usleep(DEMUX_BACKPRESSURE_US);
			continue;
		}

		err = av_read_frame(fmt_ctx, packet);
		if(err < 0) {
			packet_queue_set_eof(&pipeline.video_queue);
			packet_queue_set_eof(&pipeline.audio_queue);
			join_video_worker(&video_worker);
			join_audio_worker(&audio_worker);

			if(isStopRequested() || pipeline_is_aborted(&pipeline))
				break;
			if(isLoopState()) {
				setPlaybackState(true, false, false);
				err = restart_playback_at_ms(&pipeline, &video_worker, &audio_worker, &audio,
						fmt_ctx, video_stream_idx, audio_stream_idx, video_stream,
						&video_codec_ctx, &audio_codec_ctx, delay_ms, 0,
						&restart_warning);
				if(err < 0) {
					updateStatus("Loop seek failed: " + ffmpeg_error_string(err));
					pipeline_abort(&pipeline);
					break;
				}

				pthread_mutex_lock(&stateMutex);
				currentMs = 0;
				eof = false;
				pthread_mutex_unlock(&stateMutex);

				if(restart_warning.empty())
					updateStatus("");
				else
					updateStatus(restart_warning);
				continue;
			}

			updateStatus("Done");
			setPlaybackState(false, false, true);
			break;
		}

		err = 0;
		if(packet->stream_index == video_stream_idx) {
			err = packet_queue_push_clone(&pipeline.video_queue, packet);
		}
		else if(audio_worker.thread_running && packet->stream_index == audio_stream_idx) {
			err = packet_queue_push_clone(&pipeline.audio_queue, packet);
		}

		av_packet_unref(packet);
		if(err < 0) {
			if(err != AVERROR_EXIT)
				updateStatus("Queue packet failed: " + ffmpeg_error_string(err));
			pipeline_abort(&pipeline);
			break;
		}
	}

out_cleanup:
	av_packet_free(&packet);
	pipeline_abort(&pipeline);
	join_video_worker(&video_worker);
	join_audio_worker(&audio_worker);
	pipeline_destroy(&pipeline);
	cleanup_audio_playback(&audio);
	avcodec_free_context(&audio_codec_ctx);
	avcodec_free_context(&video_codec_ctx);
	avformat_close_input(&fmt_ctx);

out:
	pthread_mutex_lock(&stateMutex);
	threadRunning = false;
	stopRequested = false;
	if(!paused)
		playing = false;
	pthread_mutex_unlock(&stateMutex);
	update();
}

bool WidgetVideo::onMouse(xevent_t* ev) {
	if(ev->state == MOUSE_STATE_DOWN && ev->value.mouse.button == MOUSE_BUTTON_LEFT) {
		RootWidget* root = getRoot();
		if(root != NULL)
			root->focus(this);

		if(isPlaying())
			pause();
		else
			play();
		return true;
	}
	return false;
}

bool WidgetVideo::onIM(xevent_t* ev) {
	int v;

	if(ev->state != XIM_STATE_PRESS)
		return false;

	v = ev->value.im.value;
	if(v == KEY_SPACE || v == KEY_ENTER) {
		if(isPlaying())
			pause();
		else
			play();
		return true;
	}
	if(v == KEY_ESC) {
		stop();
		return true;
	}
	if(v == 'm' || v == 'M') {
		setMute(!isMutedState());
		updateStatus(isMutedState() ? "Muted" : "");
		return true;
	}
	return false;
}

void WidgetVideo::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	string text;
	font_t* font;
	bool paused_state;
	int64_t now;

	graph_fill_rect(g, r.x, r.y, r.w, r.h, argb(0xff, 0, 0, 0));

	pthread_mutex_lock(&renderMutex);
	if(frameGraph != NULL) {
		grect_t dr = fit_rect(r, frameGraph->w, frameGraph->h);
		graph_blt_fit(frameGraph, 0, 0, frameGraph->w, frameGraph->h,
				g, dr.x, dr.y, dr.w, dr.h);
	}
	pthread_mutex_unlock(&renderMutex);

	ensure_present_mutex_ready();
	pthread_mutex_lock(&g_present_mutex);
	if(g_present_state.owner == this &&
			g_present_state.painted_serial < g_present_state.present_serial) {
		now = now_ms();
		g_present_state.painted_serial = g_present_state.present_serial;
		if(g_present_state.present_submit_ms > 0 &&
				now >= g_present_state.present_submit_ms) {
			g_present_state.last_paint_lag_ms =
					(int32_t)(now - g_present_state.present_submit_ms);
		}
	}
	pthread_mutex_unlock(&g_present_mutex);

	pthread_mutex_lock(&stateMutex);
	text = statusText;
	paused_state = paused;
	pthread_mutex_unlock(&stateMutex);

	if(text.empty() && !paused_state)
		return;
	if(text.empty())
		text = "Paused";

	font = theme->getFont();
	if(font == NULL)
		return;
	graph_draw_text_font_align(g, r.x, r.y, r.w, r.h,
			text.c_str(), font, theme->basic.fontSize, argb(0xff, 0xff, 0xff, 0xff), FONT_ALIGN_CENTER);
}

void WidgetVideo::setAttr(const string& attr, json_var_t* value) {
	Widget::setAttr(attr, value);
	if(attr == "file") {
		loadVideo(json_var_get_str(value));
	}
	else if(attr == "autoplay") {
		setAutoPlay(json_var_get_int(value) != 0);
	}
	else if(attr == "loop") {
		setLoop(json_var_get_int(value) != 0);
	}
	else if(attr == "mute") {
		setMute(json_var_get_int(value) != 0);
	}
}

gsize_t WidgetVideo::getMinSize(void) {
	gsize_t sz = {160 + marginH * 2, 120 + marginV * 2};

	pthread_mutex_lock(&renderMutex);
	if(frameGraph != NULL) {
		sz.w = frameGraph->w + marginH * 2;
		sz.h = frameGraph->h + marginV * 2;
	}
	pthread_mutex_unlock(&renderMutex);
	return sz;
}

}

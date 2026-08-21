#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

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

#define AUDIO_BUFFER_CAPACITY (1024 * 1024)

typedef struct AudioQueue {
    SDL_AudioDeviceID device;
    uint8_t* data;
    size_t capacity;
    size_t start;
    size_t size;
} AudioQueue;

typedef struct AudioPlayback {
    int enabled;
    SDL_AudioSpec spec;
    AudioQueue queue;
    struct SwrContext* swr;
    AVChannelLayout out_layout;
    enum AVSampleFormat out_fmt;
    int out_rate;
    int out_channels;
    uint8_t* convert_buf;
    int convert_buf_size;
} AudioPlayback;

typedef struct VideoClock {
    int started;
    int64_t start_ticks_ms;
    int64_t start_pts_ms;
} VideoClock;

typedef struct PerfStats {
    uint64_t report_start_us;
    uint64_t last_report_us;
    uint64_t video_frames;
    uint64_t audio_frames;
    uint64_t video_send_us;
    uint64_t video_decode_us;
    uint64_t video_scale_us;
    uint64_t video_render_us;
    uint64_t video_sync_wait_us;
    uint64_t audio_send_us;
    uint64_t audio_decode_us;
    uint64_t audio_resample_us;
    uint64_t audio_queue_wait_us;
    uint64_t audio_queue_wait_count;
    uint64_t read_us;
} PerfStats;

static uint64_t perf_now_us(void)
{
    uint64_t freq = (uint64_t)SDL_GetPerformanceFrequency();
    uint64_t counter = (uint64_t)SDL_GetPerformanceCounter();

    if (freq == 0)
        return 0;
    return (counter * 1000000ULL) / freq;
}

static void perf_maybe_report(PerfStats* perf)
{
    uint64_t now_us;
    double wall_ms;
    double video_fps;

    if (perf == NULL)
        return;

    now_us = perf_now_us();
    if (perf->report_start_us == 0) {
        perf->report_start_us = now_us;
        perf->last_report_us = now_us;
        return;
    }

    if (now_us - perf->last_report_us < 3000000ULL)
        return;

    wall_ms = (double)(now_us - perf->report_start_us) / 1000.0;
    video_fps = wall_ms > 0.0 ? ((double)perf->video_frames * 1000.0 / wall_ms) : 0.0;

    printf("perf: wall=%.1fms video_frames=%llu audio_frames=%llu fps=%.2f "
           "read=%.1fms vsend=%.1fms vdec=%.1fms vscale=%.1fms vrender=%.1fms "
           "vsleep=%.1fms asend=%.1fms adec=%.1fms aresample=%.1fms "
           "aqwait=%.1fms aqwait_n=%llu\n",
           wall_ms,
           (unsigned long long)perf->video_frames,
           (unsigned long long)perf->audio_frames,
           video_fps,
           (double)perf->read_us / 1000.0,
           (double)perf->video_send_us / 1000.0,
           (double)perf->video_decode_us / 1000.0,
           (double)perf->video_scale_us / 1000.0,
           (double)perf->video_render_us / 1000.0,
           (double)perf->video_sync_wait_us / 1000.0,
           (double)perf->audio_send_us / 1000.0,
           (double)perf->audio_decode_us / 1000.0,
           (double)perf->audio_resample_us / 1000.0,
           (double)perf->audio_queue_wait_us / 1000.0,
           (unsigned long long)perf->audio_queue_wait_count);
    perf->last_report_us = now_us;
}

static void print_ffmpeg_error(const char* prefix, int err)
{
    char errbuf[128];

    av_strerror(err, errbuf, sizeof(errbuf));
    printf("%s: %s\n", prefix, errbuf);
}

static void show_error_message(const char* title, const char* message)
{
    printf("%s: %s\n", title, message);
    if (SDL_WasInit(SDL_INIT_VIDEO))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, NULL);
}

static void show_ffmpeg_error_message(const char* title, int err)
{
    char errbuf[128];

    av_strerror(err, errbuf, sizeof(errbuf));
    show_error_message(title, errbuf);
}

static int find_stream(AVFormatContext* fmt_ctx, enum AVMediaType type)
{
    unsigned int i;

    for (i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == type)
            return (int)i;
    }
    return -1;
}

static Uint32 frame_delay_ms(AVStream* stream)
{
    AVRational fps = stream->avg_frame_rate;

    if (fps.num <= 0 || fps.den <= 0)
        fps = stream->r_frame_rate;

    if (fps.num > 0 && fps.den > 0) {
        double delay = 1000.0 * fps.den / fps.num;
        if (delay >= 1.0)
            return (Uint32)delay;
    }
    return 40;
}

static int render_frame(SDL_Renderer* renderer, SDL_Texture* texture, AVFrame* frame)
{
    if (SDL_UpdateTexture(texture, NULL, frame->data[0], frame->linesize[0]) != 0) {
        printf("SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    return 0;
}

static int ensure_video_scaler(struct SwsContext** sws_ctx,
                               AVCodecContext* codec_ctx,
                               AVFrame* frame)
{
    enum AVPixelFormat src_fmt = (enum AVPixelFormat)frame->format;

    if (*sws_ctx != NULL)
        return 0;
    if (src_fmt == AV_PIX_FMT_NONE)
        return AVERROR(EINVAL);

    *sws_ctx = sws_getContext(frame->width, frame->height, src_fmt,
                              codec_ctx->width, codec_ctx->height, AV_PIX_FMT_BGRA,
                              SWS_BILINEAR, NULL, NULL, NULL);
    if (*sws_ctx == NULL)
        return AVERROR(EINVAL);
    return 0;
}

static void audio_callback(void* userdata, Uint8* stream, int len)
{
    AudioQueue* queue = (AudioQueue*)userdata;
    size_t copy = 0;
    size_t first = 0;

    memset(stream, 0, (size_t)len);
    if (queue == NULL || queue->size == 0 || queue->data == NULL)
        return;

    copy = queue->size < (size_t)len ? queue->size : (size_t)len;
    first = queue->capacity - queue->start;
    if (first > copy)
        first = copy;

    memcpy(stream, queue->data + queue->start, first);
    if (copy > first)
        memcpy(stream + first, queue->data, copy - first);

    queue->start = (queue->start + copy) % queue->capacity;
    queue->size -= copy;
}

static size_t audio_queue_space(const AudioQueue* queue)
{
    return queue->capacity - queue->size;
}

static size_t audio_queue_size(AudioQueue* queue)
{
    size_t size;

    SDL_LockAudioDevice(queue->device);
    size = queue->size;
    SDL_UnlockAudioDevice(queue->device);
    return size;
}

static int audio_queue_write(AudioQueue* queue, const uint8_t* data, size_t len,
                             PerfStats* perf)
{
    size_t offset = 0;

    while (offset < len) {
        size_t chunk = 0;
        size_t first = 0;
        size_t write_pos = 0;

        SDL_LockAudioDevice(queue->device);
        chunk = audio_queue_space(queue);
        if (chunk > len - offset)
            chunk = len - offset;
        if (chunk > 0) {
            write_pos = (queue->start + queue->size) % queue->capacity;
            first = queue->capacity - write_pos;
            if (first > chunk)
                first = chunk;

            memcpy(queue->data + write_pos, data + offset, first);
            if (chunk > first)
                memcpy(queue->data, data + offset + first, chunk - first);
            queue->size += chunk;
        }
        SDL_UnlockAudioDevice(queue->device);

        if (chunk == 0) {
            uint64_t wait_begin = perf_now_us();
            SDL_Delay(5);
            if (perf != NULL) {
                perf->audio_queue_wait_us += perf_now_us() - wait_begin;
                perf->audio_queue_wait_count++;
            }
            continue;
        }
        offset += chunk;
    }
    return 0;
}

static int choose_audio_rate(int sample_rate)
{
    switch (sample_rate) {
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

static int init_audio_playback(AudioPlayback* audio, AVCodecContext* codec_ctx)
{
    SDL_AudioSpec desired;
    int err;

    memset(audio, 0, sizeof(*audio));
    memset(&desired, 0, sizeof(desired));

    audio->out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    audio->out_fmt = AV_SAMPLE_FMT_S16;
    audio->out_rate = choose_audio_rate(codec_ctx->sample_rate);
    audio->out_channels = audio->out_layout.nb_channels;

    audio->queue.capacity = AUDIO_BUFFER_CAPACITY;
    audio->queue.data = (uint8_t*)av_malloc(audio->queue.capacity);
    if (audio->queue.data == NULL)
        return AVERROR(ENOMEM);

    desired.freq = audio->out_rate;
    desired.format = AUDIO_S16SYS;
    desired.channels = (Uint8)audio->out_channels;
    desired.samples = 2048;
    desired.callback = audio_callback;
    desired.userdata = &audio->queue;

    audio->queue.device = SDL_OpenAudioDevice(NULL, 0, &desired, &audio->spec, 0);
    if (audio->queue.device == 0) {
        show_error_message("SDL_OpenAudioDevice failed", SDL_GetError());
        return AVERROR_EXTERNAL;
    }

    audio->out_rate = audio->spec.freq;
    audio->out_channels = audio->spec.channels;
    av_channel_layout_uninit(&audio->out_layout);
    av_channel_layout_default(&audio->out_layout, audio->out_channels);

    err = swr_alloc_set_opts2(&audio->swr,
                              &audio->out_layout, audio->out_fmt, audio->out_rate,
                              &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
                              0, NULL);
    if (err < 0)
        return err;

    err = swr_init(audio->swr);
    if (err < 0)
        return err;

    SDL_PauseAudioDevice(audio->queue.device, 0);
    audio->enabled = 1;
    return 0;
}

static void cleanup_audio_playback(AudioPlayback* audio)
{
    if (audio->queue.device != 0)
        SDL_CloseAudioDevice(audio->queue.device);
    swr_free(&audio->swr);
    av_free(audio->queue.data);
    av_free(audio->convert_buf);
    av_channel_layout_uninit(&audio->out_layout);
    memset(audio, 0, sizeof(*audio));
}

static int queue_audio_frame(AudioPlayback* audio, AVFrame* frame, PerfStats* perf)
{
    int out_samples;
    int out_buf_size;
    int converted;
    uint8_t* out_planes[1];
    const uint8_t** in_planes = (const uint8_t**)frame->extended_data;
    uint64_t begin_us;

    out_samples = (int)av_rescale_rnd(
        swr_get_delay(audio->swr, frame->sample_rate) + frame->nb_samples,
        audio->out_rate, frame->sample_rate, AV_ROUND_UP);
    out_buf_size = av_samples_get_buffer_size(
        NULL, audio->out_channels, out_samples, audio->out_fmt, 1);
    if (out_buf_size < 0)
        return out_buf_size;

    if (out_buf_size > audio->convert_buf_size) {
        uint8_t* new_buf = (uint8_t*)av_realloc(audio->convert_buf, out_buf_size);
        if (new_buf == NULL)
            return AVERROR(ENOMEM);
        audio->convert_buf = new_buf;
        audio->convert_buf_size = out_buf_size;
    }

    out_planes[0] = audio->convert_buf;
    begin_us = perf_now_us();
    converted = swr_convert(audio->swr, out_planes, out_samples,
                            in_planes, frame->nb_samples);
    if (perf != NULL)
        perf->audio_resample_us += perf_now_us() - begin_us;
    if (converted < 0)
        return converted;

    out_buf_size = av_samples_get_buffer_size(
        NULL, audio->out_channels, converted, audio->out_fmt, 1);
    if (out_buf_size < 0)
        return out_buf_size;

    return audio_queue_write(&audio->queue, audio->convert_buf, (size_t)out_buf_size, perf);
}

static int handle_events(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            return 0;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            return 0;
    }
    return 1;
}

static int open_decoder(AVFormatContext* fmt_ctx, int stream_idx,
                        AVCodecContext** codec_ctx_out)
{
    const AVCodec* codec;
    AVCodecContext* codec_ctx;
    int err;
    AVCodecParameters* codecpar = fmt_ctx->streams[stream_idx]->codecpar;

    codec = avcodec_find_decoder(codecpar->codec_id);
    if (codec == NULL)
        return AVERROR_DECODER_NOT_FOUND;

    codec_ctx = avcodec_alloc_context3(codec);
    if (codec_ctx == NULL)
        return AVERROR(ENOMEM);

    err = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (err < 0) {
        avcodec_free_context(&codec_ctx);
        return err;
    }

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        /* Frame/slice threading is fragile on Ewok; keep H.264 on a single
         * thread so send_packet() stays on the straightforward decode path. */
        codec_ctx->thread_count = 1;
        codec_ctx->thread_type = 0;
    }

    err = avcodec_open2(codec_ctx, codec, NULL);
    if (err < 0) {
        avcodec_free_context(&codec_ctx);
        return err;
    }

    *codec_ctx_out = codec_ctx;
    return 0;
}

static int64_t frame_pts_ms(AVFrame* frame, AVStream* stream)
{
    int64_t pts = frame->best_effort_timestamp;

    if (pts == AV_NOPTS_VALUE)
        return -1;
    return av_rescale_q(pts, stream->time_base, (AVRational){ 1, 1000 });
}

static void sync_video_clock(VideoClock* clock, int64_t pts_ms, Uint32 fallback_delay)
{
    Uint32 now = SDL_GetTicks();

    if (pts_ms >= 0) {
        int64_t target_ms;

        if (!clock->started) {
            clock->started = 1;
            clock->start_pts_ms = pts_ms;
            clock->start_ticks_ms = now;
        }

        target_ms = clock->start_ticks_ms + (pts_ms - clock->start_pts_ms);
        if (target_ms > (int64_t)now && target_ms - now < 1000)
            SDL_Delay((Uint32)(target_ms - now));
        return;
    }

    if (fallback_delay > 0)
        SDL_Delay(fallback_delay);
}

static void sync_video_clock_profiled(VideoClock* clock, int64_t pts_ms, Uint32 fallback_delay,
                                      PerfStats* perf)
{
    uint64_t begin_us = perf_now_us();

    sync_video_clock(clock, pts_ms, fallback_delay);
    if (perf != NULL)
        perf->video_sync_wait_us += perf_now_us() - begin_us;
}

static int drain_video_decoder(AVCodecContext* codec_ctx, AVFrame* frame,
                               struct SwsContext** sws_ctx, SDL_Renderer* renderer,
                               SDL_Texture* texture, AVFrame* rgba_frame,
                               AVStream* video_stream, VideoClock* video_clock,
                               Uint32 fallback_delay, int* running, PerfStats* perf)
{
    int err;
    uint64_t begin_us;

    while (*running) {
        begin_us = perf_now_us();
        err = avcodec_receive_frame(codec_ctx, frame);
        if (perf != NULL)
            perf->video_decode_us += perf_now_us() - begin_us;
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
            return 0;
        if (err < 0)
            return err;

        err = ensure_video_scaler(sws_ctx, codec_ctx, frame);
        if (err < 0)
            return err;

        begin_us = perf_now_us();
        sws_scale(*sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0,
                  frame->height, rgba_frame->data, rgba_frame->linesize);
        if (perf != NULL)
            perf->video_scale_us += perf_now_us() - begin_us;
        sync_video_clock_profiled(video_clock, frame_pts_ms(frame, video_stream),
                                  fallback_delay, perf);
        begin_us = perf_now_us();
        if (render_frame(renderer, texture, rgba_frame) != 0)
            return AVERROR_EXTERNAL;
        if (perf != NULL) {
            perf->video_render_us += perf_now_us() - begin_us;
            perf->video_frames++;
            perf_maybe_report(perf);
        }
        *running = handle_events();
        av_frame_unref(frame);
    }

    return 0;
}

static int drain_audio_decoder(AVCodecContext* codec_ctx, AVFrame* frame,
                               AudioPlayback* audio, PerfStats* perf)
{
    int err;
    uint64_t begin_us;

    while (1) {
        begin_us = perf_now_us();
        err = avcodec_receive_frame(codec_ctx, frame);
        if (perf != NULL)
            perf->audio_decode_us += perf_now_us() - begin_us;
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
            return 0;
        if (err < 0)
            return err;

        err = queue_audio_frame(audio, frame, perf);
        av_frame_unref(frame);
        if (perf != NULL) {
            perf->audio_frames++;
            perf_maybe_report(perf);
        }
        if (err < 0)
            return err;
    }
}

int main(int argc, char** argv)
{
    const char* file;
    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* video_codec_ctx = NULL;
    AVCodecContext* audio_codec_ctx = NULL;
    struct SwsContext* sws_ctx = NULL;
    AVFrame* video_frame = NULL;
    AVFrame* audio_frame = NULL;
    AVFrame* rgba_frame = NULL;
    AVPacket* packet = NULL;
    uint8_t* rgba_buf = NULL;
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Texture* texture = NULL;
    AVStream* video_stream = NULL;
    AVStream* audio_stream = NULL;
    AudioPlayback audio = { 0 };
    VideoClock video_clock = { 0 };
    PerfStats perf = { 0 };
    int video_stream_idx;
    int audio_stream_idx;
    int err;
    int running = 1;
    Uint32 delay_ms;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    av_log_set_level(AV_LOG_ERROR);

    if (argc < 2) {
        show_error_message("mp4player", "Usage: mp4player <video-file>");
        return -1;
    }
    file = argv[1];

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    err = avformat_open_input(&fmt_ctx, file, NULL, NULL);
    if (err < 0) {
        show_ffmpeg_error_message("open input failed", err);
        goto fail;
    }

    video_stream_idx = find_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO);
    if (video_stream_idx < 0) {
        show_error_message("mp4player", "no video stream found");
        goto fail;
    }
    video_stream = fmt_ctx->streams[video_stream_idx];
    audio_stream_idx = find_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO);
    if (audio_stream_idx >= 0)
        audio_stream = fmt_ctx->streams[audio_stream_idx];

    err = open_decoder(fmt_ctx, video_stream_idx, &video_codec_ctx);
    if (err < 0) {
        show_ffmpeg_error_message("open video decoder failed", err);
        goto fail;
    }

    if (audio_stream != NULL) {
        err = open_decoder(fmt_ctx, audio_stream_idx, &audio_codec_ctx);
        if (err < 0) {
            show_ffmpeg_error_message("open audio decoder failed", err);
            goto fail;
        }

        err = init_audio_playback(&audio, audio_codec_ctx);
        if (err < 0) {
            show_ffmpeg_error_message("init audio playback failed", err);
            goto fail;
        }
    }

    video_frame = av_frame_alloc();
    audio_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (video_frame == NULL || audio_frame == NULL || rgba_frame == NULL || packet == NULL) {
        show_error_message("mp4player", "ffmpeg alloc failed");
        goto fail;
    }

    rgba_buf = (uint8_t*)av_malloc((size_t)av_image_get_buffer_size(
        AV_PIX_FMT_BGRA, video_codec_ctx->width, video_codec_ctx->height, 1));
    if (rgba_buf == NULL) {
        show_error_message("mp4player", "av_malloc failed");
        goto fail;
    }

    err = av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buf,
                               AV_PIX_FMT_BGRA, video_codec_ctx->width, video_codec_ctx->height, 1);
    if (err < 0) {
        show_ffmpeg_error_message("av_image_fill_arrays failed", err);
        goto fail;
    }

    window = SDL_CreateWindow("mp4player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              video_codec_ctx->width, video_codec_ctx->height, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        show_error_message("SDL_CreateWindow failed", SDL_GetError());
        goto fail;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) {
        show_error_message("SDL_CreateRenderer failed", SDL_GetError());
        goto fail;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                video_codec_ctx->width, video_codec_ctx->height);
    if (texture == NULL) {
        show_error_message("SDL_CreateTexture failed", SDL_GetError());
        goto fail;
    }

    delay_ms = frame_delay_ms(video_stream);

    while (running) {
        uint64_t begin_us = perf_now_us();
        err = av_read_frame(fmt_ctx, packet);
        perf.read_us += perf_now_us() - begin_us;
        if (err < 0)
            break;

        if (!handle_events()) {
            running = 0;
            break;
        }

        if (packet->stream_index == video_stream_idx) {
            begin_us = perf_now_us();
            err = avcodec_send_packet(video_codec_ctx, packet);
            perf.video_send_us += perf_now_us() - begin_us;
            av_packet_unref(packet);
            if (err < 0) {
                print_ffmpeg_error("send video packet failed", err);
                break;
            }

            err = drain_video_decoder(video_codec_ctx, video_frame, &sws_ctx, renderer,
                                      texture, rgba_frame, video_stream, &video_clock,
                                      delay_ms, &running, &perf);
            if (err < 0) {
                print_ffmpeg_error("receive video frame failed", err);
                break;
            }
            continue;
        }

        if (audio.enabled && packet->stream_index == audio_stream_idx) {
            begin_us = perf_now_us();
            err = avcodec_send_packet(audio_codec_ctx, packet);
            perf.audio_send_us += perf_now_us() - begin_us;
            av_packet_unref(packet);
            if (err < 0) {
                print_ffmpeg_error("send audio packet failed", err);
                break;
            }

            err = drain_audio_decoder(audio_codec_ctx, audio_frame, &audio, &perf);
            if (err < 0) {
                print_ffmpeg_error("receive audio frame failed", err);
                break;
            }
            continue;
        }

        av_packet_unref(packet);
    }

    avcodec_send_packet(video_codec_ctx, NULL);
    while (running) {
        err = drain_video_decoder(video_codec_ctx, video_frame, &sws_ctx, renderer,
                                  texture, rgba_frame, video_stream, &video_clock,
                                  delay_ms, &running, &perf);
        if (err == 0)
            break;
        if (err < 0) {
            print_ffmpeg_error("flush video failed", err);
            break;
        }
    }

    if (audio.enabled) {
        avcodec_send_packet(audio_codec_ctx, NULL);
        err = drain_audio_decoder(audio_codec_ctx, audio_frame, &audio, &perf);
        if (err < 0 && err != AVERROR_EOF && err != AVERROR(EAGAIN))
            print_ffmpeg_error("flush audio failed", err);

        while (running && audio_queue_size(&audio.queue) > 0) {
            running = handle_events();
            SDL_Delay(20);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&video_frame);
    av_frame_free(&audio_frame);
    av_frame_free(&rgba_frame);
    av_free(rgba_buf);
    sws_freeContext(sws_ctx);
    cleanup_audio_playback(&audio);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&fmt_ctx);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;

fail:
    av_packet_free(&packet);
    av_frame_free(&video_frame);
    av_frame_free(&audio_frame);
    av_frame_free(&rgba_frame);
    av_free(rgba_buf);
    sws_freeContext(sws_ctx);
    cleanup_audio_playback(&audio);
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&fmt_ctx);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
}

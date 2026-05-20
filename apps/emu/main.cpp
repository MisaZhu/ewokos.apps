/*
 *   This file is part of nes_emu.
 *   Copyright (c) 2019 Franz Flasch.
 *
 *   nes_emu is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   nes_emu is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with nes_emu.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/keydef.h>
#include <ewoksys/klog.h>
#include <ewoksys/timer.h>
#include <ewoksys/vfs.h>
#include <x++/X.h>

#include "src/InfoNES_Types.h"
#include "src/InfoNES.h"
#include "src/InfoNES_System.h"
#include "src/InfoNES_pAPU.h"

#include <Widget/Widget.h>
#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <WidgetEx/Menubar.h>
#include <WidgetEx/FileDialog.h>
#include <graph/graph_image.h>

// PCM Audio Driver
#define CTRL_PCM_DEV_HW         (0xF0)
#define CTRL_PCM_DEV_PRPARE     (0xF2)
#define CTRL_PCM_BUF_AVAIL      (0xF3)

struct pcm_config {
    int bit_depth;
    int rate;
    int channels;
    int period_size;
    int period_count;
    int start_threshold;
    int stop_threshold;
};

struct pcm_t {
    int fd;
    int prepared;
    int running;
    char name[32];
    int framesize;
    struct pcm_config config;
};

static int support_rate(unsigned int rate) {
    switch (rate) {
        case 8000:
        case 16000:
        case 32000:
        case 44100:
        case 48000:
        case 96000:
            return 1;
    }
    return 0;
}

static int support_channels(unsigned int channels) {
    if (channels != 2) {
        return 0;
    }
    return 1;
}

static int support_bit_depth(unsigned int bit_depth) {
    switch (bit_depth) {
        case 16:
        case 24:
        case 32:
            return 1;
        default:
            return 0;
    }
}

static int is_valid_config(struct pcm_config *config)
{
    if (!support_bit_depth(config->bit_depth) ||
        !support_channels(config->channels) ||
        !support_rate(config->rate)) {
        return 0;
    }
    if (config->period_size == 0 || config->period_count == 0) {
        return 0;
    }
    if (config->start_threshold == 0) {
        config->start_threshold = config->period_size;
    }
    if (config->stop_threshold == 0) {
        config->stop_threshold = config->period_size * config->period_count;
    }
    return 1;
}

static int pcm_param_set(struct pcm_t *pcm, struct pcm_config *config) {
    proto_t in, out;
    PF->init(&in)->add(&in, config, sizeof(struct pcm_config));
    PF->init(&out);
    int ret = 0;
    ret = dev_cntl(pcm->name, CTRL_PCM_DEV_HW, &in, &out);
    if(ret == 0) {
        ret = proto_read_int(&out);
    }
    PF->clear(&in);
    PF->clear(&out);
    return ret;
}

static struct pcm_t* pcm_open(const char *name, struct pcm_config *config)
{
    if (!is_valid_config(config)) {
        return NULL;
    }

    struct pcm_t* pcm = (struct pcm_t*)calloc(1, sizeof(struct pcm_t));
    if (pcm == NULL) return NULL;

    strncpy(pcm->name, name, 31);
    memcpy(&pcm->config, config, sizeof(struct pcm_config));
    pcm->framesize = config->channels * config->bit_depth / 8;

    pcm->fd = open(name, O_RDWR);
    if (pcm->fd < 0) {
        free(pcm);
        return NULL;
    }

    if (pcm_param_set(pcm, &pcm->config) != 0) {
        close(pcm->fd);
        free(pcm);
        return NULL;
    }

    return pcm;
}

static int pcm_prepare(struct pcm_t *pcm) {
    if (pcm->prepared) return 0;

    proto_t in, out;
    PF->init(&in);
    PF->init(&out);
    int ret = dev_cntl(pcm->name, CTRL_PCM_DEV_PRPARE, &in, &out);
    if(ret == 0) {
        ret = proto_read_int(&out);
    }
    PF->clear(&in);
    PF->clear(&out);

    if (ret == 0) pcm->prepared = 1;
    return ret;
}

static int pcm_buf_avail(struct pcm_t *pcm)
{
    proto_t in, out;
    PF->init(&in);
    PF->init(&out);
    int ret = 0;
    ret = dev_cntl(pcm->name, CTRL_PCM_BUF_AVAIL, &in, &out);
    if(ret == 0) {
        ret = proto_read_int(&out);
    }
    PF->clear(&in);
    PF->clear(&out);
    return ret;
}

static int pcm_try_write(struct pcm_t *pcm, const void* data, unsigned int count) {
    if (count == 0) return 0;

    if (pcm->running == 0) {
        int err = pcm_prepare(pcm);
        if (err != 0) {
            return err;
        }

        int written = write(pcm->fd, data, count);
        if (written != (int)count) {
            return -1;
        }
        pcm->running = 1;
        return 0;
    }

    int ret = write(pcm->fd, data, count);
    return (ret == (int)count ? 0 : -1);
}

static int wait_avail(struct pcm_t *pcm, int *avail, int time_out_ms)
{
    enum {
        SLEEP_TIME_MS = 5,
    };
    *avail = 0;
    int ret = 0;
    int period_bytes = pcm->config.period_size * 4; // 16-bit stereo = 4 bytes per frame
    int max_try_count = time_out_ms / SLEEP_TIME_MS;
    int try_count = 0;

    for(;;) {
        ret = pcm_buf_avail(pcm);
        if (ret < 0) {
            break;
        }

        if (ret >= period_bytes) {
            *avail = ret;
            break;
        }

        if(try_count++ >= max_try_count) {
            break;
        }

        proc_usleep(SLEEP_TIME_MS * 1000);
    }

    return ret;
}

static int pcm_write(struct pcm_t *pcm, const void* data, unsigned int count) {
    if (count == 0) return 0;

    int period_bytes = pcm->config.period_size * 4; // 16-bit stereo = 4 bytes per frame
    int avail = 0;
    int bytes = (int)count;
    int written = 0;
    int offset = 0;
    int copy_bytes = 0;
    int ret = 0;

    copy_bytes = bytes < period_bytes ? bytes : period_bytes;
    while (bytes > 0) {
        ret = wait_avail(pcm, &avail, 2000); // Wait up to 2 seconds
        if (ret < 0 || (avail == 0 && bytes > 0)) {
            break;
        }

        copy_bytes = bytes < avail ? bytes : avail;

        ret = pcm_try_write(pcm, (const char*)data + offset, copy_bytes);
        if (ret == 0) {
            offset += copy_bytes;
            written += copy_bytes;
            bytes -= copy_bytes;
            copy_bytes = bytes < period_bytes ? bytes : period_bytes;
        }
    }

    return (written == (int)count ? 0 : -1);
}

static int pcm_close(struct pcm_t *pcm) {
    if (pcm == NULL) return 0;
    close(pcm->fd);
    free(pcm);
    return 0;
}

static struct pcm_t* pcmDev = NULL;
static int pcmSampleRate = 44100;

// Audio buffer for NES APU output - sized for 2 frames
#define AUDIO_BUFFER_SAMPLES (735 * 2)  // 2 frames of NES audio
static int16_t audioBuffer[AUDIO_BUFFER_SAMPLES * 2]; // Stereo buffer
static int audioBufferPos = 0;

#define AUDIO_RING_SAMPLES (AUDIO_BUFFER_SAMPLES * 8)
static int16_t audioRing[AUDIO_RING_SAMPLES * 2];
static int audioRingReadPos = 0;
static int audioRingWritePos = 0;
static int audioRingUsed = 0;
static pthread_t audioThread;
static pthread_mutex_t audioLock;
static bool audioLockInited = false;
static bool audioThreadCreated = false;
static bool audioThreadExit = false;
static pthread_mutex_t frameLock;
static bool frameLockInited = false;

#define EMU_FRAME_USEC 16667ULL
#define EMU_MAX_CATCHUP_FRAMES 3
#define EMU_RESYNC_LAG_USEC (EMU_FRAME_USEC * 6)

static uint64_t now_usec(void) {
    uint64_t usec = 0;
    kernel_tic(NULL, &usec);
    return usec;
}

static void frame_sync_init(void) {
    if (!frameLockInited) {
        pthread_mutex_init(&frameLock, NULL);
        frameLockInited = true;
    }
}

static void audio_sync_init(void) {
    if (!audioLockInited) {
        pthread_mutex_init(&audioLock, NULL);
        audioLockInited = true;
    }
}

static void audio_ring_reset_locked(void) {
    audioRingReadPos = 0;
    audioRingWritePos = 0;
    audioRingUsed = 0;
}

static void audio_drop_oldest_locked(int frames) {
    if (frames <= 0 || audioRingUsed == 0) {
        return;
    }

    if (frames >= audioRingUsed) {
        audio_ring_reset_locked();
        return;
    }

    audioRingReadPos = (audioRingReadPos + frames) % AUDIO_RING_SAMPLES;
    audioRingUsed -= frames;
}

static void audio_enqueue_frames_locked(const int16_t* data, int frames) {
    if (data == NULL || frames <= 0) {
        return;
    }

    if (frames >= AUDIO_RING_SAMPLES) {
        data += (frames - AUDIO_RING_SAMPLES) * 2;
        frames = AUDIO_RING_SAMPLES;
        audio_ring_reset_locked();
    }

    int freeFrames = AUDIO_RING_SAMPLES - audioRingUsed;
    if (frames > freeFrames) {
        audio_drop_oldest_locked(frames - freeFrames);
    }

    int firstFrames = AUDIO_RING_SAMPLES - audioRingWritePos;
    if (firstFrames > frames) {
        firstFrames = frames;
    }

    memcpy(&audioRing[audioRingWritePos * 2], data, firstFrames * 2 * (int)sizeof(int16_t));

    int remainingFrames = frames - firstFrames;
    if (remainingFrames > 0) {
        memcpy(audioRing, data + firstFrames * 2, remainingFrames * 2 * (int)sizeof(int16_t));
    }

    audioRingWritePos = (audioRingWritePos + frames) % AUDIO_RING_SAMPLES;
    audioRingUsed += frames;
}

static int audio_dequeue_frames_locked(int16_t* out, int maxFrames) {
    if (out == NULL || maxFrames <= 0 || audioRingUsed <= 0) {
        return 0;
    }

    int frames = audioRingUsed < maxFrames ? audioRingUsed : maxFrames;
    int firstFrames = AUDIO_RING_SAMPLES - audioRingReadPos;
    if (firstFrames > frames) {
        firstFrames = frames;
    }

    memcpy(out, &audioRing[audioRingReadPos * 2], firstFrames * 2 * (int)sizeof(int16_t));

    int remainingFrames = frames - firstFrames;
    if (remainingFrames > 0) {
        memcpy(out + firstFrames * 2, audioRing, remainingFrames * 2 * (int)sizeof(int16_t));
    }

    audioRingReadPos = (audioRingReadPos + frames) % AUDIO_RING_SAMPLES;
    audioRingUsed -= frames;
    return frames;
}

static void* audio_thread_entry(void* arg) {
    (void)arg;
    int16_t localBuf[AUDIO_BUFFER_SAMPLES * 2];

    for (;;) {
        int frames = 0;
        bool shouldExit = false;

        pthread_mutex_lock(&audioLock);
        frames = audio_dequeue_frames_locked(localBuf, AUDIO_BUFFER_SAMPLES);
        shouldExit = audioThreadExit && (audioRingUsed == 0);
        pthread_mutex_unlock(&audioLock);

        if (frames > 0) {
            if (pcmDev != NULL) {
                pcm_write(pcmDev, localBuf, frames * 4);
            }
            continue;
        }

        if (shouldExit) {
            break;
        }

        proc_usleep(2000);
    }

    return NULL;
}

static void audio_thread_stop(void) {
    if (!audioThreadCreated) {
        return;
    }

    pthread_mutex_lock(&audioLock);
    audioThreadExit = true;
    pthread_mutex_unlock(&audioLock);

    pthread_join(audioThread, NULL);
    audioThreadCreated = false;
}

static void audio_thread_start(void) {
    audio_sync_init();

    pthread_mutex_lock(&audioLock);
    audioThreadExit = false;
    audio_ring_reset_locked();
    pthread_mutex_unlock(&audioLock);

    if (pthread_create(&audioThread, NULL, audio_thread_entry, NULL) == 0) {
        audioThreadCreated = true;
    }
}

using namespace Ewok;
   /* NES part */

#define KEY_TIMEOUT 2	
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

WORD padState;

DWORD RGBPalette[64] = {
	0xff707070,0xff201888,0xff0000a8,0xff400098,0xff880070,0xffa80010,0xffa00000,0xff780800,
	0xff402800,0xff004000,0xff005000,0xff003810,0xff183858,0xff000000,0xff000000,0xff000000,
	0xffb8b8b8,0xff0070e8,0xff2038e8,0xff8000f0,0xffb800b8,0xffe00058,0xffd82800,0xffc84808,
	0xff887000,0xff009000,0xff00a800,0xff009038,0xff008088,0xff000000,0xff000000,0xff000000,
	0xfff8f8f8,0xff38b8f8,0xff5890f8,0xff4088f8,0xfff078f8,0xfff870b0,0xfff87060,0xfff89838,
	0xfff0b838,0xff80d010,0xff48d848,0xff58f898,0xff00e8d8,0xff000000,0xff000000,0xff000000,
	0xfff8f8f8,0xffa8e0f8,0xffc0d0f8,0xffd0c8f8,0xfff8c0f8,0xfff8c0d8,0xfff8b8b0,0xfff8d8a8,
	0xfff8e0a0,0xffe0f8a0,0xffa8f0b8,0xffb0f8c8,0xff98f8f0,0xff000000,0xff000000,0xff000000,
};

WORD NesPalette[ 64 ] =
{
	0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xa,0xb,0xc,0xd,0xe,0xf,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f
};

graph_t *paint;

/* Menu screen */
int InfoNES_Menu(){
	return 0;
}

/* Read ROM image file */
int InfoNES_ReadRom( const char *pszFileName ){
 
  FILE *fp;

  /* Open ROM file */
  fp=fopen(pszFileName,"rb");
  if(fp==NULL) return -1;

  /* Read ROM Header */
  fread( &NesHeader, 1, sizeof NesHeader, fp );
  if( memcmp( NesHeader.byID, "NES\x1a", 4 )!=0){
    /* not .nes file */
    fclose( fp );
    return -1;
  }
  /* Clear SRAM */
  memset( SRAM, 0, SRAM_SIZE );

  /* If trainer presents Read Triner at 0x7000-0x71ff */
  if(NesHeader.byInfo1 & 4){
    fread( &SRAM[ 0x1000 ], 1, 512, fp );
  }
  
  /* Allocate Memory for ROM Image */
  ROM = (BYTE *)malloc( NesHeader.byRomSize * 0x4000 );

  /* Read ROM Image */
  int ret = fread( ROM, 1, NesHeader.byRomSize * 0x4000, fp );

  if(NesHeader.byVRomSize>0){
    /* Allocate Memory for VROM Image */
    VROM = (BYTE *)malloc( NesHeader.byVRomSize * 0x2000 );

    /* Read VROM Image */
    ret = fread( VROM, 1, NesHeader.byVRomSize * 0x2000, fp );
  }

  /* File close */
  fclose( fp );

  /* Successful */
  return 0;
}

/* Release a memory for ROM */
void InfoNES_ReleaseRom(){

}

static float scale = 1.0;
void graph_scale_fix_center(graph_t *src, graph_t *dst, const grect_t& r){
	if(dst->h < dst->w)
		scale = (float)r.h / (float)src->h;
	else
		scale = (float)r.w / (float)src->w;

    graph_t* sc = graph_scalef_fast(src, scale);
    if(sc == NULL)
        return;

	int sx = MAX((r.w- src->w * scale)/2, 0) + r.x;
	int sy = MAX((r.h - src->h * scale)/2, 0) + r.y;
    graph_blt(sc, 0, 0, sc->w, sc->h, dst, sx, sy, sc->w, sc->h);
    graph_free(sc);
}

void InfoNES_LoadFrame(){
    frame_sync_init();
    pthread_mutex_lock(&frameLock);
	WORD* s = WorkFrame;
	uint32_t* d=(uint32_t *)paint->buffer;
	for(int i= 0; i < NES_DISP_WIDTH*NES_DISP_HEIGHT; i++ ){
		int idx = *s++ % 64;
		d[i] = RGBPalette[idx];
	}
    pthread_mutex_unlock(&frameLock);
}

/* Get a joypad state */
void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem ){
	*pdwPad1 = padState;
}

/* memcpy */
void *InfoNES_MemoryCopy( void *dest, const void *src, int count ){
	return memcpy(dest, src, count);
}


/* memset */
void *InfoNES_MemorySet( void *dest, int c, int count ){
	return memset(dest, 0, count);
}

/* Print debug message */
void InfoNES_DebugPrint( char *pszMsg ){

}

/* Wait */
void InfoNES_Wait(){

}

/* Sound Initialize */
void InfoNES_SoundInit( void ){

}

/* Sound Open */
int InfoNES_SoundOpen( int samples_per_sync, int sample_rate ){
    (void)samples_per_sync;

    audio_sync_init();

    // Close existing PCM device if any
    audio_thread_stop();
    if (pcmDev != NULL) {
        pcm_close(pcmDev);
        pcmDev = NULL;
    }

    pcmSampleRate = sample_rate;

    // Reset audio buffer
    audioBufferPos = 0;
    pthread_mutex_lock(&audioLock);
    audio_ring_reset_locked();
    pthread_mutex_unlock(&audioLock);

    // Open PCM device for NES audio output
    // NES APU output is mono, we convert to stereo
    struct pcm_config config;
    memset(&config, 0, sizeof(config));
    config.bit_depth = 16;
    config.rate = sample_rate;
    config.channels = 2;  // Stereo output
    config.period_size = 1024;  // Smaller period for faster response
    config.period_count = 4;   // More periods for stability
    config.start_threshold = 1024 * 2;  // Start after 2 periods
    config.stop_threshold = 0;  // Let driver auto-calculate

    pcmDev = pcm_open("/dev/sound0", &config);
    if (pcmDev == NULL) {
        printf("Failed to open PCM device\n");
        return -1;
    }

    audio_thread_start();
    return 0;
}

/* Sound Close */
void InfoNES_SoundClose( void ){
    audio_sync_init();

    if (audioBufferPos > 0) {
        if (audioThreadCreated) {
            pthread_mutex_lock(&audioLock);
            audio_enqueue_frames_locked(audioBuffer, audioBufferPos);
            pthread_mutex_unlock(&audioLock);
        } else if (pcmDev != NULL) {
            pcm_write(pcmDev, audioBuffer, audioBufferPos * 4);
        }
        audioBufferPos = 0;
    }

    audio_thread_stop();

    if (pcmDev != NULL) {
        pcm_close(pcmDev);
        pcmDev = NULL;
    }

    pthread_mutex_lock(&audioLock);
    audio_ring_reset_locked();
    pthread_mutex_unlock(&audioLock);
}

/* Sound Output 5 Waves - 2 Pulse, 1 Triangle, 1 Noise, 1 DPCM */
void InfoNES_SoundOutput(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5){
    if (pcmDev == NULL) return;

    if (samples > 735) samples = 735;

    // Accumulate samples into buffer
    for (int i = 0; i < samples; i++) {
        // Mix all 5 channels (convert from unsigned 8-bit to signed)
        // NES APU outputs range from 0-255, center at 128
        int sample = 0;
        if (wave1) sample += (int)wave1[i] - 128;
        if (wave2) sample += (int)wave2[i] - 128;
        if (wave3) sample += (int)wave3[i] - 128;
        if (wave4) sample += (int)wave4[i] - 128;
        if (wave5) sample += (int)wave5[i] - 128;

        // Scale to 16-bit: NES max output ~128 * 5 channels = 640
        // Scale to fit in 16-bit range: 640 * 51 = 32640 (close to 32767)
        sample = sample * 51;

        // Clip to 16-bit range
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        // Stereo output (same sample for left and right)
        audioBuffer[audioBufferPos * 2] = (int16_t)sample;
        audioBuffer[audioBufferPos * 2 + 1] = (int16_t)sample;
        audioBufferPos++;

        // When buffer is full, write to PCM device
        if (audioBufferPos >= AUDIO_BUFFER_SAMPLES) {
            if (audioThreadCreated) {
                pthread_mutex_lock(&audioLock);
                audio_enqueue_frames_locked(audioBuffer, AUDIO_BUFFER_SAMPLES);
                pthread_mutex_unlock(&audioLock);
            } else {
                pcm_write(pcmDev, audioBuffer, AUDIO_BUFFER_SAMPLES * 4);
            }
            audioBufferPos = 0;
        }
    }
}

class NesEmu : public Widget {
    bool loaded;
    graph_t* logo; 
    pthread_t emuThread;
    bool emuThreadCreated;
    bool emuThreadExit;

    static void* emuThreadEntry(void* arg) {
        ((NesEmu*)arg)->emuLoop();
        return NULL;
    }

    void emuLoop() {
        uint64_t nextFrameUsec = now_usec();

        while (!emuThreadExit) {
            uint64_t now = now_usec();
            if (now + 1000 < nextFrameUsec) {
                proc_usleep((uint32_t)(nextFrameUsec - now));
                continue;
            }

            int catchupFrames = 0;
            while (!emuThreadExit && now >= nextFrameUsec && catchupFrames < EMU_MAX_CATCHUP_FRAMES) {
                InfoNES_Cycle();
                nextFrameUsec += EMU_FRAME_USEC;
                catchupFrames++;
                now = now_usec();
            }

            if (now > (nextFrameUsec + EMU_RESYNC_LAG_USEC)) {
                nextFrameUsec = now + EMU_FRAME_USEC;
            } else if (catchupFrames == 0) {
                proc_yield();
            }
        }
    }

    void stopEmuThread() {
        if (!emuThreadCreated) {
            return;
        }
        emuThreadExit = true;
        pthread_join(emuThread, NULL);
        emuThreadCreated = false;
    }

    void startEmuThread() {
        emuThreadExit = false;
        if (pthread_create(&emuThread, NULL, emuThreadEntry, this) == 0) {
            emuThreadCreated = true;
        }
    }

public:
	inline NesEmu() {
        logo = NULL;
        loaded = false;
        emuThreadCreated = false;
        emuThreadExit = false;
		padState = 0;
        frame_sync_init();
		paint = graph_new(NULL, 256, 240);
	}
	
	inline ~NesEmu() {
        stopEmuThread();
        if (loaded) {
            InfoNES_Fin();
            loaded = false;
        }
        if(paint)
    		graph_free(paint);

        if(logo)
            graph_free(logo);
	}

    bool loadGame(const char* path){
        stopEmuThread();
        if (loaded) {
            InfoNES_Fin();
            loaded = false;
        }

		int i = InfoNES_Load(path);
        if(i != 0) {
            loaded = false;
            return false;
        }
		InfoNES_Init();
        loaded = true;
        startEmuThread();
		return true;
    } 

protected:
    bool onIM(xevent_t* ev) {
        if(ev->state == XIM_STATE_PRESS){
            switch(ev->value.im.value){
                case JOYSTICK_A:
                case ']':
                    padState |= 0x1;
                    break;
                case JOYSTICK_B:
                case '[':
                    padState |= 0x2;
                    break;
                case 's':
                case JOYSTICK_SELECT:
                    padState |= 0x4;
                    break;
                case JOYSTICK_START:
                case KEY_ENTER:
                    padState |= 0x8;
                    break;
                case KEY_UP:
                    padState |= 0x10;
                    break;
                case KEY_DOWN:
                    padState |= 0x20;
                    break;
                case KEY_LEFT:
                    padState |= 0x40;
                    break;
                case KEY_RIGHT:
                    padState |= 0x80;
                    break;
                default:
                    break;
            }
        }else{
            switch(ev->value.im.value){
                case JOYSTICK_A:
                case ']':
                    padState &= ~0x1;
                    break;
                case JOYSTICK_B:
                case '[':
                    padState &= ~0x2;
                    break;
                case 's':
                case JOYSTICK_SELECT:
                    padState &= ~0x4;
                    break;
                case JOYSTICK_START:
                case KEY_ENTER:
                    padState &= ~0x8;
                    break;
                case KEY_UP:
                    padState &= ~0x10;
                    break;
                case KEY_DOWN:
                    padState &= ~0x20;
                    break;
                case KEY_LEFT:
                    padState &= ~0x40;
                    break;
                case KEY_RIGHT:
                    padState &= ~0x80;
                    break;
                default:
                    break;
            }
		}
        return true;
	}

    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        if(!loaded) {
            graph_fill_rect(g, r.x, r.y, r.w, r.h, theme->basic.bgColor);
            if(!logo)
                logo = graph_image_new(X::getResFullName("logo.png").c_str());
            if(logo) {
                int x = r.x + (r.w - logo->w) / 2;
                int y = r.y + (r.h - logo->h) / 2;
                graph_blt_alpha(logo, 0, 0, logo->w, logo->h, g, x, y, logo->w, logo->h, 0xff);
            }
            return;
        }

        if (!emuThreadCreated) {
            InfoNES_Cycle();
        }

        frame_sync_init();
        pthread_mutex_lock(&frameLock);
        graph_scale_fix_center(paint, g, r);
        pthread_mutex_unlock(&frameLock);
	}

    void onTimer(uint32_t timerFPS, uint32_t timerStep) {
        update();
    }
};

class PlayerWin : public WidgetWin {
    FileDialog fdialog;
    NesEmu* emu;
protected:
    void onDialoged(XWin* from, int res, void* arg) {
        if (res == Dialog::RES_OK && from == &fdialog) {
            string path = fdialog.getResult();
            if (path.length() > 0 && emu != NULL) {
                // Load game
                emu->loadGame(path.c_str());
            }
        }
    }

public:
    PlayerWin() {
        emu = NULL;
        fdialog.setInitPath(X::getResFullName("roms"));
    }

    inline void setEmu(NesEmu* emu) {
        this->emu = emu;
    }

    ~PlayerWin() {
    }

    FileDialog* getFileDialog() { return &fdialog; }
};

static void onOpenFunc(MenuItem* it, void* p) {
    PlayerWin* win = (PlayerWin*)p;
    win->getFileDialog()->popup(win, 320, 240, "files", XWIN_STYLE_NORMAL);
}

int main(int argc, char *argv[]) {
	string path;
	NesEmu *emu = new NesEmu();

	//init emulator
	if(argc < 2){
		path = X::getResFullName("roms/nes1200in1.nes");
	}else{
		path = argv[1];
	}

	/*if(emu->loadGame((char*)path.c_str()) != true){
        printf("Error load rom file:%s\n", path.c_str());
        delete emu;
        return -1;
	}
    */

    X x;
    PlayerWin win;
    win.setEmu(emu);

    RootWidget* root = win.getRoot();
    root->setType(Container::VERTICAL);

    Menubar* menubar = new Menubar();
    root->add(menubar);
    menubar->fix(0, 24);
    menubar->setItemSize(50);
    menubar->add(0, "Open", NULL, NULL, onOpenFunc, &win);

    root->add(emu);
    root->focus(emu);

	scale = 1.0;
    win.open(&x, -1, -1, -1, 256*scale, 240*scale+24, "NesEmu", XWIN_STYLE_NORMAL);
    win.setTimer(90);
    widgetXRun(&x, &win);
    delete emu;
	return 0;
}

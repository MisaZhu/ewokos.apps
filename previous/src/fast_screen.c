/*
  Hatari - screen.c

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.

 (SC) Simon Schubiger - most of it rewritten for Previous NeXT emulator
*/

#include <SDL.h>
#include <SDL_endian.h>
#include <SDL_blendmode.h>

/* EwokOS: smooth letterboxed scaling of the logical emulator frame into
 * the (possibly WM-clamped) window, macemu video_xwin.cpp style */
#include <graph/graph.h>

const char Screen_fileid[] = "Previous fast_screen.c : " __DATE__ " " __TIME__;

#include "configuration.h"
#include "log.h"
#include "m68000.h"
#include "dimension.h"
#include "paths.h"
#include "screen.h"
#include "control.h"
#include "statusbar.h"
#include "sdlgui.h"
#include "video.h"
#include "kms.h"

SDL_Window*   sdlWindow;
SDL_Surface*  sdlscrn = NULL;   /* The SDL screen surface */
int nScreenZoomX, nScreenZoomY; /* Zooming factors, used for scaling mouse motions */

/* extern for shortcuts */
volatile bool bGrabMouse    = false; /* Grab the mouse cursor in the window */
volatile bool bInFullScreen = false; /* true if in full screen */

static const int NeXT_SCRN_WIDTH  = 1120;
static const int NeXT_SCRN_HEIGHT = 832;

static SDL_Thread*   repaintThread;
static SDL_sem*      initLatch;
static SDL_atomic_t  blitFB;
static SDL_atomic_t  blitUI;           /* When value == 1, the repaint thread will blit the sldscrn surface to the screen on the next redraw */
static bool          doUIblit;
static SDL_Rect      saveWindowBounds; /* Window bounds before going fullscreen. Used to restore window size & position. */
static void*         uiBuffer;         /* UI (status bar + dialogs) with mask pixels made transparent */
static void*         uiBufferTmp;      /* Snapshot of uiBuffer consumed by the repainter */
static Uint32*       fbBuf;            /* NeXT framebuffer converted to ARGB8888 */
static Uint32*       compositeBuf;     /* fb + UI merged, input of the letterbox scaler */
static SDL_SpinLock  uiBufferLock;     /* Lock for concurrent access to UI buffer between m68k thread and repainter */
static Uint32        mask;             /* green screen mask for transparent UI areas */
static volatile bool doRepaint  = true; /* Repaint thread runs while true */
static SDL_Rect      statusBar;

/* EwokOS: letterbox geometry of the logical frame inside the window
 * (macemu view_scale/view_offset).  Written by the repaint thread in
 * presentFrame(), read by the event thread's mouse mapping; the window
 * size is fixed after creation, so a stale read is benign. */
static volatile float viewScale = 1.0f;
static volatile int   viewOffX  = 0;
static volatile int   viewOffY  = 0;


static Uint32 BW2RGB[0x400];
static Uint32 COL2RGB[0x10000];

/* EwokOS: the composite frame is a plain ARGB8888 buffer consumed by
 * the graph library, so the lookup tables map straight to 0xAARRGGBB */
static Uint32 bw2rgb(int bw) {
    switch(bw & 3) {
        case 3:  return 0xFF000000;
        case 2:  return 0xFF555555;
        case 1:  return 0xFFAAAAAA;
        case 0:  return 0xFFFFFFFF;
        default: return 0;
    }
}

static Uint32 col2rgb(int col) {
    int r = col & 0xF000; r >>= 12; r |= r << 4;
    int g = col & 0x0F00; g >>= 8;  g |= g << 4;
    int b = col & 0x00F0; b >>= 4;  b |= b << 4;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/*
 BW format is 2bit per pixel
 */
static void blitBW(Uint32* dst) {
    int   pitch = (NeXT_SCRN_WIDTH + (ConfigureParams.System.bTurbo ? 0 : 32)) / 4;
    for(int y = 0; y < NeXT_SCRN_HEIGHT; y++) {
        int src     = y * pitch;
        for(int x = 0; x < NeXT_SCRN_WIDTH/4; x++, src++) {
            int idx = NEXTVideo[src] * 4;
            *dst++  = BW2RGB[idx+0];
            *dst++  = BW2RGB[idx+1];
            *dst++  = BW2RGB[idx+2];
            *dst++  = BW2RGB[idx+3];
        }
    }
}

/*
 Color format is 4bit per pixel, big-endian: RGBx
 */
static void blitColor(Uint32* dst) {
    int pitch = NeXT_SCRN_WIDTH + (ConfigureParams.System.bTurbo ? 0 : 32);
    for(int y = 0; y < NeXT_SCRN_HEIGHT; y++) {
        Uint16* src = (Uint16*)NEXTColorVideo + (y*pitch);
        for(int x = 0; x < NeXT_SCRN_WIDTH; x++) {
            *dst++ = COL2RGB[*src++];
        }
    }
}

/*
 Dimension format is 8bit per pixel, big-endian: RRGGBBAA
 (nd_sdl.c keeps its own texture-based blitDimension() for the
 DUAL-monitor NeXTdimension window; on EwokOS only this single-window
 path runs) */
static void blitDimensionMain(Uint32* dst) {
#if ND_STEP
    Uint32* src = (Uint32*)&ND_vram[0];
#else
    Uint32* src = (Uint32*)&ND_vram[16];
#endif
    for(int y = NeXT_SCRN_HEIGHT; --y >= 0;) {
        for(int x = NeXT_SCRN_WIDTH; --x >= 0;) {
            // guest stores RRGGBBAA big-endian; a LE word read yields
            // AABBGGRR -> convert to AARRGGBB
            Uint32 v = *src++;
            *dst++   = (v & 0xFF000000) | ((v<<16) &0x00FF0000) | (v &0x0000FF00) | ((v>>16) &0x000000FF);
        }
        src += 32;
    }
}

/*
 EwokOS: link stub for the DUAL-monitor NeXTdimension window repaint
 (nd_sdl.c).  The SDL driver here is single-window, so that second
 window never displays; the real dimension frame goes through
 blitDimensionMain() below.
 */
void blitDimension(SDL_Texture* tex) {
    (void)tex;
}

/*
 Blit NeXT framebuffer to the composite source buffer.
 */
static void blitScreen(Uint32* dst) {
    if (ConfigureParams.Screen.nMonitorType==MONITOR_TYPE_DIMENSION) {
        blitDimensionMain(dst);
        return;
    }
    if(ConfigureParams.System.bColor) {
        blitColor(dst);
    } else {
        blitBW(dst);
    }
}

static void uiUpdate(void); /* defined below, used to prime the overlay */

/*
 EwokOS: merge the NeXT framebuffer with the UI overlay (status bar +
 dialogs).  uiUpdate()/statusBarUpdate() turned the green-screen mask
 pixels into fully transparent ones, so per pixel it is a plain select,
 exactly what the renderer's alpha blend produced before.
 */
static void compositeFrame(Uint32* comp, const Uint32* fb, const Uint8* ui,
                           int w, int h, int uiPitch) {
    for(int y = 0; y < h; y++) {
        const Uint32* urow = (const Uint32*)(ui + (size_t)y * uiPitch);
        const Uint32* frow = fb + (size_t)y * w;
        Uint32*       crow = comp + (size_t)y * w;
        for(int x = 0; x < w; x++) {
            Uint32 u = urow[x];
            crow[x] = (u & 0xFF000000) ? u : frow[x];
        }
    }
}

/*
 Initializes SDL graphics and then enters repaint loop.
 Loop: Blits the NeXT framebuffer, blends in the GUI surface and scales
 the logical frame into the window with the graph library (smooth
 letterboxing like macemu's xwin backend).
 */
static int repainter(void* unused) {
    int width;
    int height;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
    /* EwokOS: the window manager may clamp the window smaller than
     * requested (e.g. desktop 1280x800 minus titlebar). All buffers
     * use the logical emulator size; the repaint loop letterboxes the
     * logical frame onto the actual window with graph_scale_tof_fast().
     * Writing NeXT_SCRN_HEIGHT rows into a buffer sized after the
     * clamped window would overflow the heap. */
    width  = NeXT_SCRN_WIDTH;
    height = NeXT_SCRN_HEIGHT + Statusbar_GetHeight();

    statusBar.x = 0;
    statusBar.y = NeXT_SCRN_HEIGHT;
    statusBar.w = width;
    statusBar.h = height - NeXT_SCRN_HEIGHT;

    /* fixed ARGB8888 (AARRGGBB) masks, matching the graph library */
    Uint32 r = 0x00FF0000, g = 0x0000FF00, b = 0x000000FF, a = 0xFF000000;
    mask = g | a;
    sdlscrn     = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, r, g, b, a);
    if (!sdlscrn) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }
    /* calloc: the first composite runs before the guest has produced a
     * frame; uninitialised fbBuf would show through the transparent UI */
    uiBuffer    = calloc(1, sdlscrn->h * sdlscrn->pitch);
    uiBufferTmp = calloc(1, sdlscrn->h * sdlscrn->pitch);
    fbBuf       = calloc(1, (size_t)width * height * 4);
    compositeBuf= calloc(1, (size_t)width * height * 4);
    if (!uiBuffer || !uiBufferTmp || !fbBuf || !compositeBuf) {
        fprintf(stderr, "screen buffer malloc failed (%d x %d)\n",
                width, height);
        SDL_Quit();
        exit(-2);
    }
    // clear UI with mask
    SDL_FillRect(sdlscrn, NULL, mask);
    
    /* Exit if we can not open a screen */
    if (!sdlscrn) {
        fprintf(stderr, "Could not set video mode:\n %s\n", SDL_GetError() );
        SDL_Quit();
        exit(-2);
    }
    
    if (!bInFullScreen) {
        /* re-embed the new SDL window */
        Control_ReparentWindow(width, height, bInFullScreen);
    }
    
    Statusbar_Init(sdlscrn);
    
    /* EwokOS: prime the UI overlay so the first composite picks up the
     * status bar (also sets blitUI so it reaches the repaint loop) */
    uiUpdate();
    
	if (bGrabMouse) {
		SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
    }

	/* Configure some SDL stuff: */
	/* EwokOS: keep the host cursor visible here; the repaint loop below
	 * hides it only after NeXTSTEP has booted (boot ROM / bootloader run
	 * with the PC inside the ROM, so they need the cursor) */
	SDL_ShowCursor(SDL_ENABLE);
    
    /* Setup lookup tables */
    /* initialize BW lookup table */
    for(int i = 0; i < 0x100; i++) {
        BW2RGB[i*4+0] = bw2rgb(i>>6);
        BW2RGB[i*4+1] = bw2rgb(i>>4);
        BW2RGB[i*4+2] = bw2rgb(i>>2);
        BW2RGB[i*4+3] = bw2rgb(i>>0);
    }
    /* initialize color lookup table */
    for(int i = 0; i < 0x10000; i++)
        COL2RGB[SDL_BYTEORDER == SDL_BIG_ENDIAN ? i : SDL_Swap16(i)] = col2rgb(i);

    /* the window surface is backed directly by the xwin shm graph */
    SDL_Surface* winSurf = SDL_GetWindowSurface(sdlWindow);
    if (!winSurf) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }

    /* Initialization done -> signal */
    SDL_SemPost(initLatch);
    
    /* Start with framebuffer blit enabled */
    SDL_AtomicSet(&blitFB, 1);
    
    /* Letterbox scaling cache (refilled by graph_scale_tof_fast()) */
    graph_t* scaled = NULL;
    int   lastGW = -1, lastGH = -1, lastSW = -1, lastSH = -1;
    float lastScale = -1.0f;
    Uint32 lastPresent = 0;

    /* EwokOS: hide the host cursor only once NeXTSTEP has taken over.
     * While the boot ROM / bootloader runs, regs.pc stays inside the ROM
     * and the host cursor must stay visible (also un-grabbed: SDL relative
     * mouse mode hides the cursor unconditionally). The guest OS mouse
     * driver announces itself by putting the mouse into the KMS polling
     * mask; shortly after that WindowServer draws the guest cursor, so
     * that is when the host cursor goes away (the guest draws its own). */
    Uint32 osMouseSince = 0;
    Uint32 romSince = 0;
    bool   hostCursorHidden = false;

    /* Enter repaint loop */
    while(doRepaint) {
        Uint32 now = SDL_GetTicks();

        /* EwokOS: host cursor follows the guest boot state */
        {
            uaecptr pc = regs.pc;
            /* ROM is mapped at $00000000 and mirrored at $01000000 (128kB) */
            bool inRom = (pc < 0x00020000u) ||
                         (pc >= 0x01000000u && pc < 0x01020000u);
            /* guest OS mouse driver alive: mouse in the KMS polling mask
             * while executing outside the ROM */
            bool osMouse = !inRom && kms_mouse_enabled();

            /* a single pc sample that lands in the ROM range (e.g. an
             * exception vector fetch) must not flicker the cursor back
             * on: require ~200ms of consecutive ROM samples */
            if (inRom) {
                if (romSince == 0) romSince = now ? now : 1;
            } else {
                romSince = 0;
            }
            bool romStable = (romSince != 0) && (now - romSince >= 200);

            if (romStable) {
                osMouseSince = 0;
                if (hostCursorHidden) {
                    hostCursorHidden = false;
                    SDL_ShowCursor(SDL_ENABLE);
                }
                /* relative mode hides the host cursor; keep it off until
                 * the guest OS has taken over */
                if (SDL_GetRelativeMouseMode()) {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    SDL_SetWindowGrab(sdlWindow, SDL_FALSE);
                }
            } else if (osMouse) {
                if (osMouseSince == 0) {
                    osMouseSince = now ? now : 1;
                } else if (!hostCursorHidden && now - osMouseSince >= 3000) {
                    hostCursorHidden = true;
                    SDL_ShowCursor(SDL_DISABLE);
                }
            }
        }

        if (now - lastPresent < 16) { /* ~60fps present cap */
            host_sleep_ms(2);
            continue;
        }

        bool updateFB = SDL_AtomicGet(&blitFB) != 0;
        bool updateUI = false;

        if (updateFB) {
            // Convert the NeXT framebuffer to ARGB8888
            blitScreen(fbBuf);
        }

        // Snapshot the UI overlay
        SDL_AtomicLock(&uiBufferLock);
        if(SDL_AtomicSet(&blitUI, 0)) {
            memcpy(uiBufferTmp, uiBuffer, sdlscrn->h * sdlscrn->pitch);
            updateUI = true;
        }
        SDL_AtomicUnlock(&uiBufferLock);

        if (updateFB || updateUI) {
            /* refetch: the shm buffer may have been rebuilt on resize */
            winSurf = SDL_GetWindowSurface(sdlWindow);
            if (winSurf && winSurf->pixels) {
                compositeFrame(compositeBuf, fbBuf, (const Uint8*)uiBufferTmp,
                               width, height, sdlscrn->pitch);

                graph_t cg;
                graph_init(&cg, compositeBuf, width, height);
                graph_t wg;
                graph_init(&wg, (const uint32_t*)winSurf->pixels,
                           winSurf->w, winSurf->h);

                /* letterbox geometry, macemu on_xwin_repaint() style */
                float scaleX = (float)wg.w / width;
                float scaleY = (float)wg.h / height;
                float scale  = (scaleX < scaleY) ? scaleX : scaleY;
                if (scale < 0.5f) scale = 0.5f;
                int scaledW = (int)(width * scale);
                int scaledH = (int)(height * scale);
                int offX = (wg.w - scaledW) / 2;
                int offY = (wg.h - scaledH) / 2;
                viewScale = scale;
                viewOffX  = offX;
                viewOffY  = offY;

                if (lastGW != wg.w || lastGH != wg.h || lastScale != scale) {
                    /* window geometry changed: clear the letterbox once */
                    graph_fill_rect(&wg, 0, 0, wg.w, wg.h, 0xFF000000);
                    lastGW = wg.w; lastGH = wg.h; lastScale = scale;
                }

                if (scale != 1.0f) {
                    if (scaled == NULL || lastSW != scaledW || lastSH != scaledH) {
                        graph_t* tmp = graph_new(NULL, scaledW, scaledH);
                        if (scaled != NULL)
                            graph_free(scaled);
                        scaled = tmp;
                        lastSW = scaledW; lastSH = scaledH;
                    }
                    if (scaled != NULL && scaled->buffer != NULL) {
                        graph_scale_tof_fast(&cg, scaled, scale);
                        graph_blt(scaled, 0, 0, scaledW, scaledH,
                                  &wg, offX, offY, scaledW, scaledH);
                    } else {
                        /* cache alloc failed: let graph_blt() scale */
                        graph_blt(&cg, 0, 0, width, height,
                                  &wg, offX, offY, scaledW, scaledH);
                    }
                } else {
                    graph_blt(&cg, 0, 0, width, height,
                              &wg, offX, offY, width, height);
                }
                SDL_UpdateWindowSurface(sdlWindow);
                lastPresent = SDL_GetTicks();
            }
        } else {
            host_sleep_ms(10);
        }
    }
    if (scaled != NULL)
        graph_free(scaled);
    return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Pause Screen, pauses or resumes drawing of NeXT framebuffer
 */
void Screen_Pause(bool pause) {
    if (pause) {
        SDL_AtomicSet(&blitFB, 0);
    } else {
        SDL_AtomicSet(&blitFB, 1);
    }
}

/*-----------------------------------------------------------------------*/
/**
 * Init Screen, creates window and starts repaint thread
 */
void Screen_Init(void) {
    /* Set initial window resolution
     * EwokOS: always run fullscreen, the desktop is too small for the
     * 1120x856 window and the WM would clamp it anyway */
    bInFullScreen = true;
    nScreenZoomX  = 1;
    nScreenZoomY  = 1;

    int width  = NeXT_SCRN_WIDTH;
    int height = NeXT_SCRN_HEIGHT;
    
    /* Statusbar height */
    height += Statusbar_SetHeight(width, height);
    
    if (bInFullScreen) {
        /* unhide the WM window for fullscreen */
        Control_ReparentWindow(width, height, bInFullScreen);
    }
    
    /* Set new video mode: the repaint loop scales the logical frame
     * into the window with the graph library (smooth letterboxing) */
    
    fprintf(stderr, "SDL screen request: %d x %d (%s)\n", width, height, bInFullScreen ? "fullscreen" : "windowed");
    
    int x = SDL_WINDOWPOS_UNDEFINED;
    if(ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_DUAL) {
        for(int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
            SDL_Rect r;
            SDL_GetDisplayBounds(i, &r);
            if(r.w >= width * 2) {
                x = r.x + width + ((r.w - width * 2) / 2);
                break;
            }
            if(r.x >= 0 && SDL_GetNumVideoDisplays() == 1) x = r.x + 8;
        }
    }
    sdlWindow  = SDL_CreateWindow(PROG_NAME, x, SDL_WINDOWPOS_UNDEFINED, width, height,
                                  bInFullScreen ? SDL_WINDOW_FULLSCREEN : 0);
    if (!sdlWindow) {
        fprintf(stderr,"Failed to create window: %s!\n", SDL_GetError());
        exit(-1);
    }

    initLatch     = SDL_CreateSemaphore(0);
    repaintThread = SDL_CreateThread(repainter, "[Previous] screen repaint", NULL);
    SDL_SemWait(initLatch);
}

void nd_sdl_destroy(void);

/*-----------------------------------------------------------------------*/
/**
 * Free screen bitmap and allocated resources
 */
void Screen_UnInit(void) {
    doRepaint = false; // stop repaint thread
    int s;
    SDL_WaitThread(repaintThread, &s);
    /* EwokOS: the repaint thread is gone; free the frame buffers under
     * the UI lock so an in-flight uiUpdate()/statusBarUpdate() on the
     * m68k thread (which re-checks under this lock) cannot race us */
    SDL_AtomicLock(&uiBufferLock);
    free(fbBuf);       fbBuf = NULL;
    free(compositeBuf); compositeBuf = NULL;
    free(uiBuffer);    uiBuffer = NULL;
    free(uiBufferTmp); uiBufferTmp = NULL;
    SDL_AtomicUnlock(&uiBufferLock);
    nd_sdl_destroy();
}

/*-----------------------------------------------------------------------*/
/**
 * EwokOS: map window (host) coordinates into the logical emulator
 * coordinate space (and back) using the letterbox geometry computed by
 * the repaint thread.  macemu maps its mouse the same way
 * ((pos - view_offset) / view_scale).
 */
void Screen_WindowToLogical(int wx, int wy, int* lx, int* ly) {
    float scale = viewScale;
    if (scale <= 0.0f) scale = 1.0f;
    if (lx) *lx = (int)(((float)wx - viewOffX) / scale);
    if (ly) *ly = (int)(((float)wy - viewOffY) / scale);
}

void Screen_LogicalToWindow(int lx, int ly, int* wx, int* wy) {
    float scale = viewScale;
    if (scale <= 0.0f) scale = 1.0f;
    if (wx) *wx = (int)(lx * scale) + viewOffX;
    if (wy) *wy = (int)(ly * scale) + viewOffY;
}

float Screen_GetViewScale(void) {
    float scale = viewScale;
    return (scale > 0.0f) ? scale : 1.0f;
}

/*-----------------------------------------------------------------------*/
/**
 * Enter Full screen mode
 */
void Screen_EnterFullScreen(void) {
	bool bWasRunning;

	if (!bInFullScreen) {
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = true;

        SDL_GetWindowPosition(sdlWindow, &saveWindowBounds.x, &saveWindowBounds.y);
        SDL_GetWindowSize(sdlWindow, &saveWindowBounds.w, &saveWindowBounds.h);
        SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_Delay(20);                  /* To give monitor time to change to new resolution */
		
		if (bWasRunning) {
			/* And off we go... */
			Main_UnPauseEmulation();
		}
		SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Return from Full screen mode back to a window
 */
void Screen_ReturnFromFullScreen(void) {
	bool bWasRunning;

	if (bInFullScreen) {
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = false;

        SDL_SetWindowFullscreen(sdlWindow, 0);
		SDL_Delay(20);                /* To give monitor time to switch resolution */
        SDL_SetWindowPosition(sdlWindow, saveWindowBounds.x, saveWindowBounds.y);
        SDL_SetWindowSize(sdlWindow, saveWindowBounds.w, saveWindowBounds.h);
        
		if (bWasRunning) {
			/* And off we go... */
			Main_UnPauseEmulation();
		}

		if (!bGrabMouse) {
			/* Un-grab mouse pointer in windowed mode */
			SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_SetWindowGrab(sdlWindow, SDL_FALSE);
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Force things associated with changing between fullscreen/windowed
 */
void Screen_ModeChanged(void) {
	if (!sdlscrn) {
		/* screen not yet initialized */
		return;
	}
	if (bInFullScreen || bGrabMouse) {
		SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
	} else {
		SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_SetWindowGrab(sdlWindow, SDL_FALSE);
    }
}


/*-----------------------------------------------------------------------*/
/**
 * Draw screen to window/full-screen - (SC) Just status bar updates. Screen redraw is done in repaint thread.
 */

static bool shieldStatusBarUpdate;

static void statusBarUpdate(void) {
    if(shieldStatusBarUpdate) return;
    /* EwokOS: uiBuffer only exists while the repaint thread is up
     * (allocated late in Screen_Init, freed in Screen_UnInit); a
     * status bar update racing either edge must not crash */
    if (uiBuffer == NULL || sdlscrn == NULL)
        return;
    SDL_LockSurface(sdlscrn);
    SDL_AtomicLock(&uiBufferLock);
    if (uiBuffer != NULL)
        memcpy(&((Uint8*)uiBuffer)[statusBar.y*sdlscrn->pitch], &((Uint8*)sdlscrn->pixels)[statusBar.y*sdlscrn->pitch], statusBar.h * sdlscrn->pitch);
    SDL_AtomicSet(&blitUI, 1);
    SDL_AtomicUnlock(&uiBufferLock);
    SDL_UnlockSurface(sdlscrn);
}

bool Update_StatusBar(void) {
    shieldStatusBarUpdate = true;
    Statusbar_OverlayBackup(sdlscrn);
    Statusbar_Update(sdlscrn);
    shieldStatusBarUpdate = false;

    statusBarUpdate();
    
    return !bQuitProgram;
}

/*
 Copy UI SDL surface to uiBuffer and replace mask pixels with transparent pixels for
 UI blending with framebuffer texture.
*/
static void uiUpdate(void) {
    /* EwokOS: uiBuffer only exists while the repaint thread is up
     * (allocated late in Screen_Init, freed in Screen_UnInit); a
     * UI update racing either edge must not crash */
    if (uiBuffer == NULL || sdlscrn == NULL)
        return;
    SDL_LockSurface(sdlscrn);
    int     count = sdlscrn->w * sdlscrn->h;
    Uint32* src   = (Uint32*)sdlscrn->pixels;
    SDL_AtomicLock(&uiBufferLock);
    /* re-read under the lock: Screen_UnInit frees under this same lock */
    Uint32* dst   = (Uint32*)uiBuffer;
    if (dst != NULL) {
        // poor man's green-screen - would be nice if SDL had more blending modes...
        for(int i = count; --i >= 0; src++)
            *dst++ = *src == mask ? 0 : *src;
    }
    SDL_AtomicSet(&blitUI, 1);
    SDL_AtomicUnlock(&uiBufferLock);
    SDL_UnlockSurface(sdlscrn);
}

void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects) {
    while(numrects--) {
        if(rects->y < NeXT_SCRN_HEIGHT) {
            uiUpdate();
            doUIblit = true;
        } else {
            if(doUIblit) {
                uiUpdate();
                doUIblit = false;
            } else {
                statusBarUpdate();
            }
        }
    }
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h) {
    SDL_Rect rect = { x, y, w, h };
    SDL_UpdateRects(screen, 1, &rect);
}

/*-----------------------------------------------------------------------
 * EwokOS: "preparing disk image" splash (macemu VideoDiskCopySplash
 * equivalent).  Drawn on sdlscrn; the repaint thread picks it up via
 * uiBuffer on the next SDL_UpdateRect.  total <= 0 clears the overlay.
 */
void Screen_DiskCopySplash(int done, int total) {
    if (sdlscrn == NULL)
        return;

    if (total <= 0) {
        /* preparation done: clear the overlay, VRAM shows again */
        SDL_FillRect(sdlscrn, NULL, mask);
        SDL_UpdateRect(sdlscrn, 0, 0, 0, 0);
        return;
    }

    Uint32 bg    = SDL_MapRGB(sdlscrn->format, 0xAA, 0xAA, 0xAA);
    Uint32 black = SDL_MapRGB(sdlscrn->format, 0x00, 0x00, 0x00);
    Uint32 white = SDL_MapRGB(sdlscrn->format, 0xFF, 0xFF, 0xFF);

    SDL_Rect box = { sdlscrn->w/2 - 260, sdlscrn->h/2 - 80, 520, 160 };
    SDL_FillRect(sdlscrn, &box, bg);
    SDL_Rect edge;
    edge = (SDL_Rect){ box.x, box.y, box.w, 2 };             SDL_FillRect(sdlscrn, &edge, black);
    edge = (SDL_Rect){ box.x, box.y + box.h - 2, box.w, 2 }; SDL_FillRect(sdlscrn, &edge, black);
    edge = (SDL_Rect){ box.x, box.y, 2, box.h };             SDL_FillRect(sdlscrn, &edge, black);
    edge = (SDL_Rect){ box.x + box.w - 2, box.y, 2, box.h }; SDL_FillRect(sdlscrn, &edge, black);

    SDLGui_SetScreen(sdlscrn);
    SDLGui_Text(box.x + 20, box.y + 24, "Preparing disk image...");

    SDL_Rect bar = { box.x + 20, box.y + 96, 480, 30 };
    SDL_FillRect(sdlscrn, &bar, black);
    SDL_Rect in = { bar.x + 2, bar.y + 2, bar.w - 4, bar.h - 4 };
    SDL_FillRect(sdlscrn, &in, white);
    if (done > 0) {
        SDL_Rect fill = { in.x, in.y,
                          (int)((long long)in.w * done / total), in.h };
        SDL_FillRect(sdlscrn, &fill, black);
    }

    SDL_UpdateRect(sdlscrn, 0, 0, 0, 0);
}

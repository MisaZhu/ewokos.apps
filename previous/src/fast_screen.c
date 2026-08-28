/*
  Hatari - screen.c

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.

 (SC) Simon Schubiger - most of it rewritten for Previous NeXT emulator
*/

#include <SDL.h>
#include <SDL_endian.h>
#include <SDL_blendmode.h>

const char Screen_fileid[] = "Previous fast_screen.c : " __DATE__ " " __TIME__;

#include "main.h"
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

SDL_Window*   sdlWindow;
SDL_Surface*  sdlscrn = NULL;   /* The SDL screen surface */
int nScreenZoomX, nScreenZoomY; /* Zooming factors, used for scaling mouse motions */

/* extern for shortcuts */
volatile bool bGrabMouse    = false; /* Grab the mouse cursor in the window */
volatile bool bInFullScreen = false; /* true if in full screen */

static const int NeXT_SCRN_WIDTH  = 1120;
static const int NeXT_SCRN_HEIGHT = 832;

static SDL_Thread*   repaintThread;
static SDL_Renderer* sdlRenderer;
static SDL_sem*      initLatch;
static SDL_atomic_t  blitFB;
static SDL_atomic_t  blitUI;           /* When value == 1, the repaint thread will blit the sldscrn surface to the screen on the next redraw */
static bool          doUIblit;
static SDL_Rect      saveWindowBounds; /* Window bounds before going fullscreen. Used to restore window size & position. */
static void*         uiBuffer;         /* uiBuffer used for ui texture */
static void*         uiBufferTmp;      /* Temporary uiBuffer used by repainter */
static SDL_SpinLock  uiBufferLock;     /* Lock for concurrent access to UI buffer between m68k thread and repainter */
static Uint32        mask;             /* green screen mask for transparent UI areas */
static volatile bool doRepaint  = true; /* Repaint thread runs while true */
static SDL_Rect      statusBar;


static Uint32 BW2RGB[0x400];
static Uint32 COL2RGB[0x10000];

/* EwokOS debug: dump the initLatch semaphore and its condition variable
 * to trace heap corruption (SDL_sem: count/waiters/mutex/count_nonzero,
 * SDL_cond: lock/waiting/signals/wait_sem/wait_done) */
static void dbgDumpLatch(const char* tag) {
    Uint32* s = (Uint32*)initLatch;
    fprintf(stderr, "[latch] %s sem=%p: %08lx %08lx %08lx %08lx %08lx %08lx\n",
            tag, (void*)initLatch,
            (unsigned long)s[0], (unsigned long)s[1], (unsigned long)s[2],
            (unsigned long)s[3], (unsigned long)s[4], (unsigned long)s[5]);
    Uint64 cond = *(Uint64*)(s + 4);
    if (cond > 0x100000 && cond < 0xf0000000ULL) {
        Uint32* c = (Uint32*)(uintptr_t)cond;
        fprintf(stderr, "[latch] %s cond=%p: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                tag, (void*)(uintptr_t)cond,
                (unsigned long)c[0], (unsigned long)c[1], (unsigned long)c[2],
                (unsigned long)c[3], (unsigned long)c[4], (unsigned long)c[5],
                (unsigned long)c[6], (unsigned long)c[7]);
    }
}

static Uint32 bw2rgb(SDL_PixelFormat* format, int bw) {
    switch(bw & 3) {
        case 3:  return SDL_MapRGB(format, 0,   0,   0);
        case 2:  return SDL_MapRGB(format, 85,  85,  85);
        case 1:  return SDL_MapRGB(format, 170, 170, 170);
        case 0:  return SDL_MapRGB(format, 255, 255, 255);
        default: return 0;
    }
}

static Uint32 col2rgb(SDL_PixelFormat* format, int col) {
    int r = col & 0xF000; r >>= 12; r |= r << 4;
    int g = col & 0x0F00; g >>= 8;  g |= g << 4;
    int b = col & 0x00F0; b >>= 4;  b |= b << 4;
    return SDL_MapRGB(format, r,   g,   b);
}

/*
 BW format is 2bit per pixel
 */
static void blitBW(SDL_Texture* tex) {
    void* pixels;
    int   d;
    int   pitch = (NeXT_SCRN_WIDTH + (ConfigureParams.System.bTurbo ? 0 : 32)) / 4;
    SDL_LockTexture(tex, NULL, &pixels, &d);
    Uint32* dst = (Uint32*)pixels;
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
    SDL_UnlockTexture(tex);
}

/*
 Color format is 4bit per pixel, big-endian: RGBx
 */
static void blitColor(SDL_Texture* tex) {
    void* pixels;
    int   d;
    int pitch = NeXT_SCRN_WIDTH + (ConfigureParams.System.bTurbo ? 0 : 32);
    SDL_LockTexture(tex, NULL, &pixels, &d);
    Uint32* dst = (Uint32*)pixels;
    for(int y = 0; y < NeXT_SCRN_HEIGHT; y++) {
        Uint16* src = (Uint16*)NEXTColorVideo + (y*pitch);
        for(int x = 0; x < NeXT_SCRN_WIDTH; x++) {
            *dst++ = COL2RGB[*src++];
        }
    }
    SDL_UnlockTexture(tex);
}

/*
 Dimension format is 8bit per pixel, big-endian: RRGGBBAA
 */
void blitDimension(SDL_Texture* tex) {
#if ND_STEP
    Uint32* src = (Uint32*)&ND_vram[0];
#else
    Uint32* src = (Uint32*)&ND_vram[16];
#endif
    void*   pixels;
    int     d;
    Uint32  format;
    SDL_QueryTexture(tex, &format, &d, &d, &d);
    SDL_LockTexture(tex, NULL, &pixels, &d);
    Uint32* dst = (Uint32*)pixels;
    if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
        /* Add big-endian accelerated blit loops as needed here */
        switch (format) {
            default: {
                /* fallback to SDL_MapRGB */
                SDL_PixelFormat* pformat = SDL_AllocFormat(format);
                for(int y = NeXT_SCRN_HEIGHT; --y >= 0;) {
                    for(int x = NeXT_SCRN_WIDTH; --x >= 0;) {
                        Uint32 v = *src++;
                        *dst++   = SDL_MapRGB(pformat, (v >> 24) & 0xFF, (v>>16) & 0xFF, (v>>8) & 0xFF);
                    }
                    src += 32;
                }
                SDL_FreeFormat(pformat);
                break;
            }
        }
    } else {
        /* Add little-endian accelerated blit loops as needed here */
        switch (format) {
            case SDL_PIXELFORMAT_ARGB8888:
                for(int y = NeXT_SCRN_HEIGHT; --y >= 0;) {
                    for(int x = NeXT_SCRN_WIDTH; --x >= 0;) {
                        // Uint32 LE: AABBGGRR
                        // Target:    AARRGGBB
                        Uint32 v = *src++;
                        *dst++   = (v & 0xFF000000) | ((v<<16) &0x00FF0000) | (v &0x0000FF00) | ((v>>16) &0x000000FF);
                    }
                    src += 32;
                }
                break;
            default: {
                /* fallback to SDL_MapRGB */
                SDL_PixelFormat* pformat = SDL_AllocFormat(format);
                for(int y = NeXT_SCRN_HEIGHT; --y >= 0;) {
                    for(int x = NeXT_SCRN_WIDTH; --x >= 0;) {
                        Uint32 v = SDL_Swap32(*src++);
                        *dst++   = SDL_MapRGB(pformat, (v >> 24) & 0xFF, (v>>16) & 0xFF, (v>>8) & 0xFF);
                    }
                    src += 32;
                }
                SDL_FreeFormat(pformat);
                break;
            }
        }
    }
    SDL_UnlockTexture(tex);
}

/*
 Blit NeXT framebuffer to texture.
 */
static void blitScreen(SDL_Texture* tex) {
    if (ConfigureParams.Screen.nMonitorType==MONITOR_TYPE_DIMENSION) {
        blitDimension(tex);
        return;
    }
    if(ConfigureParams.System.bColor) {
        blitColor(tex);
    } else {
        blitBW(tex);
    }
}

/*
 Initializes SDL graphics and then enters repaint loop.
 Loop: Blits the NeXT framebuffer to the fbTexture, blends with the GUI surface and
 shows it.
 */
static int repainter(void* unused) {
    int width;
    int height;
    int winW, winH;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
    SDL_GetWindowSize(sdlWindow, &winW, &winH);
    /* EwokOS: the window manager may clamp the window smaller than
     * requested (e.g. desktop 1280x800 minus titlebar). All surfaces
     * and textures must use the logical emulator size; the renderer's
     * logical-size scaling maps it onto the actual window. Writing
     * NeXT_SCRN_HEIGHT rows into a texture sized after the clamped
     * window would overflow the heap. */
    width  = NeXT_SCRN_WIDTH;
    height = NeXT_SCRN_HEIGHT + Statusbar_GetHeight();

    statusBar.x = 0;
    statusBar.y = NeXT_SCRN_HEIGHT;
    statusBar.w = width;
    statusBar.h = height - NeXT_SCRN_HEIGHT;

    fprintf(stderr, "EWOK-TRACE: window %dx%d, logical screen %dx%d\n",
            winW, winH, width, height);
    
    SDL_Texture*  uiTexture;
    SDL_Texture*  fbTexture;
    
    Uint32 r, g, b, a;
    
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlRenderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }
    SDL_RenderSetLogicalSize(sdlRenderer, width, height);
    dbgDumpLatch("after-renderer");
    
    uiTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!uiTexture) {
        fprintf(stderr, "SDL_CreateTexture(ui) failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }
    SDL_SetTextureBlendMode(uiTexture, SDL_BLENDMODE_BLEND);
    
    fbTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!fbTexture) {
        fprintf(stderr, "SDL_CreateTexture(fb) failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }
    SDL_SetTextureBlendMode(fbTexture, SDL_BLENDMODE_NONE);
    
    Uint32 format;
    int    d;
    SDL_QueryTexture(uiTexture, &format, &d, &d, &d);
    SDL_PixelFormatEnumToMasks(format, &d, &r, &g, &b, &a);
    if (r == 0 && g == 0 && b == 0 && a == 0) {
        /* EwokOS: SDL_PixelFormatEnumToMasks() failed; use fixed masks
         * matching SDL_PIXELFORMAT_ARGB8888 (AARRGGBB on little endian) */
        a = 0xFF000000;
        r = 0x00FF0000;
        g = 0x0000FF00;
        b = 0x000000FF;
    }
    mask = g | a;
    sdlscrn     = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, r, g, b, a);
    if (!sdlscrn) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(-2);
    }
    uiBuffer    = malloc(sdlscrn->h * sdlscrn->pitch);
    uiBufferTmp = malloc(sdlscrn->h * sdlscrn->pitch);
    if (!uiBuffer || !uiBufferTmp) {
        fprintf(stderr, "uiBuffer malloc failed (%d x %d)\n",
                sdlscrn->h, sdlscrn->pitch);
        SDL_Quit();
        exit(-2);
    }
    // clear UI with mask
    SDL_FillRect(sdlscrn, NULL, mask);
    dbgDumpLatch("after-buffers");
    
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
    dbgDumpLatch("after-statusbar");
    
	if (bGrabMouse) {
		SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
    }

	/* Configure some SDL stuff: */
	SDL_ShowCursor(SDL_DISABLE);
    
    /* Setup lookup tables */
    SDL_PixelFormat* pformat = SDL_AllocFormat(format);
    /* initialize BW lookup table */
    for(int i = 0; i < 0x100; i++) {
        BW2RGB[i*4+0] = bw2rgb(pformat, i>>6);
        BW2RGB[i*4+1] = bw2rgb(pformat, i>>4);
        BW2RGB[i*4+2] = bw2rgb(pformat, i>>2);
        BW2RGB[i*4+3] = bw2rgb(pformat, i>>0);
    }
    /* initialize color lookup table */
    for(int i = 0; i < 0x10000; i++)
        COL2RGB[SDL_BYTEORDER == SDL_BIG_ENDIAN ? i : SDL_Swap16(i)] = col2rgb(pformat, i);
    
    /* Initialization done -> signal */
    dbgDumpLatch("before-post");
    SDL_SemPost(initLatch);
    
    /* Start with framebuffer blit enabled */
    SDL_AtomicSet(&blitFB, 1);
    
    /* Enter repaint loop */
    while(doRepaint) {
        bool updateFB = false;
        bool updateUI = false;
        
        if (SDL_AtomicGet(&blitFB)) {
            // Blit the NeXT framebuffer to textrue
            blitScreen(fbTexture);
            updateFB = true;
        }
        
        // Copy UI surface to texture
        SDL_AtomicLock(&uiBufferLock);
        if(SDL_AtomicSet(&blitUI, 0)) {
            // update full UI texture
            memcpy(uiBufferTmp, uiBuffer, sdlscrn->h * sdlscrn->pitch);
            updateUI = true;
        }
        SDL_AtomicUnlock(&uiBufferLock);
        
        if(updateUI) {
            SDL_UpdateTexture(uiTexture, NULL,       uiBufferTmp, sdlscrn->pitch);
        }
        
        // Update and render UI texture
        if (updateFB || updateUI) {
            SDL_RenderClear(sdlRenderer);
            // Render NeXT framebuffer texture
            SDL_RenderCopy(sdlRenderer, fbTexture, NULL, NULL);
            SDL_RenderCopy(sdlRenderer, uiTexture, NULL, NULL);
            // SDL_RenderPresent sleeps until next VSYNC because of SDL_RENDERER_PRESENTVSYNC in ScreenInit
            SDL_RenderPresent(sdlRenderer);
        } else {
            host_sleep_ms(10);
        }
    }
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
    
    /* Set new video mode */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    
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
    fprintf(stderr, "EWOK-TRACE: after SDL_CreateWindow (%p)\n", (void*)sdlWindow);
    if (!sdlWindow) {
        fprintf(stderr,"Failed to create window: %s!\n", SDL_GetError());
        exit(-1);
    }

    initLatch     = SDL_CreateSemaphore(0);
    dbgDumpLatch("created");
    repaintThread = SDL_CreateThread(repainter, "[Previous] screen repaint", NULL);
    dbgDumpLatch("before-wait");
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
    nd_sdl_destroy();
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
    SDL_LockSurface(sdlscrn);
    SDL_AtomicLock(&uiBufferLock);
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
    SDL_LockSurface(sdlscrn);
    int     count = sdlscrn->w * sdlscrn->h;
    Uint32* dst   = (Uint32*)uiBuffer;
    Uint32* src   = (Uint32*)sdlscrn->pixels;
    SDL_AtomicLock(&uiBufferLock);
    // poor man's green-screen - would be nice if SDL had more blending modes...
    for(int i = count; --i >= 0; src++)
        *dst++ = *src == mask ? 0 : *src;
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

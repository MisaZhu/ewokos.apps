/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Main_fileid[] = "Hatari main.c : " __DATE__ " " __TIME__;

#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "control.h"
#include "options.h"
#include "dialog.h"
#include "ioMem.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "paths.h"
#include "reset.h"
#include "rtcnvram.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "snd.h"
#include "statusbar.h"
#include "nextMemory.h"
#include "str.h"
#include "video.h"
#include "audio.h"
#include "debugui.h"
#include "file.h"
#include "dsp.h"
#include "host.h"
#include "dimension.h"

#include "hatari-glue.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
#endif

int nFrameSkips;

bool bQuitProgram = false;                /* Flag to quit program cleanly */

static bool bEmulationActive = true;      /* Run emulation when started */
static bool bAccurateDelays;              /* Host system has an accurate SDL_Delay()? */
static bool bIgnoreNextMouseMotion = false;  /* Next mouse motion will be ignored (needed after SDL_WarpMouse) */

volatile int mainPauseEmulation;

typedef const char* (*report_func)(double realTime, double hostTime);

typedef struct {
    const char*       label;
    const report_func report;
} report_t;

static double lastRT;
static Uint64 lastCycles;
static double speedFactor;
static char   speedMsg[32];

void Main_Speed(double realTime, double hostTime) {
    double dRT = realTime - lastRT;
    speedFactor = nCyclesMainCounter - lastCycles;
    speedFactor /= ConfigureParams.System.nCpuFreq;
    speedFactor /= 1000 * 1000;
    speedFactor /= dRT;
    lastRT     = realTime;
    lastCycles = nCyclesMainCounter;
}

void Main_SpeedReset(void) {
    double realTime, hostTime;
    host_time(&realTime, &hostTime);
    lastRT     = realTime;
    lastCycles = nCyclesMainCounter;
}

const char* Main_SpeedMsg() {
    speedMsg[0] = 0;
    if(speedFactor > 0) {
        if(ConfigureParams.System.bRealtime) {
            sprintf(speedMsg, "%dMHz/", (int)(ConfigureParams.System.nCpuFreq * speedFactor + 0.5));
        } else {
            if ((speedFactor < 0.9) || (speedFactor > 1.1))
                sprintf(speedMsg, "%.1fx%dMHz/", speedFactor, ConfigureParams.System.nCpuFreq);
            else
                sprintf(speedMsg, "%dMHz/",                   ConfigureParams.System.nCpuFreq);
        }
    }
    return speedMsg;
}

#if ENABLE_TESTING
static const report_t reports[] = {
    {"ND",    nd_reports},
    {"Host",  host_report},
};
#endif

/*-----------------------------------------------------------------------*/
/**
 * Pause emulation, stop sound.  'visualize' should be set true,
 * unless unpause will be called immediately afterwards.
 * 
 * @return true if paused now, false if was already paused
 */
bool Main_PauseEmulation(bool visualize) {
	if ( !bEmulationActive )
		return false;

	bEmulationActive = false;
    host_pause_time(!(bEmulationActive));
    Screen_Pause(true);
    Sound_Pause(true);
    if (ConfigureParams.Dimension.bEnabled) {
        dimension_pause(true);
    }
    
	if (visualize) {
		Statusbar_AddMessage("Emulation paused", 100);
		/* make sure msg gets shown */
		Statusbar_Update(sdlscrn);

		if (bGrabMouse && !bInFullScreen) {
			/* Un-grab mouse pointer in windowed mode */
			SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_SetWindowGrab(sdlWindow, SDL_FALSE);
        }
	}
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Start/continue emulation
 * 
 * @return true if continued, false if was already running
 */
bool Main_UnPauseEmulation(void) {
	if ( bEmulationActive )
		return false;

	bEmulationActive = true;
    host_pause_time(!(bEmulationActive));
    Screen_Pause(false);
    Sound_Pause(false);
    if (ConfigureParams.Dimension.bEnabled) {
        dimension_pause(false);
    }

	if (bGrabMouse) {
		/* Grab mouse pointer again */
		SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
    }
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Optionally ask user whether to quit and set bQuitProgram accordingly
 */
void Main_RequestQuit(void) {
    if (ConfigureParams.Log.bConfirmQuit) {
		bQuitProgram = false;	/* if set true, dialog exits */
		bQuitProgram = DlgAlert_Query("All unsaved data will be lost.\nDo you really want to quit?");
	}
	else {
		bQuitProgram = true;
	}

	if (bQuitProgram) {
		/* Assure that CPU core shuts down */
		M68000_SetSpecial(SPCFLAG_BRK);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Since SDL_Delay and friends are very inaccurate on some systems, we have
 * to check if we can rely on this delay function.
 */
static void Main_CheckForAccurateDelays(void) {
	int nStartTicks, nEndTicks;

	/* Force a task switch now, so we have a longer timeslice afterwards */
	SDL_Delay(10);

	nStartTicks = SDL_GetTicks();
	SDL_Delay(1);
	nEndTicks = SDL_GetTicks();

	/* If the delay took longer than 10ms, we are on an inaccurate system! */
	bAccurateDelays = ((nEndTicks - nStartTicks) < 9);

	if (bAccurateDelays)
		Log_Printf(LOG_WARN, "Host system has accurate delays. (%d)\n", nEndTicks - nStartTicks);
	else
		Log_Printf(LOG_WARN, "Host system does not have accurate delays. (%d)\n", nEndTicks - nStartTicks);
}


/* ----------------------------------------------------------------------- */
/**
 * Set mouse pointer to new coordinates and set flag to ignore the mouse event
 * that is generated by SDL_WarpMouse().
 */
void Main_WarpMouse(int x, int y) {
    int wx, wy;
    /* EwokOS: callers pass logical emulator coordinates, but the warp
     * lands in window space (the logical frame is letterbox-scaled) */
    Screen_LogicalToWindow(x, y, &wx, &wy);
    SDL_WarpMouseInWindow(sdlWindow, wx, wy); /* Set mouse pointer to new position */
	bIgnoreNextMouseMotion = true;          /* Ignore mouse motion event from SDL_WarpMouse */
}


/* ----------------------------------------------------------------------- */
/**
 * Handle mouse motion event.
 */
SDL_Event mymouse[100];
static void Main_HandleMouseMotion(SDL_Event *pEvent) {
	int dx, dy;
	int i,nb;

	dx = pEvent->motion.xrel;
	dy = pEvent->motion.yrel;

	/* get all mouse event to clean the queue and sum them */
	nb=SDL_PeepEvents(&mymouse[0], 100, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION);

	for (i=0;i<nb;i++) {
		dx += mymouse[i].motion.xrel;
		dy += mymouse[i].motion.yrel;
	}

	/* EwokOS: host pixels map to guest pixels through the letterbox
	 * scale; keep the fractional remainders so small motions at
	 * non-1:1 scales are not truncated away */
	{
		static float resX = 0.0f, resY = 0.0f;
		float vs = Screen_GetViewScale();
		float fx = dx / vs + resX;
		float fy = dy / vs + resY;
		dx = (int)fx;
		dy = (int)fy;
		resX = fx - (float)dx;
		resY = fy - (float)dy;
	}

	if (bGrabMouse) {
		Keymap_MouseMove(dx,dy,ConfigureParams.Mouse.fLinSpeedLocked,ConfigureParams.Mouse.fExpSpeedLocked);
	} else {
		Keymap_MouseMove(dx,dy,ConfigureParams.Mouse.fLinSpeedNormal,ConfigureParams.Mouse.fExpSpeedNormal);
	}
}

static int statusBarUpdate;

/* ----------------------------------------------------------------------- */
/**
 * SDL message handler.
 * Here we process the SDL events (keyboard, mouse, ...)
 */
void Main_EventHandler(void) {
    bool bContinueProcessing;
    SDL_Event event;
    int events;
    int remotepause;
    
    if(++statusBarUpdate > 400) {
        double vt;
        double rt;
        host_time(&rt, &vt);
#if ENABLE_TESTING
        fprintf(stderr, "[reports]");
        for(int i = 0; i < sizeof(reports)/sizeof(report_t); i++) {
            const char* msg = reports[i].report(rt, vt);
            if(msg[0]) fprintf(stderr, " %s:%s", reports[i].label, msg);
        }
        fprintf(stderr, "\n");
#else
        Main_Speed(rt, vt);
#endif
        Statusbar_UpdateInfo();
        statusBarUpdate = 0;
    }
    
    do {
        bContinueProcessing = false;
        
        /* check remote process control from different thread (e.g. i860) */
        switch(mainPauseEmulation) {
            case PAUSE_EMULATION:
                mainPauseEmulation = PAUSE_NONE;
                Main_PauseEmulation(true);
                break;
            case UNPAUSE_EMULATION:
                mainPauseEmulation = PAUSE_NONE;
                Main_UnPauseEmulation();
                break;
        }
        
        /* check remote process control */
        remotepause = Control_CheckUpdates();
        
        if ( bEmulationActive || remotepause ) {
            double time_offset = host_real_time_offset() * 1000;
            if(time_offset > 10)
                events = SDL_WaitEventTimeout(&event, time_offset);
            else
                events = SDL_PollEvent(&event);
        }
        else {
            ShortCut_ActKey();
            /* last (shortcut) event activated emulation? */
            if ( bEmulationActive )
                break;
            events = SDL_WaitEvent(&event);
        }
        if (!events) {
            /* no events -> if emulation is active or
             * user is quitting -> return from function.
             */
            continue;
        }
        switch (event.type) {
            case SDL_WINDOWEVENT:
                if(event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    SDL_WaitEventTimeout(&event, 100); // grab SDL_Quit if pending
                    Main_RequestQuit();
                }
                continue;

            case SDL_QUIT:
                Main_RequestQuit();
                break;
                
            case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
                Main_HandleMouseMotion(&event);
                bContinueProcessing = false;
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (ConfigureParams.Mouse.bEnableAutoGrab && !bGrabMouse) {
                        bGrabMouse = true;        /* Toggle flag */
                        
                        /* If we are in windowed mode, toggle the mouse cursor mode now: */
                        if (!bInFullScreen)
                        {
                            SDL_SetRelativeMouseMode(SDL_TRUE);
                            SDL_SetWindowGrab(sdlWindow, SDL_TRUE);
                            Main_SetTitle(MOUSE_LOCK_MSG);
                        }
                    }
                    
                    Keymap_MouseDown(true);
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    Keymap_MouseDown(false);
                }
                break;
                
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    Keymap_MouseUp(true);
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    Keymap_MouseUp(false);
                }
                break;
                
            case SDL_MOUSEWHEEL:
                Keymap_MouseWheel(&event.wheel);
                break;
                
            case SDL_KEYDOWN:
                if (event.key.repeat)
                    break;
                
                Keymap_KeyDown(&event.key.keysym);
                break;
                
            case SDL_KEYUP:
                Keymap_KeyUp(&event.key.keysym);
                break;
                
                
            default:
                /* don't let unknown events delay event processing */
                bContinueProcessing = true;
                break;
        }
    } while (bContinueProcessing || !(bEmulationActive || bQuitProgram));
}


void Main_EventHandlerInterrupt() {
    CycInt_AcknowledgeInterrupt();
    Main_EventHandler();
    CycInt_AddRelativeInterruptUs((1000*1000)/200, 0, INTERRUPT_EVENT_LOOP); // poll events with 200 Hz
    /* EwokOS CD-boot diagnosis: periodic heartbeat on the serial console,
     * so a frozen guest (still running, waiting for an interrupt that
     * never comes) can be told apart from a dead emulator process */
    {
        extern void ESP_DiagSnapshot(void);
        extern void m68k_dumpstate_2(unsigned int pc, unsigned int *nextpc);
        extern void Disasm(FILE *f, unsigned int addr, unsigned int *nextpc, int cnt, int engine);
        extern Uint32 DBGMemory_ReadLong(Uint32 addr);
        static int beats = 0;
        if (++beats >= 500) { /* every ~2.5 emulated seconds */
            unsigned int pc = (unsigned int)m68k_getpc();
            unsigned int nextpc;
            beats = 0;
            fprintf(stderr, "[HB] alive: PC=$%08x cycles=%lld\n",
                    (unsigned)pc, (long long)nCyclesMainCounter);
            ESP_DiagSnapshot();
            /* guest kernel variables (NeXTSTEP 3.3 m68k kernel symbol table):
             *  0x040B6CD8 = _time (timeval: tv_sec, tv_usec) - advances iff
             *               the hardclock interrupt reaches the guest
             *  0x040B6D1C = _need_ast[0]
             *  0x040B7410 = default_pset+0x108 (runq) - idle loop waits on it
             *  0x040C32E8 = processor_array+0x108 - second idle-loop check */
            fprintf(stderr, "[HB] guest: _time=(%u,%u) need_ast0=$%08x pset108=$%08x parr108=$%08x\n",
                    DBGMemory_ReadLong(0x040B6CD8), DBGMemory_ReadLong(0x040B6CDC),
                    DBGMemory_ReadLong(0x040B6D1C), DBGMemory_ReadLong(0x040B7410),
                    DBGMemory_ReadLong(0x040C32E8));
            /* walk the BSD allproc chain (p_nxt at +0x08, p_pid is the
             * low halfword at +0x22, p_comm at +0x11c) */
            {
                Uint32 p = DBGMemory_ReadLong(0x040B71E4); /* _allproc */
                int n = 0, j;
                while (p && n < 24) {
                    char comm[17];
                    Uint32 c;
                    for (j = 0; j < 4; j++) {
                        c = DBGMemory_ReadLong(p + 0x11c + j*4);
                        comm[j*4+0] = (c>>24)&0xff; comm[j*4+1] = (c>>16)&0xff;
                        comm[j*4+2] = (c>>8)&0xff;  comm[j*4+3] = c&0xff;
                    }
                    comm[16] = 0;
                    for (j = 0; j < 16; j++) if (!comm[j]) break;
                    comm[j] = 0;
                    fprintf(stderr, "[HB] proc @$%08x pid=%d comm=%s\n", p,
                            (int)(DBGMemory_ReadLong(p + 0x20) & 0xFFFF), comm);
                    p = DBGMemory_ReadLong(p + 0x08); /* p_nxt */
                    n++;
                }
            }
            /* dump default_pset: it contains the threads queue head (the
             * complete thread list on a UP system) - offsets get decoded
             * offline, then every thread's wait_event can be listed */
            {
                int i;
                fprintf(stderr, "[HB] pset @0x040B7308:\n");
                for (i = 0; i < 0x140; i += 16) {
                    fprintf(stderr, "  +%03x: %08x %08x %08x %08x\n", i,
                            DBGMemory_ReadLong(0x040B7308+i), DBGMemory_ReadLong(0x040B7308+i+4),
                            DBGMemory_ReadLong(0x040B7308+i+8), DBGMemory_ReadLong(0x040B7308+i+12));
                }
            }
            /* dump guest registers (SR shows the interrupt mask) and
             * disassemble the code at PC, so a guest spin loop reveals
             * what it is waiting for */
            m68k_dumpstate_2(pc, &nextpc);
            fprintf(stderr, "[HB] code at PC:\n");
            Disasm(stderr, pc, &nextpc, 8, 0 /* DISASM_ENGINE_UAE */);
            fflush(stderr);
        }
    }
}

/*-----------------------------------------------------------------------*/
/**
 * Set Hatari window title. Use NULL for default
 */
void Main_SetTitle(const char *title) {
    if (title)
        SDL_SetWindowTitle(sdlWindow, title);
    else
        SDL_SetWindowTitle(sdlWindow, PROG_NAME);
}

/*-----------------------------------------------------------------------*/
/**
 * Initialise emulation
 */
static void Main_Init(void) {
	/* Open debug log file */
	if (!Log_Init()) {
		fprintf(stderr, "Logging/tracing initialization failed\n");
		exit(-1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");
	/* EwokOS: unique build tag, so the running binary can be identified
	 * (the __DATE__/__TIME__ above only changes when main.c is rebuilt) */
	Log_Printf(LOG_INFO, "ewokos build: std-mouse-v13\n");

	/* Init SDL's video subsystem. Note: Audio and joystick subsystems
	   will be initialized later (failures there are not fatal). */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | Opt_GetNoParachuteFlag()) < 0)
	{
		fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
		exit(-1);
	}
	SDLGui_Init();
	Screen_Init();
	Main_SetTitle(NULL);

	/* EwokOS: window is visible now; copy/unzip the pending bundled
	 * disk images into the user dir (with splash), then mount the
	 * user copies bootable-first onto the free SCSI targets, all
	 * before the SCSI layer opens the drives in Reset_Cold() below */
	Ewok_PrepareUserDisks();
	Ewok_AssignDiskTargets();

	DSP_Init();
	M68000_Init();                /* Init CPU emulation */
	Keymap_Init();

    /* call menu at startup */
    if (!File_Exists(sConfigFileName) || ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup) {
        Dialog_DoProperty();
        if (bQuitProgram) {
            SDL_Quit();
            exit(-2);
        }
    }

    Dialog_CheckFiles();
    
    if (bQuitProgram) {
        SDL_Quit();
        exit(-2);
    }
    
    Reset_Cold();
    
	IoMem_Init();
	
    /* Start EventHandler */
    CycInt_AddRelativeInterruptUs(500*1000, 0, INTERRUPT_EVENT_LOOP);
    
	/* done as last, needs CPU & DSP running... */
	DebugUI_Init();
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialise emulation
 */
static void Main_UnInit(void) {
	nvram_save();                 /* EwokOS: persist guest NVRAM (BOM settings) */
	Screen_ReturnFromFullScreen();
	IoMem_UnInit();
	SDLGui_UnInit();
	Screen_UnInit();
	Exit680x0();

	/* SDL uninit: */
	SDL_Quit();

	/* Close debug log file */
	Log_UnInit();
}


/*-----------------------------------------------------------------------*/
/**
 * Load initial configuration file(s)
 */
static void Main_LoadInitialConfig(void) {
	char *psGlobalConfig;

	psGlobalConfig = malloc(FILENAME_MAX);
	if (psGlobalConfig)
	{
#if defined(__AMIGAOS4__)
		strncpy(psGlobalConfig, CONFDIR"previous.cfg", FILENAME_MAX);
#else
		snprintf(psGlobalConfig, FILENAME_MAX, CONFDIR"%cprevious.cfg", PATHSEP);
#endif
		/* Try to load the global configuration file */
		Configuration_Load(psGlobalConfig);

		free(psGlobalConfig);
	}

	/* Now try the users configuration file */
	Configuration_Load(NULL);
}

/*-----------------------------------------------------------------------*/
/**
 * Set TOS etc information and initial help message
 */
static void Main_StatusbarSetup(void) {
	const char *name = NULL;
	SDL_Keycode key;

	key = ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS];
	if (!key)
		key = ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS];
	if (key)
		name = SDL_GetKeyName(key);
	if (name)
	{
		char message[24], *keyname;
#ifdef _MUDFLAP
		__mf_register(name, 32, __MF_TYPE_GUESS, "SDL keyname");
#endif
		keyname = Str_ToUpper(strdup(name));
		snprintf(message, sizeof(message), "Press %s for Options", keyname);
		free(keyname);

		Statusbar_AddMessage(message, 6000);
	}
	/* update information loaded by Main_Init() */
	Statusbar_UpdateInfo();
}

#ifdef WIN32
	extern void Win_OpenCon(void);
#endif

/*-----------------------------------------------------------------------*/
/**
 * Set signal handlers to catch signals
 */
static void Main_SetSignalHandlers(void) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    signal(SIGFPE, SIG_IGN);
}


/*-----------------------------------------------------------------------*/
/**
 * Main
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[]) {
	/* EwokOS: unbuffered console output, so a hanging guest cannot
	 * swallow the boot diagnostics sitting in a stdio buffer */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	/* EwokOS: make malloc thread-safe before SDL spawns its threads */
	Ewok_EnableHeapLock();

	/* Generate random seed */
	srand(time(NULL));
    
    /* Set signal handlers */
    Main_SetSignalHandlers();

	/* Initialize directory strings */
	Paths_Init(argv[0]);

	/* Set default configuration values: */
	Configuration_SetDefault();

	/* Now load the values from the configuration file */
	Main_LoadInitialConfig();

	/* EwokOS: stage the bundled ROMs into the writable user dir
	 * (<home>/.previous/roms) and point the config at the user copies,
	 * so the "missing ROM" dialog does not appear on every startup */
	Ewok_FixAssetPaths();

	/* EwokOS: emulate a Turbo machine: its Rev_3.3_v74 ROM accepts
	 * CD-ROM (INQUIRY type 5) boot devices, the non-Turbo v66 ROM
	 * rejects them with "SCSI error" */
	Ewok_ConfigureMachine();

	/* EwokOS: scan the bundled/user disks and record the missing
	 * user copies as pending (extracted once the window is visible,
	 * then mounted bootable-first onto the free SCSI targets) */
	Ewok_AutoMountDisks();
    
#if 0 /* FIXME: This sometimes causes exits when starting from application bundles */
	/* Check for any passed parameters */
	if (!Opt_ParseParameters(argc, (const char * const *)argv))
	{
		return 1;
	}
#endif
	/* monitor type option might require "reset" -> true */
	Configuration_Apply(true);

#ifdef WIN32
	Win_OpenCon();
#endif

	/* Needed on maemo but useful also with normal X11 window managers
	 * for window grouping when you have multiple Hatari SDL windows open
	 */
#if HAVE_SETENV
	setenv("SDL_VIDEO_X11_WMCLASS", "previous", 1);
#endif

	/* Init emulator system */
	Main_Init();

	/* Set initial Statusbar information */
	Main_StatusbarSetup();
	
	/* Check if SDL_Delay is accurate */
	Main_CheckForAccurateDelays();


	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */


	/* Un-init emulation system */
	Main_UnInit();

	return 0;
}

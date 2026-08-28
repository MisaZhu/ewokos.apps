/*
 *  ewok_compat.c - libc shims for functions missing on EwokOS
 */

#include <stdio.h>
#include <stdbool.h>
#include <ewoksys/proc.h>

/* fputc() is not in the EwokOS libc, map it onto putc() */
int fputc(int c, FILE *stream)
{
	return putc(c, stream);
}

/*
 * EwokOS turns the libc heap lock (_proc_global_need_lock) on only from
 * libewoksys thread_create(). SDL creates its threads through
 * pthread_create(), so an SDL app that mallocs from more than one thread
 * (main thread + screen repaint thread) corrupts the heap. Enable the
 * lock while the process is still single-threaded.
 */
void Ewok_EnableHeapLock(void)
{
	proc_malloc_lock_prepare();
	_proc_global_need_lock = true;
}

/*
 * The DSP debug UI is not part of this port, but src/debug references
 * these hooks when ENABLE_DSP_EMU is set (see debug/debug_priv.h).
 * dbgcommand_t is only passed as a pointer, so an opaque forward
 * declaration is sufficient.
 */
struct dbgcommand_opaque;
typedef struct dbgcommand_opaque dbgcommand_t;

int DebugDsp_Init(const dbgcommand_t **table)
{
	(void)table;
	return 0;
}

void DebugDsp_InitSession(void)
{
}

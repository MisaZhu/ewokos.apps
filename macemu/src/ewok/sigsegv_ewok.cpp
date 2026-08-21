/*
 *  sigsegv_ewok.cpp - SIGSEGV handling stub for EwokOS
 *
 *  EwokOS does not deliver POSIX-style fault signals with register
 *  contexts, so the upstream signal-based SIGSEGV machinery cannot be
 *  used. Install a no-op handler: the banked memory model does not
 *  rely on fault-driven screen updates (VOSF is disabled).
 */

#include "sysdeps.h"
#include "sigsegv.h"

bool sigsegv_install_handler(sigsegv_fault_handler_t handler)
{
	return true;
}

void sigsegv_uninstall_handler(void)
{
}

void sigsegv_set_dump_state(sigsegv_state_dumper_t handler)
{
}

sigsegv_address_t sigsegv_get_fault_address(sigsegv_info_t *sip)
{
	return (sigsegv_address_t)sip->addr;
}

sigsegv_address_t sigsegv_get_fault_instruction_address(sigsegv_info_t *sip)
{
	return (sigsegv_address_t)sip->pc;
}

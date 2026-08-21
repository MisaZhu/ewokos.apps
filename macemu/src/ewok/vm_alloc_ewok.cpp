/*
 *  vm_alloc_ewok.cpp - malloc-based virtual memory allocation for EwokOS
 *
 *  EwokOS has no mmap()/mprotect(), so this replaces the upstream
 *  CrossPlatform/vm_alloc.cpp with a plain heap allocator. Only the
 *  banked memory model is supported with this backend.
 */

#include "sysdeps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm_alloc.h"

#define VM_PAGE_SIZE 4096

int vm_init(void)
{
	return 0;
}

void vm_exit(void)
{
}

void *vm_acquire(size_t size, int options)
{
	// round up to page size and allocate zero-filled memory
	size = (size + VM_PAGE_SIZE - 1) & ~(size_t)(VM_PAGE_SIZE - 1);
	void *addr = calloc(1, size);
	if (addr == NULL)
		return VM_MAP_FAILED;
	return addr;
}

int vm_acquire_fixed(void *addr, size_t size, int options)
{
	// fixed-address allocation is not possible without mmap
	return -1;
}

int vm_release(void *addr, size_t size)
{
	if (addr == NULL || addr == VM_MAP_FAILED)
		return -1;
	free(addr);
	return 0;
}

int vm_protect(void *addr, size_t size, int prot)
{
	// no memory protection support, pretend it worked
	return 0;
}

int vm_get_write_watch(void *addr, size_t size,
					   void **pages, unsigned int *n_pages,
					   int options)
{
	return -1;
}

int vm_reset_write_watch(void *addr, size_t size)
{
	return -1;
}

int vm_get_page_size(void)
{
	return VM_PAGE_SIZE;
}

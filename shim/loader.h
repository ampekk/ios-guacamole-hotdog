#pragma once
#include <stdint.h>

/* Boot the SWEETXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb);

/*
 * Several payloads in one ramdisk (tools/mktripleboot.py): each one starts on a 4 KiB boundary
 * after the end of the one before. loader_count: how many there are; loader_boot_n: boot the
 * n-th (0 = the first, the one loader_boot boots).
 */
int loader_count(uintptr_t dtb);
const char *loader_boot_n(uintptr_t dtb, int n);

/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The guest pages pending GPU results cover, counted per page, so a read or write of guest memory finds out with one
   pass over its pages whether any result is pending there, instead of checking every result. Both Eden (MemoryTracker,
   one bit per page, de7e6e01) and Ryujinx (region tracking, a2c00350) key what the GPU wrote by guest address and
   translate a GPU range once, when it is made: GPU addresses that alias the same guest pages meet without more work */
#ifndef SNX_VK_PENDING_PAGES_H
#define SNX_VK_PENDING_PAGES_H
#include "common/cpu/aarch64/cpu.h"

/* Every guest page under 2^40 in a two-level table: leaves of 4096 pages made when a result first covers one, each
   with a count per page, the pages of each group of 64 that have one, and its pages in all, so a lookup skips what no
   result covers. Zeroed is empty. Not locked: the caller serializes (vk_lock) */
typedef struct PendLeaf PendLeaf;
typedef struct {
    PendLeaf* leaf[1u << 16];
    u64 total; /* every page's count added up */
    u64 far;   /* pages past 2^40 results cover, read as covering every page */
} PendPages;
/* the guest pages behind [gva, gva + size) (a GPU range, translated through the GMMU now) gain a result (delta 1) or
   lose one (-1). A result loses its pages with the mappings it gained them with: a mapping change lands every result
   over it first */
void pend_add(PendPages* t, u64 gva, u64 size, int delta);
/* nonzero when a result covers a guest page behind [gva, gva + size) */
int pend_hit(const PendPages* t, u64 gva, u64 size);
/* nonzero when no result covers any page: once every result is removed, each with the mappings it was added with */
int pend_empty(const PendPages* t);

#endif

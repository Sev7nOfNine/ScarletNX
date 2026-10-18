/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The guest pages pending compute results cover (vk/pending_pages.c), over a stand-in GMMU: GPU ranges that alias the
   same guest pages meet, unmapped GPU pages cover nothing, and a result removed leaves the pages others cover */
#include "nx/gpu/vk/pending_pages.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/runtime/platform/log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* GPU page -> guest address | 1, for the GPU pages under 1 GiB */
#define GPU_PAGES (1u << 18)
static u64 gmmu[GPU_PAGES];

static void map(u64 gva, u64 cpu, u64 size) {
    for (u64 p = gva >> 12; p < (gva + size) >> 12; p++, cpu += 0x1000) gmmu[p] = cpu | 1;
}
static void unmap(u64 gva, u64 size) {
    for (u64 p = gva >> 12; p < (gva + size) >> 12; p++) gmmu[p] = 0;
}
/* as nvdrv.c's: the bytes from gva on that map contiguously, at most max, and where they start */
u64 gmmu_run(u64 gva, u64 max, u64* cpu) {
    u64 page = gva >> 12, n, want;
    if (!max || page >= GPU_PAGES || !(gmmu[page] & 1)) return 0;
    *cpu = (gmmu[page] & ~0xFFFull) | (gva & 0xFFF);
    n = 0x1000 - (gva & 0xFFF);
    want = (gmmu[page] & ~0xFFFull) + 0x1000;
    while (n < max && ++page < GPU_PAGES && gmmu[page] == (want | 1)) {
        n += 0x1000;
        want += 0x1000;
    }
    return n < max ? n : max;
}
void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

static int failures;
static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main(void) {
    PendPages* t = calloc(1, sizeof(PendPages));
    if (!t) return 1;
    map(0x100000, 0x80000000ull, 0x10000);   /* 16 pages */
    map(0x400000, 0x80004000ull, 0x2000);    /* aliases pages 4 and 5 of the range above */
    map(0x600000, 0x90000000ull, 0x4000000); /* 64 MiB: crosses table leaves (16 MiB of guest pages each) */
    check(!pend_hit(t, 0x100000, 0x10000) && pend_empty(t), "an empty table covers nothing");

    pend_add(t, 0x104000, 0x2000, 1);
    check(pend_hit(t, 0x105FFF, 1), "the last byte of a result's range");
    check(!pend_hit(t, 0x106000, 0x1000) && !pend_hit(t, 0x100000, 0x4000), "the pages beside it");
    check(pend_hit(t, 0x401000, 4), "a GPU range that aliases its guest pages");
    check(!pend_empty(t), "a result's pages are counted");

    pend_add(t, 0x400000, 0x1000, 1); /* the same guest page through the alias */
    pend_add(t, 0x104000, 0x2000, -1);
    check(pend_hit(t, 0x104000, 1) && !pend_hit(t, 0x105000, 1), "removing one result leaves another's page");
    pend_add(t, 0x400000, 0x1000, -1);
    check(pend_empty(t) && !pend_hit(t, 0x100000, 0x10000), "every result removed leaves nothing");

    /* a hole in the GPU range: the unmapped page covers nothing, the pages after it do */
    unmap(0x108000, 0x1000);
    pend_add(t, 0x107000, 0x3000, 1);
    check(!pend_hit(t, 0x108000, 0x1000) && pend_hit(t, 0x109000, 1), "a result over an unmapped GPU page");
    pend_add(t, 0x107000, 0x3000, -1);
    check(pend_empty(t), "and its removal");

    /* a large range: the first group and leaf are skipped, a page at the far end is found */
    pend_add(t, 0x600000 + 0x3FFF000, 0x1000, 1);
    check(pend_hit(t, 0x600000, 0x4000000), "the last page of 64 MiB");
    check(!pend_hit(t, 0x600000, 0x3FFF000), "64 MiB just before it");
    check(pend_hit(t, 0x600000 + 0x3FC0000, 0x40000) && !pend_hit(t, 0x600000 + 0x3F80000, 0x3F000),
          "the group of 64 pages it is in, and the one before");
    pend_add(t, 0x600000 + 0x3FFF000, 0x1000, -1);

    /* past the table's 2^40 bytes of guest pages: read as everywhere until removed */
    map(0x200000, 0x20000000000ull, 0x1000);
    pend_add(t, 0x200000, 0x1000, 1);
    check(pend_hit(t, 0x600000, 0x1000), "a page past 2^40 covers every page");
    pend_add(t, 0x200000, 0x1000, -1);
    check(pend_empty(t) && !pend_hit(t, 0x600000, 0x1000), "until it is removed");

    /* a range to the top of the address space ends there */
    check(!pend_hit(t, ~0ull - 0xFFF, ~0ull), "a range that would wrap");
    if (failures) return 1;
    printf("pending pages: aliases, holes, large ranges and removals\n");
    return 0;
}

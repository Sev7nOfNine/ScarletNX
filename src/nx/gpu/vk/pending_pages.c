/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include <stdlib.h>
#include "nx/gpu/vk/pending_pages.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/runtime/platform/log.h"

#define LEAF_PAGES 4096u
struct PendLeaf {
    u32 count[LEAF_PAGES];      /* the results covering each page */
    u16 group[LEAF_PAGES / 64]; /* each group's pages with a count */
    u32 pages;                  /* the leaf's pages with a count */
};

/* the guest pages [first, last] gain or lose a result */
static int add_pages(void* table, u64 first, u64 last, int delta) {
    PendPages* t = table;
    for (u64 p = first; p <= last; p++) {
        u64 li = p / LEAF_PAGES;
        PendLeaf* l;
        u32 i = (u32)(p % LEAF_PAGES), before;
        t->total += (u64)(i64)delta;
        if (li >= sizeof t->leaf / sizeof *t->leaf) {
            t->far += (u64)(i64)delta;
            continue;
        }
        l = t->leaf[li];
        if (!l && !(l = t->leaf[li] = calloc(1, sizeof *l))) fatal("vulkan: out of memory for pending result pages");
        before = l->count[i];
        l->count[i] = (u32)((i64)before + delta);
        if (!before != !l->count[i]) {
            int d = l->count[i] ? 1 : -1;
            l->group[i / 64] = (u16)(l->group[i / 64] + d);
            l->pages = (u32)((i64)l->pages + d);
        }
    }
    return 0;
}
/* calls fn for each run of guest pages behind [gva, gva + size), skipping unmapped GPU pages, until it returns nonzero */
static int each_run(u64 gva, u64 size, int (*fn)(void* t, u64 first, u64 last, int delta), void* t, int delta) {
    u64 a = gva, left = gva && size > 0 - gva ? 0 - gva : size; /* up to the top of the address space */
    while (left) {
        u64 cpu, n = gmmu_run(a, left, &cpu);
        if (!n) { /* an unmapped page: nothing of guest memory behind it */
            n = 0x1000 - (a & 0xFFF);
            if (n >= left) break;
        } else if (fn(t, cpu >> 12, (cpu + n - 1) >> 12, delta))
            return 1;
        a += n;
        left -= n;
    }
    return 0;
}
void pend_add(PendPages* t, u64 gva, u64 size, int delta) { each_run(gva, size, add_pages, t, delta); }
/* whether a page of [first, last] has a count, skipping leaves and groups that have none */
static int hit_pages(void* table, u64 first, u64 last, int delta) {
    const PendPages* t = table;
    (void)delta;
    for (u64 p = first; p <= last;) {
        u64 li = p / LEAF_PAGES, leaf_end = (li + 1) * LEAF_PAGES - 1, stop = last < leaf_end ? last : leaf_end;
        const PendLeaf* l = li < sizeof t->leaf / sizeof *t->leaf ? t->leaf[li] : NULL;
        if (l && l->pages)
            while (p <= stop) {
                u32 i = (u32)(p % LEAF_PAGES);
                u64 gstop = p + (63u - (i & 63u)) < stop ? p + (63u - (i & 63u)) : stop;
                if (!l->group[i / 64]) {
                    p = gstop + 1;
                    continue;
                }
                for (; p <= gstop; p++)
                    if (l->count[p % LEAF_PAGES]) return 1;
            }
        p = stop + 1;
    }
    return 0;
}
int pend_hit(const PendPages* t, u64 gva, u64 size) {
    if (!t->total) return 0;
    if (t->far) return 1;
    return each_run(gva, size, hit_pages, (void*)t, 0);
}
int pend_empty(const PendPages* t) { return !t->total; }

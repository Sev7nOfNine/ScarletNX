/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.apache and NOTICE;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs.
   This fork's changes are under GPL-3.0-or-later, the license of the fork as a whole (LICENSE). */
/* vi's framebuffers: handed to the renderer, and the pace the game's frames are shown at */
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/cheats.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/renderer.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/hle/services/vi.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

void vi_convert_cpu(u64 a, u32 w, u32 h, u32 pitch, u32 layout, u32 bhl, u32 fmt, u32* out) {
    u32 bpp = (fmt == 4 || fmt == 6 || fmt == 7) ? 2 : 4;
    /* a buffer per calling thread, kept for the next frame */
    static __thread u8* lin;
    static __thread size_t lin_cap;
    if ((size_t)w * h * bpp > lin_cap) {
        free(lin);
        lin = malloc((size_t)w * h * bpp);
        if (!lin) fatal("Out of memory converting a %ux%u frame", w, h);
        lin_cap = (size_t)w * h * bpp;
    }
    if (layout == 3 || layout == 2) {
        deswizzle(lin, w * bpp, GP(a), w * bpp, h, bhl);
    } else {
        for (u32 y = 0; y < h; y++) memcpy(lin + (size_t)y * w * bpp, GP(a + (u64)y * pitch), w * bpp);
    }
    if (bpp == 4) {
        if (fmt == 5) {
            for (size_t i = 0; i < (size_t)w * h; i++) {
                u32 p = ((u32*)lin)[i];
                out[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
            }
        } else
            memcpy(out, lin, (size_t)w * h * 4);
    } else {
        u16* s = (u16*)lin;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            u32 p = s[i], r, g, b;
            if (fmt == 4) {
                r = (p >> 11) & 31;
                g = (p >> 5) & 63;
                b = p & 31;
                r = (r << 3) | (r >> 2);
                g = (g << 2) | (g >> 4);
                b = (b << 3) | (b >> 2);
            } else if (fmt == 6) {
                r = (p >> 11) & 31;
                g = (p >> 6) & 31;
                b = (p >> 1) & 31;
                r = (r << 3) | (r >> 2);
                g = (g << 3) | (g >> 2);
                b = (b << 3) | (b >> 2);
            } else {
                r = ((p >> 12) & 15) * 17;
                g = ((p >> 8) & 15) * 17;
                b = ((p >> 4) & 15) * 17;
            }
            out[i] = r | (g << 8) | (b << 16) | 0xFF000000u;
        }
    }
}
void vi_show_buffer(u32 nvmap_id, u32 offset, u32 w, u32 h, u32 pitch, u32 layout, u32 bhl, u32 fmt, const i32 crop[4],
                    u32 transform) {
    u64 a = nvmap_addr_by_id(nvmap_id);
    if (!a) { /* the game queues it every frame: said a few times */
        WARN_LIMIT(5, "vi: buffer without memory (nvmap %u)\n", nvmap_id);
        return;
    }
    /* the renderer runs before the game's first thread does: video_init ends the run when it can't start */
    g_renderer->present_buffer(a + offset, w, h, pitch, layout, bhl, fmt, crop, transform);
    __atomic_add_fetch(&g_frames, 1, __ATOMIC_RELAXED);
    cheats_frame(); /* the cheats run once per frame shown, as dmnt's run on the console */
}
/* fps_limit=game: the console's display keeps each frame for the swap interval it was queued with, so a game made
   for 30 FPS (interval 2) runs at 30, and its game thread waits for a buffer until then. This wait is that one, before
   the game is handed a buffer for its next frame: the GPU thread never waits for the display. A frame less than a
   refresh late keeps its turn, so the game thread's own oversleep isn't lost. One later than that starts the schedule
   from now: the frame after it isn't hurried to catch up, and a game slower than the limit keeps its own pace with no
   wait added. The refresh is the display's (60 Hz) for the game's pace and the period itself for a number. Interval 0
   asks for no wait (Ryujinx's SurfaceFlinger takes it the same way) */
void present_pace(u32 interval) {
    static PMutex lock = PMUTEX_INIT;                        /* a game may queue on more than one thread */
    static u64 last;                                         /* the turn the previous frame was given */
    int fps = __atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED); /* F4 and reloads change it on the main thread */
    u64 per, refresh, now, next;
    if (!fps || (fps == SNX_FPS_GAME && !interval)) return;
    refresh = fps == SNX_FPS_GAME ? 1000000000ull / VI_REFRESH_HZ : 1000000000ull / (u64)fps;
    per = fps == SNX_FPS_GAME ? refresh * interval : refresh;
    pm_lock(&lock);
    now = plat_ns();
    next = last + per;
    if (!last || now > next + refresh) next = now; /* the first frame, or one more than a refresh late */
    last = next;
    pm_unlock(&lock);
    if (next > now) plat_sleep_ns((i64)(next - now));
}

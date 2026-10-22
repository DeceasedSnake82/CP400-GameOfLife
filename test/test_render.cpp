// Renderer vs. a slow per-pixel reference, for every zoom level and random views.
//   make -C test render
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../render.h"

static uint16_t g_vram[320 * 528];
static uint16_t* host_vram() { return g_vram; }
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;

static uint32_t rng = 4242;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int failures = 0;

static void makeWorld(life::World& w) {
    w.clear();
    const int N = life::GRID;
    const int mode = rnd() % 4;
    if (mode == 0) {                                       // sparse random cells (lots of empty chunks / rows)
        for (int i = 0; i < 3000; ++i) w.set(rnd() % N, rnd() % N, true);
    } else if (mode == 1) {                                // a few dense blobs
        for (int i = 0; i < 6; ++i) {
            int x0 = rnd() % N, y0 = rnd() % N;
            uint32_t seed = rnd() | 1;
            w.fillRandom(x0, y0, x0 + 40 + rnd() % 400, y0 + 40 + rnd() % 400, seed);
        }
    } else if (mode == 2) {                                // everything
        uint32_t seed = rnd() | 1;
        w.fillRandom(0, 0, N, N, seed);
    } else {                                               // cells hugging the world edges
        for (int i = 0; i < 2000; ++i) {
            const int e = rnd() % 4, t = rnd() % N, d = rnd() % 3;
            if (e == 0) w.set(d, t, true); else if (e == 1) w.set(N - 1 - d, t, true);
            else if (e == 2) w.set(t, d, true); else w.set(t, N - 1 - d, true);
        }
    }
}

int main() {
    gfx::init();
    const int viewH = 418;
    const int N = life::GRID;
    void* mem = malloc(life::World::memoryNeeded());
    life::World w;
    w.init(mem);

    long pixelsChecked = 0;
    for (int world = 0; world < 24; ++world) {
        makeWorld(w);
        for (int zi = 0; zi < render::NUM_ZOOMS; ++zi) {
            for (int trial = 0; trial < 6; ++trial) {
                int vx = (int)(rnd() % (N + 600)) - 300, vy = (int)(rnd() % (N + 600)) - 300;
                if (trial == 0) vx = vy = 0;
                if (trial == 1) vx = vy = N;               // clamps to the far corner
                render::clampView(zi, viewH, vx, vy);

                memset(g_vram, 0xAB, sizeof g_vram);
                render::drawWorld(w, vx, vy, viewH, zi);

                const int z = render::ZOOMS[zi];
                bool ok = true;
                for (int py = 0; py < viewH && ok; ++py)
                    for (int px = 0; px < 320; ++px) {
                        uint16_t want;
                        if (z > 0) {
                            const int cx = vx + px / z, cy = vy + py / z;
                            const bool gap = z >= 4 && (px % z == z - 1 || py % z == z - 1);
                            want = gap ? render::COL_GRID : (w.get(cx, cy) ? render::COL_ALIVE : render::COL_DEAD);
                        } else {
                            const int K = -z;
                            const int bx = (vx & ~31) + px * K, by = (vy & ~(K - 1)) + py * K;
                            if (bx < 0 || by < 0 || bx + K > N || by + K > N) {
                                want = render::COL_VOID;
                                if (bx + K > 0 && bx < N && by + K > 0 && by < N) { printf("block straddles world edge!\n"); ok = false; }
                            } else {
                                bool any = false;
                                for (int j = 0; j < K && !any; ++j) for (int i = 0; i < K; ++i) if (w.get(bx + i, by + j)) { any = true; break; }
                                want = any ? render::COL_ALIVE : render::COL_DEAD;
                            }
                        }
                        ++pixelsChecked;
                        if (g_vram[py * 320 + px] != want) {
                            printf("FAIL world %d zoom %d (z=%d) view (%d,%d): pixel (%d,%d) got %04x want %04x\n", world, zi, z, vx, vy, px, py, g_vram[py * 320 + px], want);
                            ++failures; ok = false; break;
                        }
                    }
                // nothing below the view may be touched
                for (int i = viewH * 320; i < 320 * 528; ++i)
                    if (g_vram[i] != 0xABAB) { printf("FAIL zoom %d: drew outside the view (pixel index %d)\n", zi, i); ++failures; break; }
            }
        }
    }
    if (failures) printf("FAILED (%d)\n", failures);
    else printf("render: ALL PASSED (%ld pixels checked)\n", pixelsChecked);
    free(mem);
    return failures != 0;
}

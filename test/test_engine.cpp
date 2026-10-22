// Host-side tests for the Life engine. Build and run with:  make -C test engine
//
//  * Compares the tiled bit-parallel engine against a naive reference on a
//    small torus, with random edits / soups / clears between steps (this is
//    what exercises the active-tile skipping and the no-copy double buffering).
//  * Checks well known results at the real world size.
#include "../life.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

using life::World;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rng = 12345;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

struct Naive {
    std::vector<uint8_t> g, n;
    Naive() : g(life::GRID * life::GRID, 0), n(life::GRID * life::GRID, 0) {}
    uint8_t& at(int x, int y) { return g[(size_t)(y & (life::GRID - 1)) * life::GRID + (x & (life::GRID - 1))]; }
    void step() {
        const int N = life::GRID;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                int c = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy) c += at(x + dx, y + dy);
                n[(size_t)y * N + x] = (c == 3) || (c == 2 && at(x, y));
            }
        g.swap(n);
    }
};

static void *alloc(World& w) {
    void* mem = malloc(World::memoryNeeded());
    w.init(mem);
    return mem;
}

static bool same(World& w, Naive& ref, const char* what, int iter) {
    long pop = 0;
    for (int y = 0; y < life::GRID; ++y)
        for (int x = 0; x < life::GRID; ++x) {
            const bool a = w.get(x, y), b = ref.at(x, y) != 0;
            pop += b;
            if (a != b) { printf("FAIL %s iter %d: cell (%d,%d) engine=%d ref=%d\n", what, iter, x, y, a, b); ++failures; return false; }
        }
    if (w.population() != pop) { printf("FAIL %s iter %d: population engine=%d ref=%ld\n", what, iter, (int)w.population(), pop); ++failures; return false; }
    return true;
}

// Randomised differential test on the (small, when built with -DLIFE_TILES_LOG2=3) torus.
static void differential(int seedBase, int iters) {
    World w; void* mem = alloc(w);
    Naive ref;
    rng = 777u + seedBase;
    const int N = life::GRID;
    for (int it = 0; it < iters; ++it) {
        const uint32_t action = rnd() % 16;
        if (action < 2) {                               // random soup in a rectangle (may straddle the seam / tile edges)
            int x0 = rnd() % N, y0 = rnd() % N, x1 = x0 + 1 + rnd() % (N / 2), y1 = y0 + 1 + rnd() % (N / 2);
            if (x1 > N) x1 = N;
            if (y1 > N) y1 = N;
            uint32_t seed = rnd() | 1;
            w.fillRandom(x0, y0, x1, y1, seed);
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) ref.at(x, y) = w.get(x, y);   // adopt engine's soup
            // cells outside the rectangle must be untouched
        } else if (action < 8) {                        // single cell edits
            int k = 1 + rnd() % 12;
            while (k--) {
                int x = rnd() % N, y = rnd() % N; bool v = rnd() & 1;
                if (rnd() % 4 == 0) { x = (x & ~31) + (rnd() % 2 ? 0 : 31); y = (y & ~31) + (rnd() % 2 ? 0 : 31); }  // tile corners
                w.set(x, y, v); ref.at(x, y) = v;
            }
        } else if (action == 8 && rnd() % 8 == 0) {     // clear
            w.clear(); std::fill(ref.g.begin(), ref.g.end(), 0);
        }
        int steps = 1 + rnd() % 4;
        while (steps--) { w.step(); ref.step(); }
        if (!same(w, ref, "differential", it)) break;
    }
    free(mem);
}

// All-zero / all-one / blinker-at-seam sanity checks
static void edgeCases() {
    World w; void* mem = alloc(w);
    const int N = life::GRID;
    // Blinker straddling the left/right seam
    w.set(N - 1, 5, true); w.set(0, 5, true); w.set(1, 5, true);
    w.step();
    CHECK(w.get(0, 4) && w.get(0, 5) && w.get(0, 6) && w.population() == 3, "seam blinker");
    w.step();
    CHECK(w.get(N - 1, 5) && w.get(0, 5) && w.get(1, 5) && w.population() == 3, "seam blinker back");
    // Block astride the corner of the torus never changes
    w.clear();
    w.set(N - 1, N - 1, true); w.set(0, N - 1, true); w.set(N - 1, 0, true); w.set(0, 0, true);
    for (int i = 0; i < 5; ++i) w.step();
    CHECK(w.population() == 4 && w.get(0, 0) && w.get(N - 1, N - 1), "corner block");
    // A lone cell dies and the world goes idle (0 tiles computed after settling)
    w.clear(); w.set(10, 10, true); w.step();
    CHECK(w.population() == 0, "lone cell dies");
    w.step();
    CHECK(w.step() == 0, "idle world computes no tiles");
    // Out of range access is ignored
    w.set(-1, 0, true); w.set(0, N, true);
    CHECK(w.population() == 0 && !w.get(-1, 0), "oob ignored");
    free(mem);
}

static void stamp(World& w, int ox, int oy, const char* const* rows) {
    for (int y = 0; rows[y]; ++y)
        for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] == 'O') w.set(ox + x, oy + y, true);
}

// Known results on the full-size world.
static void known() {
    World w; void* mem = alloc(w);
    const int N = life::GRID;
    {   // glider: translates by (+1,+1) every 4 generations
        static const char* const g[] = {".O.", "..O", "OOO", 0};
        stamp(w, 100, 100, g);
        for (int i = 0; i < 400; ++i) w.step();
        CHECK(w.population() == 5, "glider pop");
        CHECK(w.get(201, 200) && w.get(202, 201) && w.get(200, 202) && w.get(201, 202) && w.get(202, 202), "glider moved by (100,100)");
    }
    {   // glider wraps around the torus: N*4 gens returns it to start
        w.clear();
        static const char* const g[] = {".O.", "..O", "OOO", 0};
        stamp(w, 3, 3, g);
        for (int i = 0; i < 4 * N; ++i) w.step();
        CHECK(w.population() == 5 && w.get(4, 3) && w.get(5, 4) && w.get(3, 5), "glider full lap");
    }
    {   // R-pentomino: stabilises at generation 1103 with 116 cells
        w.clear();
        static const char* const r[] = {".OO", "OO.", ".O.", 0};
        stamp(w, N / 2, N / 2, r);
        int stableFrom = -1; int32_t last = -1; (void)stableFrom;
        for (int i = 0; i < 1103; ++i) w.step();
        CHECK(w.population() == 116, "R-pentomino pop at gen 1103 = %d (want 116)", (int)w.population());
        last = w.population();
        for (int i = 0; i < 4; ++i) w.step();   // period-4 gliders etc. keep population constant
        CHECK(w.population() == last, "R-pentomino stable after 1103");
    }
    {   // Diehard vanishes after exactly 130 generations
        w.clear();
        static const char* const d[] = {"......O.", "OO......", ".O...OOO", 0};
        stamp(w, 500, 500, d);
        for (int i = 0; i < 129; ++i) w.step();
        CHECK(w.population() > 0, "diehard alive at 129");
        w.step();
        CHECK(w.population() == 0, "diehard dead at 130 (pop %d)", (int)w.population());
    }
    free(mem);
}

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void bench() {
    World w; void* mem = alloc(w);
    const int N = life::GRID;
    {   // dense soup over the whole world: every tile active (worst case)
        uint32_t seed = 99;
        w.fillRandom(0, 0, N, N, seed);
        w.step();
        double t0 = now(); int tiles = 0;
        for (int i = 0; i < 20; ++i) tiles += w.step();
        double dt = now() - t0;
        printf("bench dense soup %dx%d: %.2f ms/gen (%d tiles/gen, %.2f us/tile)\n", N, N, dt * 1000 / 20, tiles / 20, dt * 1e6 / tiles);
    }
    {   // sparse: a few gliders
        w.clear();
        static const char* const g[] = {".O.", "..O", "OOO", 0};
        for (int i = 0; i < 20; ++i) stamp(w, 50 + i * 90, 50 + i * 70, g);
        double t0 = now();
        for (int i = 0; i < 2000; ++i) w.step();
        double dt = now() - t0;
        printf("bench 20 gliders: %.1f us/gen\n", dt * 1e6 / 2000);
    }
    {   // idle
        w.clear();
        double t0 = now();
        for (int i = 0; i < 100000; ++i) w.step();
        printf("bench empty world: %.3f us/gen\n", (now() - t0) * 1e6 / 100000);
    }
    free(mem);
}

int main(int argc, char** argv) {
    const bool small = life::GRID <= 512;
    if (small) {
        for (int s = 0; s < 40; ++s) differential(s, 60);
        edgeCases();
        printf("small-world differential tests done (GRID=%d)\n", life::GRID);
    } else {
        edgeCases();
        known();
        if (argc > 1 && !strcmp(argv[1], "bench")) bench();
    }
    printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures != 0;
}

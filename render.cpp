#include "render.h"

namespace render {

const int8_t ZOOMS[NUM_ZOOMS] = { -8, -4, -2, 1, 2, 3, 4, 6, 8, 12, 16, 24 };

namespace {

using life::GRID;
using life::TILE_CELLS;
using life::TILES_SIDE;

constexpr int MAX_CS = 24;
alignas(4) uint16_t lineBuf[VIEW_W + MAX_CS];
alignas(4) uint16_t emptyLine[VIEW_W + MAX_CS];

// 32 cells starting at cell column `x` of the row, as a bit mask (bit 0 = x).
inline uint32_t bits32(const uint32_t* rp, int x) {
    const int wi = x >> 5, s = x & 31;
    const uint32_t a = rp[(wi & (TILES_SIDE - 1)) * TILE_CELLS];
    if (!s) return a;
    const uint32_t b = rp[((wi + 1) & (TILES_SIDE - 1)) * TILE_CELLS];
    return (a >> s) | (b << (32 - s));
}

// ---------------------------------------------------------------------------
// Zoomed in: one or more pixels per cell.
// ---------------------------------------------------------------------------
void drawCells(const life::World& w, int vx, int vy, int viewH, int cs) {
    uint16_t* const fb = gfx::fb;
    const int nCols = (VIEW_W + cs - 1) / cs;
    const int nRows = (viewH + cs - 1) / cs;
    const int gap = cs >= 4 ? 1 : 0;               // 1px grid line on the right / bottom of each cell
    const int body = cs - gap;
    const int nChunks = (nCols + 31) >> 5;

    if (cs > 1) {                                  // template for rows with no live cells
        uint16_t* d = emptyLine;
        for (int j = 0; j < nCols; ++j, d += cs) {
            for (int k = 0; k < body; ++k) d[k] = COL_DEAD;
            if (gap) d[body] = COL_GRID;
        }
    }

    uint32_t chunk[12];
    for (int r = 0; r < nRows; ++r) {
        const int py = r * cs;
        const int rows = (viewH - py < cs) ? viewH - py : cs;
        uint16_t* const dst0 = fb + py * VIEW_W;
        const uint32_t* const rp = w.row(vy + r);

        uint32_t any = 0;
        for (int c = 0; c < nChunks; ++c) { chunk[c] = bits32(rp, vx + 32 * c); any |= chunk[c]; }

        if (cs == 1) {                             // build straight into VRAM
            for (int c = 0; c < nChunks; ++c) {
                uint16_t* d = dst0 + c * 32;
                const int cnt = (nCols - 32 * c < 32) ? nCols - 32 * c : 32;
                uint32_t b = chunk[c];
                if (!b) { gfx::fillSpan(d, cnt, COL_DEAD); continue; }
                for (int j = 0; j < cnt; ++j, b >>= 1) d[j] = (b & 1) ? COL_ALIVE : COL_DEAD;
            }
            continue;
        }

        const uint16_t* src = emptyLine;
        if (any) {
            for (int c = 0; c < nChunks; ++c) {
                uint16_t* d = lineBuf + c * 32 * cs;
                const int cnt = (nCols - 32 * c < 32) ? nCols - 32 * c : 32;
                uint32_t b = chunk[c];
                if (!b) {                          // 32 empty cells: copy from the template
                    const uint16_t* e = emptyLine + c * 32 * cs;
                    for (int i = 0; i < cnt * cs; ++i) d[i] = e[i];
                    continue;
                }
                for (int j = 0; j < cnt; ++j, b >>= 1, d += cs) {
                    const uint16_t col = (b & 1) ? COL_ALIVE : COL_DEAD;
                    for (int k = 0; k < body; ++k) d[k] = col;
                    if (gap) d[body] = COL_GRID;
                }
            }
            src = lineBuf;
        }
        for (int k = 0; k < rows; ++k) {
            uint16_t* d = dst0 + k * VIEW_W;
            if (gap && k == cs - 1) gfx::fillSpan(d, VIEW_W, COL_GRID);
            else gfx::copyPixels(d, src, VIEW_W);
        }
    }
}

// ---------------------------------------------------------------------------
// Zoomed out: K x K cells per pixel (K = 2, 4, 8). vx is snapped to a word
// boundary and vy to a multiple of K, so every block lies inside one word and
// the K source rows of a pixel row are consecutive words of a tile.
// ---------------------------------------------------------------------------
void drawMinified(const life::World& w, int vx, int vy, int viewH, int K) {
    uint16_t* const fb = gfx::fb;
    const int ppw = 32 / K;                        // pixels produced per 32-cell word
    const int nWords = VIEW_W / ppw;
    vx &= ~31;
    vy &= ~(K - 1);
    const int wx0 = vx >> 5;

    for (int pr = 0; pr < viewH; ++pr) {
        uint16_t* d = fb + pr * VIEW_W;
        const int sy = vy + pr * K;
        if ((unsigned)sy >= (unsigned)GRID) { gfx::fillSpan(d, VIEW_W, COL_VOID); continue; }
        const uint32_t* const rp = w.row(sy);
        for (int wi = 0; wi < nWords; ++wi, d += ppw) {
            const int wx = wx0 + wi;
            if ((unsigned)wx >= (unsigned)TILES_SIDE) { for (int j = 0; j < ppw; ++j) d[j] = COL_VOID; continue; }
            const uint32_t* p = rp + wx * TILE_CELLS;
            uint32_t v = p[0];
            for (int k = 1; k < K; ++k) v |= p[k];
            if (!v) { for (int j = 0; j < ppw; ++j) d[j] = COL_DEAD; continue; }
            v |= v >> 1;                           // fold K bits into the lowest bit of each group
            if (K >= 4) v |= v >> 2;
            if (K >= 8) v |= v >> 4;
            for (int j = 0; j < ppw; ++j, v >>= K) d[j] = (v & 1) ? COL_ALIVE : COL_DEAD;
        }
    }
}

int axisSpan(int zoomIdx, int pixels) {
    const int z = ZOOMS[zoomIdx];
    return z > 0 ? (pixels + z - 1) / z : pixels * -z;
}

int clampAxis(int v, int vis) {
    if (vis >= GRID) return -((vis - GRID) / 2);   // smaller than the screen: centre it
    if (v < 0) return 0;
    if (v > GRID - vis) return GRID - vis;
    return v;
}

}  // namespace

int visCols(int zoomIdx) { return axisSpan(zoomIdx, VIEW_W); }
int visRows(int zoomIdx, int viewH) { return axisSpan(zoomIdx, viewH); }

void clampView(int zoomIdx, int viewH, int& vx, int& vy) {
    vx = clampAxis(vx, visCols(zoomIdx));
    vy = clampAxis(vy, visRows(zoomIdx, viewH));
}

void drawWorld(const life::World& world, int vx, int vy, int viewH, int zoomIdx) {
    const int z = ZOOMS[zoomIdx];
    if (z > 0) drawCells(world, vx, vy, viewH, z);
    else drawMinified(world, vx, vy, viewH, -z);
}

}  // namespace render

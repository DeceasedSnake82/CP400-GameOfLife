// ============================================================================
//  Conway's Game of Life engine (platform independent, no SDK dependencies)
//
//  * Toroidal world of GRID x GRID cells (default 2048 x 2048 = 4.2M cells).
//  * Cells are bit-packed, 32 per word, and stored tile-major: a tile is 32x32
//    cells = 32 consecutive words, so one tile (and its neighbours) stays
//    cache-resident while it is being computed.
//  * A generation is computed 32 cells at a time with bitwise full/half adders.
//  * Only "active" tiles are recomputed: a tile is active when it, or one of
//    its 8 neighbours, changed in the previous generation. Empty space and
//    still lifes cost nothing, so big sparse patterns (guns, glider streams,
//    spaceships) run as fast as small ones.
//  * Double buffered, but inactive tiles are never copied (see World::step).
// ============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef LIFE_TILES_LOG2
#define LIFE_TILES_LOG2 6               // 2^6 = 64 tiles per side
#endif

namespace life {

constexpr int TILE_LOG2   = 5;                       // tile = 32 x 32 cells
constexpr int TILE_CELLS  = 1 << TILE_LOG2;          // also words per tile
constexpr int TILES_LOG2  = LIFE_TILES_LOG2;
constexpr int TILES_SIDE  = 1 << TILES_LOG2;
constexpr int TILE_COUNT  = TILES_SIDE * TILES_SIDE;
constexpr int GRID_LOG2   = TILES_LOG2 + TILE_LOG2;
constexpr int GRID        = 1 << GRID_LOG2;          // cells per side
constexpr int WORDS_TOTAL = TILE_COUNT * TILE_CELLS;

class World {
public:
    // Bytes of memory init() needs. Caller supplies it (4-byte aligned).
    static size_t memoryNeeded();
    void init(void* memory);                         // also clears

    void clear();                                    // kill everything, generation = 0
    bool get(int x, int y) const;
    void set(int x, int y, bool alive);              // out-of-range is ignored
    // Replace the rectangle [x0,x1) x [y0,y1) (clipped to the world) with
    // random soup of roughly 37% density.
    void fillRandom(int x0, int y0, int x1, int y1, uint32_t& seed);

    // Advance one generation. Returns the number of tiles that were computed
    // (a measure of the work done).
    int step();

    uint32_t generation() const { return gen_; }
    int32_t  population() const { return total_; }

    // Row access for renderers: word i (cells 32*i .. 32*i+31, bit 0 = leftmost)
    // of row y is row(y)[i * TILE_CELLS]. Rows y .. y+k that share a tile
    // (same y >> 5) are contiguous: row(y)[k].
    const uint32_t* row(int y) const {
        return cur_ + (((y >> TILE_LOG2) << TILES_LOG2) << TILE_LOG2) + (y & (TILE_CELLS - 1));
    }

private:
    static uint32_t wordIndex(int x, int y) {
        return ((((uint32_t)(y >> TILE_LOG2) << TILES_LOG2) + (uint32_t)(x >> TILE_LOG2)) << TILE_LOG2)
               + (uint32_t)(y & (TILE_CELLS - 1));
    }
    void markPending(uint32_t tile) {
        if (!pend_[tile]) { pend_[tile] = 1; chg_[nChg_++] = (uint16_t)tile; }
    }
    void recount(uint32_t tile);
    bool stepTile(uint32_t tile);

    uint32_t* cur_;          // generation n
    uint32_t* nxt_;          // scratch for generation n+1
    uint16_t* pop_;          // live cells per tile
    uint16_t* chg_;          // tiles that changed (or were edited) since they were last stepped
    uint16_t* act_;          // scratch: tiles to compute this step
    uint32_t* mark_;         // per-tile stamp used to de-duplicate act_
    uint8_t*  pend_;         // 1 if the tile is listed in chg_
    uint32_t  nChg_;
    uint32_t  stamp_;
    uint32_t  gen_;
    int32_t   total_;
};

}  // namespace life

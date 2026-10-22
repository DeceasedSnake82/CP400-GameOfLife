#include "life.h"

namespace life {

namespace {

constexpr uint32_t TILE_MASK = TILES_SIDE - 1;

// ---------------------------------------------------------------------------
// Bit-parallel neighbour counting.
//
// For one row of 32 cells we need, per cell, the count of live cells among the
// left / centre / right columns. rowSum() produces those per-row partial sums
// as bit-planes so that three of them (row above, own row, row below) can be
// added with a handful of boolean ops for all 32 cells at once.
//
// Bit i of a word is cell x = 32*word + i, so the left neighbour of bit i is
// bit i-1 (shift left by one) and the right neighbour is bit i+1.
// ---------------------------------------------------------------------------
struct RowSum {
    uint32_t x;    // l ^ r        : sum bit   of {left,right}       (own row, centre excluded)
    uint32_t hc;   // l & r        : carry bit of {left,right}
    uint32_t fs;   // l ^ m ^ r    : sum bit   of {left,centre,right} (rows above/below)
    uint32_t fc;   // maj(l,m,r)   : carry bit of {left,centre,right}
};

static inline RowSum rowSum(uint32_t left, uint32_t mid, uint32_t right) {
    const uint32_t l = (mid << 1) | (left >> 31);
    const uint32_t r = (mid >> 1) | (right << 31);
    RowSum s;
    s.x  = l ^ r;
    s.hc = l & r;
    s.fs = s.x ^ mid;
    s.fc = s.hc | (mid & s.x);
    return s;
}

// Next state of 32 cells given the sums of the row above, own row and row below.
static inline uint32_t nextState(const RowSum& up, const RowSum& mid, uint32_t self, const RowSum& dn) {
    // Weight-1 plane: up.fs + mid.x + dn.fs
    const uint32_t t  = up.fs ^ mid.x;
    const uint32_t b0 = t ^ dn.fs;
    const uint32_t k1 = (up.fs & mid.x) | (dn.fs & t);        // carry into weight 2
    // Weight-2 plane: up.fc + mid.hc + dn.fc
    const uint32_t u  = up.fc ^ mid.hc;
    const uint32_t t2 = u ^ dn.fc;
    const uint32_t k2 = (up.fc & mid.hc) | (dn.fc & u);       // carry into weight 4
    // count = b0 + 2*b1 + 4*b2 + 8*b3
    const uint32_t b1 = t2 ^ k1;
    const uint32_t m  = t2 & k1;                              // b2 = k2 ^ m, b3 = k2 & m
    // alive next  <=>  count == 3  ||  (count == 2 && alive)
    return b1 & ~(k2 | m) & (b0 | self);
}

// Population of one tile (32 words). Bytes are accumulated across 16 words
// (<= 128 per byte) before the horizontal fold, which keeps this to a few ops
// per word.
static uint32_t tilePop(const uint32_t* w) {
    uint32_t total = 0;
    for (int h = 0; h < 2; ++h) {
        uint32_t acc = 0;
        for (int i = 0; i < 16; ++i) {
            uint32_t x = w[h * 16 + i];
            x = x - ((x >> 1) & 0x55555555u);
            x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
            x = (x + (x >> 4)) & 0x0F0F0F0Fu;
            acc += x;
        }
        acc = (acc & 0x00FF00FFu) + ((acc >> 8) & 0x00FF00FFu);
        total += (acc & 0xFFFFu) + (acc >> 16);
    }
    return total;
}

static inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------

size_t World::memoryNeeded() {
    return sizeof(uint32_t) * (2 * (size_t)WORDS_TOTAL + TILE_COUNT)    // cur, nxt, mark
         + sizeof(uint16_t) * 3 * (size_t)TILE_COUNT                    // pop, chg, act
         + (size_t)TILE_COUNT;                                          // pend
}

void World::init(void* memory) {
    uint8_t* p = (uint8_t*)memory;
    cur_  = (uint32_t*)p; p += sizeof(uint32_t) * WORDS_TOTAL;
    nxt_  = (uint32_t*)p; p += sizeof(uint32_t) * WORDS_TOTAL;
    mark_ = (uint32_t*)p; p += sizeof(uint32_t) * TILE_COUNT;
    pop_  = (uint16_t*)p; p += sizeof(uint16_t) * TILE_COUNT;
    chg_  = (uint16_t*)p; p += sizeof(uint16_t) * TILE_COUNT;
    act_  = (uint16_t*)p; p += sizeof(uint16_t) * TILE_COUNT;
    pend_ = (uint8_t*)p;
    for (int i = 0; i < TILE_COUNT; ++i) mark_[i] = 0;
    stamp_ = 0;
    clear();
}

void World::clear() {
    for (int i = 0; i < WORDS_TOTAL; ++i) { cur_[i] = 0; nxt_[i] = 0; }
    for (int i = 0; i < TILE_COUNT; ++i) { pop_[i] = 0; pend_[i] = 0; }
    nChg_ = 0;
    gen_ = 0;
    total_ = 0;
}

bool World::get(int x, int y) const {
    if ((unsigned)x >= (unsigned)GRID || (unsigned)y >= (unsigned)GRID) return false;
    return (cur_[wordIndex(x, y)] >> (x & 31)) & 1u;
}

void World::set(int x, int y, bool alive) {
    if ((unsigned)x >= (unsigned)GRID || (unsigned)y >= (unsigned)GRID) return;
    const uint32_t idx = wordIndex(x, y);
    const uint32_t bit = 1u << (x & 31);
    const bool was = (cur_[idx] & bit) != 0;
    if (was == alive) return;
    cur_[idx] ^= bit;
    const uint32_t tile = idx >> TILE_LOG2;
    if (alive) { ++pop_[tile]; ++total_; } else { --pop_[tile]; --total_; }
    markPending(tile);
}

void World::recount(uint32_t tile) {
    const uint32_t p = tilePop(cur_ + tile * TILE_CELLS);
    total_ += (int32_t)p - (int32_t)pop_[tile];
    pop_[tile] = (uint16_t)p;
}

void World::fillRandom(int x0, int y0, int x1, int y1, uint32_t& seed) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GRID) x1 = GRID;
    if (y1 > GRID) y1 = GRID;
    if (x0 >= x1 || y0 >= y1) return;
    if (seed == 0) seed = 0x2545F491u;

    const int w0 = x0 >> 5, w1 = (x1 - 1) >> 5;
    for (int y = y0; y < y1; ++y) {
        for (int w = w0; w <= w1; ++w) {
            const int lo = (w == w0) ? (x0 & 31) : 0;
            const int hi = (w == w1) ? (((x1 - 1) & 31) + 1) : 32;
            const uint32_t mask = (hi - lo == 32) ? 0xFFFFFFFFu : (((1u << (hi - lo)) - 1u) << lo);
            const uint32_t soup = xorshift(seed) & (xorshift(seed) | xorshift(seed));   // 37.5%
            uint32_t& word = cur_[wordIndex(w << 5, y)];
            word = (word & ~mask) | (soup & mask);
        }
    }
    for (int ty = y0 >> TILE_LOG2; ty <= (y1 - 1) >> TILE_LOG2; ++ty) {
        for (int tx = w0; tx <= w1; ++tx) {
            const uint32_t tile = ((uint32_t)ty << TILES_LOG2) + (uint32_t)tx;
            recount(tile);
            markPending(tile);
        }
    }
}

// Computes the next generation of one tile into nxt_. Returns true if any cell
// of the tile changed.
bool World::stepTile(uint32_t t) {
    const uint32_t tx = t & TILE_MASK, ty = t >> TILES_LOG2;
    const uint32_t txl = (tx - 1) & TILE_MASK, txr = (tx + 1) & TILE_MASK;
    const uint32_t tyu = (ty - 1) & TILE_MASK, tyd = (ty + 1) & TILE_MASK;
#define TILE_AT(X, Y) (cur_ + ((((Y) << TILES_LOG2) | (X)) << TILE_LOG2))
    const uint32_t* const M = TILE_AT(tx, ty);
    const uint32_t* const L = TILE_AT(txl, ty);
    const uint32_t* const R = TILE_AT(txr, ty);
    RowSum prev = rowSum(TILE_AT(txl, tyu)[TILE_CELLS - 1], TILE_AT(tx, tyu)[TILE_CELLS - 1],
                         TILE_AT(txr, tyu)[TILE_CELLS - 1]);
    RowSum cur = rowSum(L[0], M[0], R[0]);
    uint32_t* const out = nxt_ + (t << TILE_LOG2);

    uint32_t diff = 0;
    for (int r = 0; r < TILE_CELLS - 1; ++r) {
        const RowSum nx = rowSum(L[r + 1], M[r + 1], R[r + 1]);
        const uint32_t v = nextState(prev, cur, M[r], nx);
        out[r] = v;
        diff |= v ^ M[r];
        prev = cur;
        cur = nx;
    }
    {   // last row: the row below lives in the tiles underneath
        const RowSum nx = rowSum(TILE_AT(txl, tyd)[0], TILE_AT(tx, tyd)[0], TILE_AT(txr, tyd)[0]);
        const uint32_t v = nextState(prev, cur, M[TILE_CELLS - 1], nx);
        out[TILE_CELLS - 1] = v;
        diff |= v ^ M[TILE_CELLS - 1];
    }
#undef TILE_AT

    if (!diff) return false;
    const uint32_t p = tilePop(out);
    total_ += (int32_t)p - (int32_t)pop_[t];
    pop_[t] = (uint16_t)p;
    return true;
}

// Invariant that makes skipping safe and copy-free:
//   every tile that is not listed in chg_ has identical contents in cur_ and nxt_.
// A tile not in chg_ did not change last step, so its (older) copy in nxt_ is
// still equal to cur_. Tiles that are edited, or change when stepped, are put
// in chg_ and are therefore always rewritten completely by the next step.
int World::step() {
    if (++stamp_ == 0) {                         // stamp wrapped: reset marks
        for (int i = 0; i < TILE_COUNT; ++i) mark_[i] = 0;
        stamp_ = 1;
    }

    // Active = every changed tile and its 8 neighbours (torus).
    uint32_t nAct = 0;
    for (uint32_t i = 0; i < nChg_; ++i) {
        const uint32_t t = chg_[i];
        pend_[t] = 0;
        const uint32_t tx = t & TILE_MASK, ty = t >> TILES_LOG2;
        for (int dy = -1; dy <= 1; ++dy) {
            const uint32_t ny = (ty + (uint32_t)dy) & TILE_MASK;
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t n = (ny << TILES_LOG2) | ((tx + (uint32_t)dx) & TILE_MASK);
                if (mark_[n] != stamp_) { mark_[n] = stamp_; act_[nAct++] = (uint16_t)n; }
            }
        }
    }
    nChg_ = 0;

    for (uint32_t i = 0; i < nAct; ++i) {
        const uint32_t t = act_[i];
        if (stepTile(t)) { pend_[t] = 1; chg_[nChg_++] = (uint16_t)t; }
    }

    uint32_t* tmp = cur_; cur_ = nxt_; nxt_ = tmp;
    ++gen_;
    return (int)nAct;
}

}  // namespace life

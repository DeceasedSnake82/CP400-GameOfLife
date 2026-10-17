// World -> pixels. Draws the visible part of the grid into the top of VRAM.
#pragma once

#include <stdint.h>
#include "gfx.h"
#include "life.h"

namespace render {

constexpr int VIEW_W = gfx::W;

// Zoom levels. Positive: pixels per cell (cells get a grid line from 4 up).
// Negative: -z cells per pixel, i.e. a zoomed-out overview where a pixel is lit
// if any cell in its block is alive (-8 shows the whole 2048x2048 world).
constexpr int NUM_ZOOMS = 12;
extern const int8_t ZOOMS[NUM_ZOOMS];
constexpr int DEFAULT_ZOOM = 7;                   // 6 pixels per cell
constexpr int MIN_EDIT_ZOOM = 3;                  // first index with >= 1 pixel per cell

constexpr uint16_t COL_ALIVE = gfx::rgb(90, 240, 130);
constexpr uint16_t COL_DEAD  = gfx::rgb(14, 16, 24);
constexpr uint16_t COL_GRID  = gfx::rgb(30, 34, 46);
constexpr uint16_t COL_VOID  = gfx::rgb(4, 4, 6);

int visCols(int zoomIdx);                         // cells across the view
int visRows(int zoomIdx, int viewH);              // cells down the view
void clampView(int zoomIdx, int viewH, int& vx, int& vy);

// (vx, vy) is the cell shown at the top-left pixel.
void drawWorld(const life::World& world, int vx, int vy, int viewH, int zoomIdx);

}  // namespace render

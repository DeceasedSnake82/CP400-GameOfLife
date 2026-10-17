# Conway's Game of Life for the CP400

A fast, big-grid Game of Life for the ClassPad CP400, written for [Hollyhock 3](https://github.com/ClassPad-Dev/hollyhock-3).
To use it, copy **`GameOfLife.hh3`** to the calculator.

## Controls

| Action | Touch | Key |
| --- | --- | --- |
| Toggle a cell / paint cells | tap / drag in the grid | |
| Move around | | arrow keys (hold to speed up) |
| Zoom in / out | `ZOOM+` / `ZOOM-` | `+` / `-` |
| Play / pause | `PLAY` / `PAUSE` | `EXE` |
| Single step | `STEP` | `.` |
| Faster / slower | `FAST` / `SLOW` | `×` / `÷` |
| Random soup in the view | `RAND` | `0` |
| Stamp a pattern in the view centre (cycles through glider gun, glider, LWSS, ...) | `PATN` | `5` |
| Centre the view | | `1` |
| Clear everything (asks twice) | `CLEAR` | `Backspace` |
| Help screen | `HELP` | `=` |
| Quit | | `Power/Clear` |

The simulation pauses by itself when every cell has died or nothing is changing any more.

## The world

* 2048 × 2048 cells (4.2 million) on a **wrap-around** (toroidal) grid: a glider that leaves one edge comes back on the opposite one.
* 12 zoom levels, from 24 px per cell down to an overview where one pixel covers 8 × 8 cells and the whole world fits on screen.
* Speed is counted in frames (simulate + draw + LCD refresh) because the SDK has no timer: from 1 generation per 24 frames up to 64 generations per frame.

## How it is fast

* Cells are bit-packed, 32 per word, and a whole word is advanced at once with bitwise full/half adders (about 60 instructions per 32 cells).
* The world is stored in 32 × 32 cell tiles. Only tiles that changed last generation, plus their neighbours, are recomputed. Empty space and still lifes cost nothing, so a glider gun in a huge empty world runs as fast as on a tiny one.
* The two generation buffers are never copied: inactive tiles are provably identical in both (see the comment in `World::step`).
* Drawing only walks the visible cells, skips empty 32-cell chunks, builds one pixel row per cell row and copies it down; the zoomed-out views OR whole words together. The screen is only redrawn when something changed, and while paused the app sleeps in `GetInput`.

## Building

You need the [Hollyhock 3](https://github.com/ClassPad-Dev/hollyhock-3) toolchain and SDK (the `ghcr.io/classpaddev/hollyhock-3` image has both, as used by the Chess project's dev container).

```sh
export SDK_DIR=/sdk        # default
make                       # -> GameOfLife.elf and GameOfLife.hh3
```

## Tests (run on the PC, not the calculator)

```sh
make -C test test          # engine + renderer tests (needs only the SDK headers)
make -C test shots         # runs the real UI headlessly, writes screenshots to test/out/
make -C test perf          # frame timings on the host
```

* `test/test_engine.cpp` compares the engine against a naive reference on random edits, soups and clears (this is what exercises the tile skipping), and checks known results: a glider's path and full lap of the torus, the R-pentomino stabilising at 116 cells in generation 1103, and Diehard dying in generation 130.
* `test/test_render.cpp` compares every pixel at every zoom level against a slow reference, including partial cells at the edge of the screen and the empty area outside the world.
* `test/host_app.cpp` runs `main.cpp` unchanged against a fake LCD and scripted input (`test/script*.txt`).

## Source layout

| File | |
| --- | --- |
| `life.h/.cpp` | the engine (no SDK dependencies) |
| `render.h/.cpp` | grid to pixels, all zoom levels |
| `gfx.h/.cpp` | framebuffer helpers and the 5×7 font |
| `patterns.h` | patterns for the `PATN` button |
| `main.cpp` | input, buttons, HUD, main loop |

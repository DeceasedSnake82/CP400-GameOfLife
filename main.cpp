// ============================================================================
//  Conway's Game of Life for the CP400 (Hollyhock 3)
//
//  World : 2048 x 2048 wrap-around grid, simulated by life.cpp
//  View  : top 418 px of the screen, drawn by render.cpp (12 zoom levels)
//  UI    : buttons + status text in the bottom panel
//
//  Controls (also shown by the HELP button):
//    Touch      tap a cell to toggle it, drag to paint
//    Arrow keys move the view (hold to speed up)
//    + / -      zoom in / out
//    EXE        play / pause           .  single step
//    x / div    faster / slower        0  random soup     5  stamp a pattern
//    1          centre the view        Backspace  clear   =  help
//    Power/Clear  quit
// ============================================================================

#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <sdk/os/mem.h>
#include <stdint.h>

#include "life.h"
#include "gfx.h"
#include "render.h"
#include "patterns.h"

APP_NAME("Game of Life")
APP_DESCRIPTION("Conway's Game of Life on a 2048x2048 wrap-around grid")
APP_AUTHOR("DeceasedSnake82")
APP_VERSION("1.0.0")

using gfx::rgb;
using life::GRID;

namespace {

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
constexpr int PANEL_H = 110;
constexpr int VIEW_H  = gfx::H - PANEL_H;
constexpr int HUD_Y1  = VIEW_H + 4;
constexpr int HUD_Y2  = VIEW_H + 22;
constexpr int BTN_Y0  = VIEW_H + 42;
constexpr int BTN_W   = 64;
constexpr int BTN_H   = 34;
constexpr int BTN_COLS = 5;

constexpr uint16_t COL_PANEL   = rgb(22, 24, 34);
constexpr uint16_t COL_BTN     = rgb(54, 60, 84);
constexpr uint16_t COL_BTN_EDGE= rgb(110, 118, 150);
constexpr uint16_t COL_PLAY    = rgb(36, 140, 66);
constexpr uint16_t COL_PAUSE   = rgb(206, 140, 24);
constexpr uint16_t COL_DANGER  = rgb(150, 48, 48);
constexpr uint16_t COL_ARMED   = rgb(224, 36, 36);
constexpr uint16_t COL_TEXT    = rgb(240, 240, 240);
constexpr uint16_t COL_DIM     = rgb(150, 156, 176);
constexpr uint16_t COL_MSG     = rgb(250, 214, 90);
constexpr uint16_t COL_HELP_BG = rgb(10, 12, 20);

enum Button { B_PLAY, B_STEP, B_SLOW, B_FAST, B_CLEAR, B_ZOUT, B_ZIN, B_RAND, B_PATN, B_HELP, B_COUNT };

// Simulation speed: run `gens` generations every `frames` frames. There is no
// timer in the SDK, so one frame (simulate + draw + LCD refresh) is the unit.
struct Speed { uint8_t gens, frames; };
const Speed SPEEDS[] = { {1,24}, {1,12}, {1,6}, {1,3}, {1,1}, {2,1}, {4,1}, {8,1}, {16,1}, {32,1}, {64,1} };
constexpr int NUM_SPEEDS = (int)(sizeof(SPEEDS) / sizeof(SPEEDS[0]));
constexpr int DEFAULT_SPEED = 4;
constexpr int TILE_BUDGET = 12000;      // max tiles computed per frame (~3 fully dense generations)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
life::World world;
int  viewX, viewY;                      // cell shown at the top-left pixel
int  zoom = render::DEFAULT_ZOOM;
int  speed = DEFAULT_SPEED;
int  slowCounter;
int  patIdx;
bool running, quit, helpOpen, clearArmed;
bool viewDirty = true, hudDirty = true, btnDirty = true;
uint32_t seed = 0x9E3779B9u;
char msg[40];

// touch painting
bool painting, paintValue;
int  lastCx, lastCy;

// arrow keys
int  panHeld;
int  evDx, evDy;                        // arrow presses seen this frame

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void setMsg(const char* s) { gfx::putStr(msg, s); hudDirty = true; }

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void clampView() { render::clampView(zoom, VIEW_H, viewX, viewY); }

void centerOn(int cx, int cy) {
    viewX = cx - render::visCols(zoom) / 2;
    viewY = cy - render::visRows(zoom, VIEW_H) / 2;
    clampView();
}

int centerX() { return clampi(viewX + render::visCols(zoom) / 2, 0, GRID - 1); }
int centerY() { return clampi(viewY + render::visRows(zoom, VIEW_H) / 2, 0, GRID - 1); }

void stampPattern(const Pattern& p, int cx, int cy) {
    int w = 0, h = 0;
    for (; p.rows[h]; ++h) { int n = 0; while (p.rows[h][n]) ++n; if (n > w) w = n; }
    const int ox = clampi(cx - w / 2, 0, GRID - w), oy = clampi(cy - h / 2, 0, GRID - h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; p.rows[y][x]; ++x)
            if (p.rows[y][x] == 'O') world.set(ox + x, oy + y, true);
}

// ---------------------------------------------------------------------------
// Actions (shared by buttons and keys)
// ---------------------------------------------------------------------------
void actPlayPause() {
    running = !running;
    slowCounter = 0;
    btnDirty = hudDirty = true;
}

void actStep() {
    running = false;
    world.step();
    viewDirty = hudDirty = btnDirty = true;
}

void actFaster() { if (speed < NUM_SPEEDS - 1) ++speed; hudDirty = true; }
void actSlower() { if (speed > 0) --speed; hudDirty = true; }

void actZoom(int dir) {
    const int z = clampi(zoom + dir, 0, render::NUM_ZOOMS - 1);
    if (z == zoom) return;
    const int cx = centerX(), cy = centerY();
    zoom = z;
    centerOn(cx, cy);
    viewDirty = hudDirty = true;
}

void actClear() {
    if (!clearArmed) {
        clearArmed = true;
        setMsg("PRESS CLEAR AGAIN");
        btnDirty = true;
        return;
    }
    world.clear();
    running = false;
    clearArmed = false;
    setMsg("CLEARED");
    viewDirty = btnDirty = true;
}

void actRandom() {
    const int cx = centerX(), cy = centerY();
    int x0 = viewX, x1 = viewX + render::visCols(zoom), y0 = viewY, y1 = viewY + render::visRows(zoom, VIEW_H);
    if (x0 < cx - 256) x0 = cx - 256;          // cap the soup at 512 x 512 when zoomed far out
    if (x1 > cx + 256) x1 = cx + 256;
    if (y0 < cy - 256) y0 = cy - 256;
    if (y1 > cy + 256) y1 = cy + 256;
    seed ^= world.generation() * 2654435761u + (uint32_t)(viewX * 31 + viewY);
    world.fillRandom(x0, y0, x1, y1, seed);
    setMsg("RANDOM SOUP");
    viewDirty = true;
}

void actPattern() {
    const Pattern& p = PATTERNS[patIdx];
    stampPattern(p, centerX(), centerY());
    char buf[40];
    gfx::putStr(gfx::putStr(buf, "STAMPED "), p.name);
    setMsg(buf);
    patIdx = (patIdx + 1) % NUM_PATTERNS;
    viewDirty = true;
}

void actHome() { centerOn(GRID / 2, GRID / 2); viewDirty = hudDirty = true; }

void actHelp() { helpOpen = true; viewDirty = true; }

void runAction(int id) {
    if (id != B_CLEAR && clearArmed) { clearArmed = false; btnDirty = true; }
    switch (id) {
        case B_PLAY:  actPlayPause(); break;
        case B_STEP:  actStep();      break;
        case B_SLOW:  actSlower();    break;
        case B_FAST:  actFaster();    break;
        case B_CLEAR: actClear();     break;
        case B_ZOUT:  actZoom(-1);    break;
        case B_ZIN:   actZoom(+1);    break;
        case B_RAND:  actRandom();    break;
        case B_PATN:  actPattern();   break;
        case B_HELP:  actHelp();      break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
const char* buttonLabel(int id) {
    switch (id) {
        case B_PLAY:  return running ? "PAUSE" : "PLAY";
        case B_STEP:  return "STEP";
        case B_SLOW:  return "SLOW";
        case B_FAST:  return "FAST";
        case B_CLEAR: return clearArmed ? "SURE?" : "CLEAR";
        case B_ZOUT:  return "ZOOM-";
        case B_ZIN:   return "ZOOM+";
        case B_RAND:  return "RAND";
        case B_PATN:  return "PATN";
        default:      return "HELP";
    }
}

uint16_t buttonColor(int id) {
    if (id == B_PLAY)  return running ? COL_PAUSE : COL_PLAY;
    if (id == B_CLEAR) return clearArmed ? COL_ARMED : COL_DANGER;
    return COL_BTN;
}

void buttonRect(int id, int& x, int& y) {
    x = (id % BTN_COLS) * BTN_W + 1;
    y = BTN_Y0 + (id / BTN_COLS) * BTN_H;
}

void drawButtons() {
    for (int id = 0; id < B_COUNT; ++id) {
        int x, y;
        buttonRect(id, x, y);
        gfx::fillRect(x, y, BTN_W - 2, BTN_H - 2, buttonColor(id));
        gfx::frameRect(x, y, BTN_W - 2, BTN_H - 2, COL_BTN_EDGE);
        const char* s = buttonLabel(id);
        gfx::text(x + (BTN_W - 2 - gfx::textWidth(s, 2)) / 2, y + (BTN_H - 2 - 14) / 2, s, 2, COL_TEXT);
    }
}

void drawHud() {
    gfx::fillRect(0, VIEW_H, gfx::W, BTN_Y0 - VIEW_H, COL_PANEL);
    char buf[48];
    char* p = gfx::putStr(buf, "GEN ");
    p = gfx::putInt(p, (int32_t)world.generation());
    p = gfx::putStr(p, "  POP ");
    gfx::putInt(p, world.population());
    gfx::text(4, HUD_Y1, buf, 2, COL_TEXT);

    if (msg[0]) {
        gfx::text(4, HUD_Y2, msg, 2, COL_MSG);
    } else {
        p = gfx::putStr(buf, "X");
        p = gfx::putInt(p, centerX());
        p = gfx::putStr(p, " Y");
        p = gfx::putInt(p, centerY());
        p = gfx::putStr(p, " Z");
        const int z = render::ZOOMS[zoom];
        if (z > 0) p = gfx::putInt(p, z);
        else { p = gfx::putStr(p, "1/"); p = gfx::putInt(p, -z); }
        p = gfx::putStr(p, " SPD ");
        const Speed& s = SPEEDS[speed];
        if (s.gens > 1) { p = gfx::putStr(p, "x"); p = gfx::putInt(p, s.gens); }
        else if (s.frames > 1) { p = gfx::putStr(p, "1/"); p = gfx::putInt(p, s.frames); }
        else p = gfx::putStr(p, "x1");
        gfx::text(4, HUD_Y2, buf, 2, COL_DIM);
    }
}

void drawHelp() {
    static const char* const LINES[] = {
        "GAME OF LIFE - CONTROLS",
        "",
        "ARROWS   MOVE AROUND",
        "+ -      ZOOM IN / OUT",
        "EXE      PLAY / PAUSE",
        ".        STEP ONE",
        "TIMES    FASTER",
        "DIVIDE   SLOWER",
        "0        RANDOM SOUP",
        "5        STAMP PATTERN",
        "1        CENTRE VIEW",
        "BACKSP   CLEAR ALL",
        "=        THIS HELP",
        "POWER    QUIT",
        "",
        "TAP      TOGGLE CELL",
        "DRAG     PAINT CELLS",
        "",
        "TAP ANYWHERE TO CLOSE",
    };
    gfx::fillRect(0, 0, gfx::W, VIEW_H, COL_HELP_BG);
    int y = 10;
    for (const char* l : LINES) {
        gfx::text(8, y, l, 2, y == 10 ? COL_MSG : COL_TEXT);
        y += 20;
    }
}

void redraw() {
    if (viewDirty) {
        if (helpOpen) drawHelp();
        else render::drawWorld(world, viewX, viewY, VIEW_H, zoom);
    }
    if (hudDirty) drawHud();
    if (btnDirty) drawButtons();
    viewDirty = hudDirty = btnDirty = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool pixelToCell(int px, int py, int& cx, int& cy) {
    const int z = render::ZOOMS[zoom];
    if (z <= 0 || px < 0 || px >= gfx::W || py < 0 || py >= VIEW_H) return false;
    cx = viewX + px / z;
    cy = viewY + py / z;
    return cx >= 0 && cx < GRID && cy >= 0 && cy < GRID;
}

// Set every cell on the line (x0,y0)-(x1,y1) so fast drags leave no gaps.
void paintLine(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        world.set(x0, y0, paintValue);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
    viewDirty = hudDirty = true;
}

void onTouch(int dir, int x, int y) {
    if (dir == TOUCH_UP) { painting = false; return; }
    if (dir != TOUCH_DOWN && dir != TOUCH_HOLD_DRAG) return;

    if (dir == TOUCH_DOWN) {
        msg[0] = 0;
        if (helpOpen) { helpOpen = false; viewDirty = hudDirty = true; return; }
        if (y >= BTN_Y0) {
            const int col = x / BTN_W, row = (y - BTN_Y0) / BTN_H;
            if (x >= 0 && col < BTN_COLS && row < 2) runAction(row * BTN_COLS + col);
            hudDirty = true;
            return;
        }
        if (clearArmed) { clearArmed = false; btnDirty = true; }
        int cx, cy;
        if (pixelToCell(x, y, cx, cy)) {
            paintValue = !world.get(cx, cy);
            painting = true;
            lastCx = cx; lastCy = cy;
            paintLine(cx, cy, cx, cy);
        } else if (y < VIEW_H && render::ZOOMS[zoom] <= 0) {
            setMsg("ZOOM IN TO EDIT CELLS");
        }
    } else if (painting) {                          // drag
        int cx, cy;
        if (pixelToCell(x, y, cx, cy) && (cx != lastCx || cy != lastCy)) {
            paintLine(lastCx, lastCy, cx, cy);
            lastCx = cx; lastCy = cy;
        }
    }
}

void onKey(int code) {
    msg[0] = 0;
    hudDirty = true;
    if (helpOpen) {                                 // any key closes help (power still quits)
        helpOpen = false;
        viewDirty = true;
        if (code != KEYCODE_POWER_CLEAR) return;
    }
    switch (code) {
        case KEYCODE_POWER_CLEAR: quit = true; break;
        case KEYCODE_EXE:         runAction(B_PLAY);  break;
        case KEYCODE_DOT:         runAction(B_STEP);  break;
        case KEYCODE_PLUS:        runAction(B_ZIN);   break;
        case KEYCODE_MINUS:       runAction(B_ZOUT);  break;
        case KEYCODE_TIMES:       runAction(B_FAST);  break;
        case KEYCODE_DIVIDE:      runAction(B_SLOW);  break;
        case KEYCODE_0:           runAction(B_RAND);  break;
        case KEYCODE_5:           runAction(B_PATN);  break;
        case KEYCODE_BACKSPACE:   runAction(B_CLEAR); break;
        case KEYCODE_EQUALS:      runAction(B_HELP);  break;
        case KEYCODE_1:
            if (clearArmed) { clearArmed = false; btnDirty = true; }
            actHome();
            break;
        case KEYCODE_LEFT:        evDx = -1; break;
        case KEYCODE_RIGHT:       evDx = +1; break;
        case KEYCODE_UP:          evDy = -1; break;
        case KEYCODE_DOWN:        evDy = +1; break;
        default: break;
    }
}

// Returns false when the queue is empty.
bool handleEvent(const Input_Event& ev) {
    if (ev.type == EVENT_KEY) {
        if (ev.data.key.direction == KEY_PRESSED) onKey(ev.data.key.keyCode);
        return true;
    }
    if (ev.type == EVENT_TOUCH) {
        onTouch(ev.data.touch_single.direction, ev.data.touch_single.p1_x, ev.data.touch_single.p1_y);
        return true;
    }
    return false;
}

// Arrow keys: polled every frame so holding one scrolls smoothly (and
// accelerates); a quick tap that ends before the poll still moves one step
// thanks to the press event recorded in evDx/evDy.
bool panView() {
    int dx = evDx, dy = evDy;
    evDx = evDy = 0;
    if (Input_GetKeyState(ScancodeLeft))  dx = -1;
    if (Input_GetKeyState(ScancodeRight)) dx = +1;
    if (Input_GetKeyState(ScancodeUp))    dy = -1;
    if (Input_GetKeyState(ScancodeDown))  dy = +1;
    if (helpOpen || (!dx && !dy)) { panHeld = 0; return false; }

    ++panHeld;
    msg[0] = 0;                                     // show the coordinates while moving
    const int px = 4 + (panHeld / 3 < 5 ? panHeld / 3 : 5) * 4;           // 4 .. 24 screen pixels per frame
    const int z = render::ZOOMS[zoom];
    const int cells = z > 0 ? (px / z > 0 ? px / z : 1) : px * -z;
    viewX += dx * cells;
    viewY += dy * cells;
    clampView();
    viewDirty = hudDirty = true;
    return true;
}

void advance() {
    const Speed& s = SPEEDS[speed];
    if (++slowCounter < s.frames) return;
    slowCounter = 0;
    int work = 0;
    for (int g = 0; g < s.gens && work < TILE_BUDGET; ++g) {
        const int tiles = world.step();
        work += tiles;
        if (tiles == 0) {                           // nothing is changing any more
            running = false;
            setMsg(world.population() ? "STABLE - NOTHING MOVES" : "ALL CELLS DIED");
            btnDirty = true;
            break;
        }
    }
    if (world.population() == 0 && running) {
        running = false;
        setMsg("ALL CELLS DIED");
        btnDirty = true;
    }
    viewDirty = hudDirty = true;
}

}  // namespace

int main() {
    gfx::init();

    void* mem = Mem_Malloc(life::World::memoryNeeded());
    if (!mem) {
        gfx::fillRect(0, 0, gfx::W, gfx::H, rgb(0, 0, 0));
        gfx::text(8, 8, "OUT OF MEMORY", 2, rgb(255, 80, 80));
        LCD_Refresh();
        struct Input_Event e;
        GetInput(&e, 0xFFFFFFFF, 0x10);
        return 1;
    }
    world.init(mem);

    stampPattern(PATTERNS[0], GRID / 2, GRID / 2);   // something to look at: an R-pentomino
    patIdx = 1;
    actHome();
    setMsg("TAP CELLS, EXE = PLAY");

    redraw();
    LCD_Refresh();

    while (!quit) {
        const bool busy = running || panHeld > 0;

        // Block for input when nothing is animating; otherwise just poll.
        for (int n = 0; n < 64; ++n) {
            struct Input_Event ev;
            uint8_t* raw = (uint8_t*)&ev;
            for (unsigned i = 0; i < sizeof ev; ++i) raw[i] = 0;
            GetInput(&ev, (n == 0 && !busy) ? 0xFFFFFFFF : 0, 0x10);
            if (!handleEvent(ev) || quit) break;
        }
        if (quit) break;

        panView();
        if (running && !helpOpen) advance();

        if (viewDirty || hudDirty || btnDirty || running) {
            redraw();
            LCD_Refresh();           // while running this also paces the simulation
        }
    }

    Mem_Free(mem);
    return 0;
}

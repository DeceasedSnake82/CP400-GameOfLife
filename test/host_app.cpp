// Host harness: runs the real app (main.cpp, compiled with -Dmain=life_main) against a
// fake 320x528 LCD and scripted input, and writes screenshots as PPM files.
//
//   make -C test shots      # builds, runs test/script.txt, converts to PNG in test/out/
//
// Script commands (one per line, '#' starts a comment):
//   key NAME            press a key (EXE DOT PLUS MINUS TIMES DIVIDE 0 1 5 BACKSPACE EQUALS CLEAR LEFT RIGHT UP DOWN)
//   tap X Y             touch down + up
//   drag X1 Y1 X2 Y2    touch down, a few drag events, up
//   hold NAME N         hold an arrow key down for N frames
//   frames N            let the app run N loop iterations without input
//   shot NAME           save the LCD as out/NAME.ppm
//   time NAME N         print how long N frames took (host speed, not calculator speed)
#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <sdk/os/mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#undef main   // the harness owns main(); the app's main() is built as life_main()
extern int life_main();

static uint16_t g_vram[320 * 528];
static uint16_t g_lcd[320 * 528];
static long g_refreshes = 0;

static void host_refresh() { memcpy(g_lcd, g_vram, sizeof g_lcd); ++g_refreshes; }
static uint16_t* host_vram() { return g_vram; }
static void* host_malloc(size_t n) { return calloc(1, n); }
static void host_free(void* p) { free(p); }

static void shot(const char* name) {
    char path[256];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n320 528\n255\n");
    for (int i = 0; i < 320 * 528; ++i) {
        const uint16_t c = g_lcd[i];
        const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

struct Cmd { std::string op, a; int n[4]; };
static std::vector<Cmd> script;
static size_t pc = 0;
static int waitFrames = 0;
static int holdFrames = 0;
static uint16_t heldScan = 0;
static struct { int dir; int x, y; } pendingTouch[16];
static int nPending = 0, iPending = 0;
static bool quitSent = false;
static double timeStart = 0; static long timeRefreshStart = 0; static int timeFrames = 0; static std::string timeName;

static int keyCode(const std::string& k) {
    static const struct { const char* n; int c; } T[] = {
        {"EXE", KEYCODE_EXE}, {"DOT", KEYCODE_DOT}, {"PLUS", KEYCODE_PLUS}, {"MINUS", KEYCODE_MINUS},
        {"TIMES", KEYCODE_TIMES}, {"DIVIDE", KEYCODE_DIVIDE}, {"0", KEYCODE_0}, {"1", KEYCODE_1}, {"5", KEYCODE_5},
        {"BACKSPACE", KEYCODE_BACKSPACE}, {"EQUALS", KEYCODE_EQUALS}, {"CLEAR", KEYCODE_POWER_CLEAR},
        {"LEFT", KEYCODE_LEFT}, {"RIGHT", KEYCODE_RIGHT}, {"UP", KEYCODE_UP}, {"DOWN", KEYCODE_DOWN} };
    for (auto& t : T) if (k == t.n) return t.c;
    fprintf(stderr, "unknown key %s\n", k.c_str()); exit(1);
}
static int scanCode(const std::string& k) {
    if (k == "LEFT")  return ScancodeLeft;
    if (k == "RIGHT") return ScancodeRight;
    if (k == "UP")    return ScancodeUp;
    if (k == "DOWN")  return ScancodeDown;
    fprintf(stderr, "unknown scancode %s\n", k.c_str()); exit(1);
}

static void setKey(Input_Event* e, int code) {
    e->type = EVENT_KEY; e->data.key.direction = KEY_PRESSED; e->data.key.keyCode = (Input_Keycode)code;
}

static int host_getinput(Input_Event* e, uint32_t, uint32_t) {
    e->type = EVENT_NONE;
    if (iPending < nPending) {
        e->type = EVENT_TOUCH;
        e->data.touch_single.direction = (Input_TouchEventType)pendingTouch[iPending].dir;
        e->data.touch_single.p1_x = pendingTouch[iPending].x;
        e->data.touch_single.p1_y = pendingTouch[iPending].y;
        ++iPending;
        return 0;
    }
    nPending = iPending = 0;
    if (waitFrames > 0) { --waitFrames; ++timeFrames; return 0; }
    if (holdFrames > 0) { if (--holdFrames == 0) heldScan = 0; return 0; }
    while (pc < script.size()) {
        const Cmd& c = script[pc++];
        if (c.op == "key") { setKey(e, keyCode(c.a)); return 0; }
        if (c.op == "tap") {
            pendingTouch[0] = { TOUCH_DOWN, c.n[0], c.n[1] }; pendingTouch[1] = { TOUCH_UP, c.n[0], c.n[1] };
            nPending = 2; iPending = 0; return host_getinput(e, 0, 0);
        }
        if (c.op == "drag") {
            nPending = 0;
            pendingTouch[nPending++] = { TOUCH_DOWN, c.n[0], c.n[1] };
            for (int i = 1; i <= 6; ++i)
                pendingTouch[nPending++] = { TOUCH_HOLD_DRAG, c.n[0] + (c.n[2] - c.n[0]) * i / 6, c.n[1] + (c.n[3] - c.n[1]) * i / 6 };
            pendingTouch[nPending++] = { TOUCH_UP, c.n[2], c.n[3] };
            iPending = 0; return host_getinput(e, 0, 0);
        }
        if (c.op == "hold") { heldScan = (uint16_t)scanCode(c.a); holdFrames = c.n[0]; return 0; }
        if (c.op == "frames") { waitFrames = c.n[0]; timeFrames = 0; timeStart = now(); timeRefreshStart = g_refreshes; return 0; }
        if (c.op == "shot") { shot(c.a.c_str()); continue; }
        if (c.op == "time") {
            // this command is processed after a preceding "frames N": report per-frame cost
            double dt = now() - timeStart;
            printf("%-24s %6.2f ms/frame over %d frames (%ld refreshes)\n", c.a.c_str(), dt * 1000 / (timeFrames ? timeFrames : 1), timeFrames, g_refreshes - timeRefreshStart);
            continue;
        }
        fprintf(stderr, "bad script op %s\n", c.op.c_str()); exit(1);
    }
    if (!quitSent) { quitSent = true; setKey(e, KEYCODE_POWER_CLEAR); }
    return 0;
}

static bool host_keystate(uint16_t* scancode) { return heldScan && *scancode == heldScan; }

// Satisfy the SDK's extern function pointers
int (*GetInput)(struct Input_Event*, uint32_t, uint32_t) = host_getinput;
bool (*Input_GetKeyStateU)(uint16_t*) __asm__("Input_GetKeyState") = host_keystate;
void (*LCD_Refresh)() = host_refresh;
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;
void* (*_FP_Mem_Malloc)(size_t) __asm__("Mem_Malloc") = host_malloc;
void (*Mem_Free)(void*) = host_free;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "script.txt";
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char op[32] = "", a[64] = ""; int n[4] = {0, 0, 0, 0};
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%31s", op) != 1) continue;
        Cmd c; c.op = op;
        if (c.op == "key" || c.op == "shot" || c.op == "time") sscanf(line, "%*s %63s", a);
        else if (c.op == "hold") sscanf(line, "%*s %63s %d", a, &n[0]);
        else sscanf(line, "%*s %d %d %d %d", &n[0], &n[1], &n[2], &n[3]);
        c.a = a; memcpy(c.n, n, sizeof n);
        script.push_back(c);
    }
    fclose(f);
    life_main();
    return 0;
}

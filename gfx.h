// Minimal framebuffer helpers (RGB565 VRAM, 320x528) and a 5x7 bitmap font.
#pragma once

#include <stdint.h>

namespace gfx {

constexpr int W = 320;
constexpr int H = 528;

// 32-bit access to the framebuffer without breaking strict aliasing.
typedef uint32_t __attribute__((may_alias)) u32a;

extern uint16_t* fb;            // set by init()
void init();                    // grab the VRAM pointer

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Duplicate a colour into both halves of a 32-bit word (endian neutral).
constexpr uint32_t pair(uint16_t c) { return ((uint32_t)c << 16) | c; }

void fillRect(int x, int y, int w, int h, uint16_t c);
void frameRect(int x, int y, int w, int h, uint16_t c);          // 1px outline
void fillSpan(uint16_t* dst, int pixels, uint16_t c);            // dst must be 4-byte aligned
void copyPixels(uint16_t* dst, const uint16_t* src, int pixels); // both 4-byte aligned, pixels even

int  textWidth(const char* s, int scale);
void text(int x, int y, const char* s, int scale, uint16_t c);
// Tiny helpers for building HUD strings without libc.
char* putStr(char* d, const char* s);
char* putInt(char* d, int32_t v);

}  // namespace gfx

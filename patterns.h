// Classic patterns the PATN button can stamp into the world ('O' = alive).
#pragma once

struct Pattern {
    const char* name;
    const char* const* rows;     // null terminated
};

static const char* const PAT_GLIDER[]   = { ".O.", "..O", "OOO", 0 };
static const char* const PAT_LWSS[]     = { ".O..O", "O....", "O...O", "OOOO.", 0 };
static const char* const PAT_RPENT[]    = { ".OO", "OO.", ".O.", 0 };
static const char* const PAT_ACORN[]    = { ".O.....", "...O...", "OO..OOO", 0 };
static const char* const PAT_DIEHARD[]  = { "......O.", "OO......", ".O...OOO", 0 };
static const char* const PAT_PI[]       = { "OOO", "O.O", "O.O", 0 };
static const char* const PAT_PENTADEC[] = { "..O....O..", "OO.OOOO.OO", "..O....O..", 0 };
static const char* const PAT_PULSAR[]   = {
    "..OOO...OOO..",
    ".............",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    "..OOO...OOO..",
    ".............",
    "..OOO...OOO..",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    ".............",
    "..OOO...OOO..", 0 };
static const char* const PAT_GUN[] = {
    "........................O...........",
    "......................O.O...........",
    "............OO......OO............OO",
    "...........O...O....OO............OO",
    "OO........O.....O...OO..............",
    "OO........O...O.OO....O.O...........",
    "..........O.....O.......O...........",
    "...........O...O....................",
    "............OO......................", 0 };

static const Pattern PATTERNS[] = {
    { "R-PENTOMINO", PAT_RPENT },
    { "GLIDER",      PAT_GLIDER },
    { "GLIDER GUN",  PAT_GUN },
    { "LWSS",        PAT_LWSS },
    { "ACORN",       PAT_ACORN },
    { "DIEHARD",     PAT_DIEHARD },
    { "PI HEPTOMINO",PAT_PI },
    { "PENTADECATHLON", PAT_PENTADEC },
    { "PULSAR",      PAT_PULSAR },
};
static const int NUM_PATTERNS = (int)(sizeof(PATTERNS) / sizeof(PATTERNS[0]));

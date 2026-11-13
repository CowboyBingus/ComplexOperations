/* The war-table tag (co_tag.c): a banner beside the native operation and briefing panels that says what the highlighted
 * level-11 operation, or the picked mission, will start as. Its state comes from the launch's own decision functions on
 * the launch's own inputs; its place, from the native panel's rectangle. PROOFS.md W-1 to W-6 back the numbers here. */
#ifndef CO_TAG_H
#define CO_TAG_H

#include <stdint.h>

#include "co.h"

enum { CO_TAG_NONE, CO_TAG_COMPLEX, CO_TAG_STANDARD, CO_TAG_OFF, CO_TAG_WAITING, CO_TAG_CLEARED };
enum { CO_TAG_WHY_NONE, CO_TAG_WHY_HOST, CO_TAG_WHY_PC, CO_TAG_WHY_BUILD };
enum { CO_TAG_SCREEN_NONE, CO_TAG_SCREEN_MAP, CO_TAG_SCREEN_BRIEFING };

/* What the launch decides with, for one level-11 operation or mission. `clears` is the host's launch-clear
 * prediction for the picked mission (co_launch_would_clear), asked on the briefing only. */
typedef struct CoTagInputs { int level11, marked, is_host, own_on, host_row, briefing, clears, build; } CoTagInputs;
int co_tag_state(const CoTagInputs* in, int* why);

/* The operation the war table highlights: `index` its board record, `level11` and `marked` over the mission entries
 * whose +0x28 is that index. The briefing's mission: the first entry whose +0x2C and +0x3C are the selection's. 1 read
 * (*found 0: no such operation or mission), 0 refused (the table's count is past its entries). */
int co_tag_map_entries(uintptr_t war, uint32_t* index, uint32_t* entry, int* level11, int* marked, int* found);
int co_tag_briefing_entry(uintptr_t war, uint32_t* index, uint32_t* entry, int* level11, int* marked, int* found);
/* The mark the fill gives board record `index`'s level-11 entries while it rolls: its operation seed (+0xC) and planet
 * (+0x10) through co_complex_entry_seed. A client's table filled before the host's answer holds no mark; this does.
 * 1 read, 0 refused (no such record in use). */
int co_tag_board_mark(uintptr_t war, uint32_t index, int* marked);

/* Layout, in the native panel's UI units times its scale (y up, as the Gui draws). */
#define CO_TAG_BORDER 3.0
#define CO_TAG_INSET 7.0
#define CO_TAG_EDGE 5.0
#define CO_TAG_PAD 22.0
#define CO_TAG_SIDE 12.0   /* from the panel, and from the screen's edges: Know Your Constellation's side gap */
#define CO_TAG_RAISE 6.0   /* above the panel's top line */
#define CO_TAG_TITLE 18.0
#define CO_TAG_CAPTION 13.0
#define CO_TAG_HEIGHT (2 * (CO_TAG_BORDER + CO_TAG_INSET + CO_TAG_EDGE) + CO_TAG_TITLE * 1.3 + CO_TAG_CAPTION + CO_TAG_TITLE * 0.3)
#define CO_TAG_LAYER_SHADE 1011.0
#define CO_TAG_LAYER_BODY 1012.0
#define CO_TAG_LAYER_BORDER 1013.0
#define CO_TAG_LAYER_TEXT 1014.0
enum { CO_TAG_PLACE_NONE, CO_TAG_PLACE_BESIDE, CO_TAG_PLACE_COLUMN };
typedef struct CoTagAnchor { double x, y, w, h, scale; uint64_t font, material, atlas; } CoTagAnchor;
typedef struct CoTagBox { double x, y, w, h; } CoTagBox;
int co_tag_place(const CoTagAnchor* a, double width, double height, CoTagBox* out);

typedef struct CoTagColour { uint8_t a, r, g, b; } CoTagColour;
typedef struct CoTagRect { double x, y, w, h, z; CoTagColour c; } CoTagRect;
typedef struct CoTagText { const char* text; double size, x, y, z; CoTagColour c; } CoTagText;
#define CO_TAG_RECTS 6
typedef struct CoTagPlan { CoTagRect rect[CO_TAG_RECTS]; CoTagText text[2]; } CoTagPlan;
const char* co_tag_title(int state);
const char* co_tag_caption(int state, int why);
void co_tag_plan(int state, int why, const CoTagBox* box, double scale, CoTagPlan* out);

/* The game side: `install` once with game.dll's base (NULL, or why the tag stays off); `tick` from the game-thread
 * tick. */
const char* co_tag_install(uintptr_t game);
void co_tag_tick(void);
#define CO_TAG_SETTLE 30 /* frames a state holds before its line is logged */

#ifdef CO_TEST
void co_tag_test_reset(void);
void co_tag_test_pins(uint8_t* image); /* the game's readers the install checks, into a stand-in image */
/* What the tag drew last: the state, the GUI (0 none), whether its items show, the box. */
typedef struct CoTagShown { int state, why, screen, place, visible; double gui, world; CoTagBox box; uint32_t index; } CoTagShown;
const CoTagShown* co_tag_test_shown(void);
#endif

#endif

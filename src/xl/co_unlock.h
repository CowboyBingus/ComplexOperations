/* A main objective waits for the steps of its own mission. The game's objective update holds every main (class 0)
 * while any step (class 1) on the map is not done: at the activation pass (0x5D36A8) and at the one-time waiting pass
 * (0x5D3F8B). On a complex operation the map holds several missions, so each pair is asked whether the step is of the
 * main's mission; a pair whose mission is not known keeps the game's answer. The war table's hologram draws its lines
 * from a step to the mains of its own mission by the same note.
 *
 * Only the peer that owns the objectives (the host) runs these passes; the others copy the state it sends, so nothing
 * new goes over the network. A normal level 11, vanilla and the ship run the game's own bytes. */
#ifndef CO_UNLOCK_H
#define CO_UNLOCK_H

#include "co.h"

#define CO_UNLOCK_NONE 0xFFu /* no mission known */
#define CO_UNLOCK_NOTED 64u  /* the most objective locations a note holds (the arcs' objectives) */

/* The note of a generation (cluster pass, ObjectiveArcs): location index, its objective id (+0xD8), its cluster (+0xE0)
 * and mission. Every mission repeats the ids; the cluster tells their copies apart. An objective that shares id and
 * cluster with another mission's is not noted. The process has one level generator, so one note: a level init forgets it. */
void co_unlock_note(uintptr_t lg, uintptr_t gen, const uint32_t* location, const uint32_t* id, const uint64_t* cluster, const uint8_t* mission, uint32_t n);
void co_unlock_forget(void);
/* The noted mission of location `l`, when the note is of `lg`/`gen` and the location still has the noted id and cluster. */
uint8_t co_unlock_noted(uintptr_t lg, uintptr_t gen, uint32_t l, uint32_t id, uint64_t cluster);
/* 1 when the step is to hold the main: the game's map-wide answer unless both missions are known and differ. */
int co_unlock_rule(uint8_t mainMission, uint8_t stepMission);

/* The mission in play: 1 a complex one. Nothing before the install. */
void co_unlock_live(int complex);
/* Called by the stubs with the objective manager and two instance indexes. */
uint64_t co_unlock_holds(uintptr_t manager, uint32_t main, uint32_t step);

/* The activation pass: `mov rax,[rdx+0x68]; movzx esi,sil; cmp dword [rax+rbx+0x20],2; mov eax,1; cmovne esi,eax`
 * (rdx the manager, rbx the step's net record offset, edi the step, r13d the main). */
#define CO_UNLOCK_A_SITE 0x5D36A8u
#define CO_UNLOCK_A_BACK 0x5D36BDu
#define CO_UNLOCK_A_STUB 103u
/* The waiting pass: `mov rax,[rax+0x68]; cmp dword [rdx+rax+0x20],2; jne 0x5D3FAF` (rcx the step, r11d the main,
 * [rsp+0x30] the manager); not done holds the main at 0x5D3FAF, else the loop goes on at 0x5D3F96. */
#define CO_UNLOCK_B_SITE 0x5D3F8Bu
#define CO_UNLOCK_B_HOLD 0x5D3FAFu
#define CO_UNLOCK_B_NEXT 0x5D3F96u
#define CO_UNLOCK_B_STUB 111u
extern const uint8_t kCoUnlockA[21], kCoUnlockB[11];
size_t co_unlock_stub_a(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds);
size_t co_unlock_stub_b(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds);

/* The hologram: the marker source (0xF736C0) and a step's line draw (0x14324E0). */
#define CO_UNLOCK_MARKERS 0xF736C0u
#define CO_UNLOCK_LINES 0x14324E0u
#define CO_UNLOCK_SLOTS 32u
/* Which slots a step's lines skip: the slots of another known mission. `mission` per slot (CO_UNLOCK_NONE unknown);
 * returns a bit per slot, 0 when the step's own mission is not known. */
uint32_t co_unlock_hide(const uint8_t* mission, uint32_t slots, uint32_t step);

/* NULL when the two sites hold the game's bytes and the hologram's two functions their pins. Reads only. */
const char* co_unlock_check(uintptr_t game);
/* Both stubs and both hooks; `list` is the important locations' offset in the generation block (HellForge's). NULL,
 * or why: then nothing was written and every main waits for every step on the map, as in the game. */
const char* co_unlock_install(uintptr_t game, uint32_t list);

#ifdef CO_TEST
void co_unlock_test_reset(void);
const uint8_t* co_unlock_test_page(void);
uint32_t co_unlock_test_markers(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out);
uint64_t co_unlock_test_lines(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha);
#endif

#endif

/* Sites with two paths (co_switch.h). A batch's page: per site 0x100 bytes, the jump through its slot at +0, the game's
 * path at +8, the mod's at +0x80; the slots in the page after it, which stays writable. */
#include <string.h>

#include <windows.h>

#include "co_switch.h"

#define CO_SWITCH_ROOM 0x100u
#define CO_SWITCH_BATCH 16u
#define CO_SWITCH_MAX 64u

typedef struct { uintptr_t site; volatile uintptr_t* slot; uintptr_t game, mod; int group; } CoSwitchOn;
static CoSwitchOn g_on[CO_SWITCH_MAX];
static int g_count;
static volatile LONG g_live[CO_SWITCH_GROUPS];

/* A rel32 from the end of a field at `end` to `to`; 0 when out of reach. */
static int Rel(uint8_t* field, uintptr_t end, uintptr_t to)
{
    const int64_t d = (int64_t)to - (int64_t)end;
    if (d < INT32_MIN || d > INT32_MAX) return 0;
    const int32_t r = (int32_t)d;
    memcpy(field, &r, 4);
    return 1;
}

static int Jump(uint8_t* at, uintptr_t to) { at[0] = 0xE9; return Rel(at + 1, (uintptr_t)at + 5u, to); }

const char* co_switch_check(uintptr_t game, const CoSwitchSite* sites, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        uint8_t now[96];
        if (sites[i].size > sizeof now || g_coApi->read(game + sites[i].rva, now, sites[i].size) != 0 || memcmp(now, sites[i].pin, sites[i].size) != 0)
            return sites[i].what;
    }
    return NULL;
}

/* The game's bytes moved: each rel32 field kept on its old target. */
static int Fix(uint8_t* out, const CoSwitchFix* fix)
{
    for (int k = 0; k < 2 && fix[k].kind; k++)
    {
        const CoSwitchFix* f = &fix[k];
        if (f->kind == CO_FIX_IMM64) memcpy(out + f->at, &f->to, 8);
        else if (!Rel(out + f->at, (uintptr_t)out + f->at + 4u, f->to)) return 0;
    }
    return 1;
}

static int GamePath(uint8_t* out, uintptr_t game, const CoSwitchSite* s)
{
    if (s->game != NULL) { memcpy(out, s->game, s->copy); return Fix(out, s->gameFix) && Jump(out + s->copy, game + s->back); }
    memcpy(out, s->pin, s->copy);
    for (int k = 0; k < 3 && s->rel[k]; k++)
    {
        int32_t was;
        memcpy(&was, s->pin + s->rel[k], 4);
        const uintptr_t to = game + s->rva + s->rel[k] + 4u + (uintptr_t)(intptr_t)was;
        if (!Rel(out + s->rel[k], (uintptr_t)out + s->rel[k] + 4u, to)) return 0;
    }
    return Jump(out + s->copy, game + s->back);
}

static int ModPath(uint8_t* out, uintptr_t game, const CoSwitchSite* s)
{
    memcpy(out, s->mod, s->modLen);
    return Fix(out, s->fix) && Jump(out + s->modLen, game + s->modBack);
}

const char* co_switch_install(uintptr_t game, const CoSwitchSite* sites, size_t count, int group)
{
    const char* why;
    if (group < 0 || group >= CO_SWITCH_GROUPS) return "no such group of switched sites";
    if (count == 0 || count > CO_SWITCH_BATCH || g_count + (int)count > (int)CO_SWITCH_MAX) return "too many switched sites";
    if ((why = co_switch_check(game, sites, count)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + sites[0].rva, 0x2000);
    if (page == NULL) return "no memory near the game for the switched sites";
    volatile uintptr_t* slots = (volatile uintptr_t*)(page + 0x1000);
    memset(page, 0xCC, 0x1000);
    const int live = g_live[group] != 0;
    for (size_t i = 0; i < count; i++)
    {
        const CoSwitchSite* s = &sites[i];
        uint8_t* room = page + i * CO_SWITCH_ROOM;
        if (s->copy + 5u > 0x78u || s->modLen + 5u > 0x80u) { VirtualFree(page, 0, MEM_RELEASE); return "a switched site's path does not fit"; }
        room[0] = 0xFF; room[1] = 0x25; /* jmp [rip+slot] */
        if (!Rel(room + 2, (uintptr_t)room + 6u, (uintptr_t)&slots[i]) || !GamePath(room + 8, game, s) || !ModPath(room + 0x80, game, s))
        { VirtualFree(page, 0, MEM_RELEASE); return "a switched site's path is out of reach"; }
        slots[i] = (uintptr_t)(room + (live ? 0x80 : 8));
    }
    DWORD old;
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the switched sites' paths could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    for (size_t i = 0; i < count; i++)
    {
        uint8_t jump[96];
        memset(jump, sites[i].fill, sizeof jump);
        if (sites[i].after != NULL) memcpy(jump + 5, sites[i].after, sites[i].size - 5u);
        jump[0] = 0xE9; /* its rel32 from the site's own place, not this buffer's */
        if (!Rel(jump + 1, game + sites[i].rva + 5u, (uintptr_t)(page + i * CO_SWITCH_ROOM)) || g_coApi->write(game + sites[i].rva, jump, sites[i].size) != 0)
        {
            for (size_t j = 0; j < i; j++) g_coApi->write(game + sites[j].rva, sites[j].pin, sites[j].size);
            VirtualFree(page, 0, MEM_RELEASE);
            return "a switched site could not be written";
        }
    }
    for (size_t i = 0; i < count; i++)
    {
        uint8_t* room = page + i * CO_SWITCH_ROOM;
        g_on[g_count++] = (CoSwitchOn){game + sites[i].rva, &slots[i], (uintptr_t)(room + 8), (uintptr_t)(room + 0x80), group};
    }
    if ((g_live[group] != 0) != live) co_switch_live(group, g_live[group] != 0); /* the mission changed kind meanwhile */
    return NULL;
}

void co_switch_live(int group, int on)
{
    if (group < 0 || group >= CO_SWITCH_GROUPS) return;
    InterlockedExchange(&g_live[group], on != 0);
    for (int i = 0; i < g_count; i++)
        if (g_on[i].group == group) *g_on[i].slot = on ? g_on[i].mod : g_on[i].game;
}

int co_switch_path(uintptr_t site)
{
    for (int i = 0; i < g_count; i++)
        if (g_on[i].site == site) return *g_on[i].slot == g_on[i].mod;
    return -1;
}

#ifdef CO_TEST
void co_switch_test_reset(void) { memset(g_on, 0, sizeof g_on); g_count = 0; memset((void*)g_live, 0, sizeof g_live); }
#endif

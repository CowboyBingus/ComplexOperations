/* The loadout cards' figures for players 5 to 8 against a stand-in game image: the records' move to a block of twelve,
 * the game's own code running on the moved records, the caves for records 8 to 11, the way back and the refusals. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_figure_code.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
uintptr_t co_counts_test_caves(void);
unsigned co_counts_test_room(void);
int co_counts_test_players(void);
int co_counts_test_write_of(uint32_t rva);
int co_counts_test_active(uint8_t group);
uint32_t co_counts_test_refused(void);
uintptr_t co_counts_test_figure_records(void);
unsigned co_counts_test_figure_fields(void);
uint32_t co_counts_test_figure_field(unsigned i, uint32_t* at, uint32_t* old);
void co_counts_undo(void);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_managers_test_force(int state);
void co_ui_test_reset(void);
void co_ui_test_force(int layout);
void co_extract_test_reset(void);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define G_FIGURES 18
#define G_FIGURES4 19
#define RECORDS 0xC008u
#define STRIDE 0xA0u
#define OLD_SIZE 0x500u
#define NEW_SIZE 0x780u
#define MASK (RECORDS + OLD_SIZE + 4u)
#define WORLD 0x3C48u                /* the world the destroy at 0x138FF5B compares, in its rdi */

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef void (*DestroyFn)(void* object, uint64_t value, const void* target);
typedef void (*WorldFn)(uint64_t world, const void* target);
typedef struct { uint32_t r15d, r12d; uint64_t r14; uint32_t eax; } LoopRegs;
typedef void (*LoopFn)(LoopRegs* regs, const void* target);
typedef void (*FrameFn)(void* f, void* frame, const void* target);
typedef uint32_t (*ValueFn)(uint32_t value, const void* target);
typedef void (*ReadFn)(void* unused, uint32_t* out, uint32_t equipment, uint32_t record);
typedef const float* (*TableFn)(void);

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;
static uint8_t* g_thunks;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* Ways into the game's code and the caves with the registers they read (tests/fig thunks, assembled with GNU as). */
static const uint8_t kDestroy[] = {0x53, 0x57, 0x48, 0x89, 0xCF, 0x48, 0x89, 0xD3, 0x41, 0xFF, 0xD0, 0x5F, 0x5B, 0xC3};
static const uint8_t kWorld[] = {0x48, 0x89, 0xD0, 0x48, 0x89, 0xCA, 0xFF, 0xD0, 0xC3};
static const uint8_t kLoop[] = {0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x53, 0x48, 0x89, 0xCB, 0x44, 0x8B, 0x3B, 0x44, 0x8B, 0x63,
                                0x04, 0x4C, 0x8B, 0x73, 0x08, 0xFF, 0xD2, 0x44, 0x89, 0x3B, 0x44, 0x89, 0x63, 0x04, 0x4C, 0x89,
                                0x73, 0x08, 0x89, 0x43, 0x10, 0x5B, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5C, 0xC3};
static const uint8_t kFrame[] = {0x53, 0x55, 0x48, 0x89, 0xCB, 0x48, 0x89, 0xD5, 0x41, 0xFF, 0xD0, 0x5D, 0x5B, 0xC3};
static const uint8_t kEbp[] = {0x55, 0x89, 0xCD, 0xFF, 0xD2, 0x5D, 0xC3};
static const uint8_t kR13[] = {0x41, 0x55, 0x41, 0x89, 0xCD, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0xFF, 0xD2,
                               0x48, 0x81, 0xC4, 0x90, 0x00, 0x00, 0x00, 0x41, 0x5D, 0xC3};
/* Where the game's code goes on, the test returns what it needs. */
static const uint8_t kRet[] = {0xC3};
static const uint8_t kOne[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
static const uint8_t kTwo[] = {0xB8, 0x02, 0x00, 0x00, 0x00, 0xC3};
static const uint8_t kNone[] = {0xB8, 0xFE, 0xFF, 0xFF, 0xFF, 0xC3};
static const uint8_t kEbpBack[] = {0x89, 0xE8, 0xC3};
static const uint8_t kSetb[] = {0x0F, 0x92, 0xC0, 0x0F, 0xB6, 0xC0, 0xC3};
static const uint8_t kRcx[] = {0x48, 0x89, 0xC8, 0xC3};

static const void* Thunk(const uint8_t* code, size_t size)
{
    static size_t used;
    if (g_thunks == NULL) g_thunks = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (g_thunks == NULL || used + size > 0x1000) return NULL;
    memcpy(g_thunks + used, code, size);
    used += (size + 15u) & ~(size_t)15u;
    return g_thunks + used - ((size + 15u) & ~(size_t)15u);
}

static void Landing(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

static uint8_t* Ups(void) { return g_fake.image + FAKE_UPS; }

static void Start(const char* name)
{
    const HfApi* api;
    const uint64_t none = 0;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    co_counts_test_reset();
    co_ui_test_reset();
    co_extract_test_reset();
    co_registry_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    for (size_t i = 0; i < sizeof kFigureCode / sizeof kFigureCode[0]; i++)
        memcpy(g_fake.image + kFigureCode[i].rva, kFigureCode[i].code, kFigureCode[i].size);
    co_session_test_fill(g_fake.image);
    co_counts_test_fill(g_fake.image);
    co_registry_test_fill(g_fake.image);
    /* Records 0 to 7 as the game left them, each byte telling where it came from. */
    for (uint32_t i = 0; i < OLD_SIZE; i++) Ups()[RECORDS + i] = (uint8_t)(i * 7u + 1u);
    memset(Ups() + MASK, 0, 4);
    memcpy(g_fake.image + SESSION_GLOBAL, &none, 8);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
}

static void Init(void)
{
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
    co_managers_test_force(1);
    co_ui_test_force(1);
}

/* The jump at a cave's site, the rest of the replaced bytes as nops; the cave it leads to. */
static const uint8_t* Cave(uint32_t site, uint32_t size)
{
    const uint8_t* image = g_fake.image;
    const uintptr_t caves = co_counts_test_caves();
    int32_t to;
    if (caves == 0 || image[site] != 0xE9) return NULL;
    for (uint32_t i = 5; i < size; i++)
        if (image[site + i] != 0x90) return NULL;
    memcpy(&to, image + site + 1, 4);
    const uint8_t* cave = image + site + 5 + to;
    return (uintptr_t)cave >= caves && (uintptr_t)cave < caves + co_counts_test_room() ? cave : NULL;
}

/* Every figure field as the game has it (moved 0) or leading to the block (moved 1). */
static int FieldsHold(int64_t delta, int moved)
{
    for (unsigned i = 0; i < co_counts_test_figure_fields(); i++)
    {
        uint32_t at, old, value;
        co_counts_test_figure_field(i, &at, &old);
        memcpy(&value, g_fake.image + at, 4);
        if (value != (moved ? (uint32_t)(int32_t)((int64_t)(int32_t)old + delta) : old)) return 0;
    }
    return 1;
}

static int Bytes(uint32_t rva, const char* hex)
{
    for (size_t i = 0; hex[2 * i] != 0; i++)
    {
        unsigned b;
        if (sscanf(hex + 2 * i, "%2x", &b) != 1 || g_fake.image[rva + i] != b) return 0;
    }
    return 1;
}

static uint32_t F32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* A tile of the 4 x 2 atlas for a card position: viewport x, y, w, h and the image's UV corners. */
static void Atlas(uint32_t position, float out[8])
{
    const float x = 0.25f * (float)(position % 4u), y = 0.5f * (float)(position / 4u);
    const float row[8] = {x, y, 0.25f, 0.5f, x, 1.0f - y - 0.5f, x + 0.25f, 1.0f - y};
    memcpy(out, row, sizeof row);
}

int main(void)
{
    const int players = co_counts_test_players();

    Start("the reviewed game");
    Init();
    CHECK(co_counts_test_on() == 1 && co_counts_test_figure_fields() == 228u);
    if (players != 8)
    {
        /* A build for four: no figure record moves, no field or cave changes. */
        CHECK(!co_counts_test_active(G_FIGURES) && !co_counts_test_active(G_FIGURES4) && co_counts_test_figure_records() == 0);
        CHECK(FieldsHold(0, 0) && !fake_host_logged("3D figure") && !fake_host_logged("four figures"));
        for (unsigned i = 0; i < co_counts_test_figure_fields(); i++)
        {
            uint32_t at, old;
            CHECK(co_counts_test_write_of(co_counts_test_figure_field(i, &at, &old)) == -1);
        }
        CHECK(Bytes(0x13995D4u, "4183ff08") && Bytes(0x18A1B94u, "488d0de5589400") && Bytes(0x13951A9u, "83fd047326"));
        printf("%s\n", g_failed ? "figures: failures" : "figures: all cases passed");
        return g_failed != 0;
    }

    /* The stand-in image is data; the game's code the test runs must execute. */
    for (size_t i = 0; i < sizeof kFigureCode / sizeof kFigureCode[0]; i++)
    {
        DWORD before;
        CHECK(VirtualProtect(g_fake.image + kFigureCode[i].rva, kFigureCode[i].size, PAGE_EXECUTE_READWRITE, &before));
    }
    uint8_t* const records = (uint8_t*)co_counts_test_figure_records();
    const uintptr_t old = (uintptr_t)(Ups() + RECORDS);
    const int64_t delta = (int64_t)(uintptr_t)records - (int64_t)old;
    CHECK(records != NULL && co_counts_test_active(G_FIGURES) && !co_counts_test_active(G_FIGURES4));
    CHECK(fake_host_logged("players: every loadout card has a 3D figure; the figure records moved to room for twelve (") &&
          !fake_host_logged("the loadout screen shows four figures"));
    if (records == NULL)
    {
        printf("figures: failures\n");
        return 1;
    }
    {
        g_case = "the move";
        int same = 1, empty = 1;
        for (uint32_t i = 0; i < OLD_SIZE; i++) same &= records[i] == (uint8_t)(i * 7u + 1u);
        for (uint32_t i = OLD_SIZE; i < NEW_SIZE; i++) empty &= records[i] == 0;
        CHECK(same && empty && ((uintptr_t)records & 15u) == (old & 15u));
        CHECK(delta >= INT32_MIN && delta <= INT32_MAX && FieldsHold(delta, 1));
        /* The in-place rows: loops over twelve records, the busy checks over every slot, the image at half width. */
        CHECK(Bytes(0x13995D4u, "4183ff0c") && Bytes(0x1398198u, "83fb0c") && Bytes(0x1397E51u, "ba0c000000"));
        CHECK(Bytes(0x130417Cu, "83fe0c0f1f00") && Bytes(0x130639Eu, "83fe0c0f1f00") && Bytes(0x13F87CFu, "83fe0c0f1f00"));
        uint32_t width;
        memcpy(&width, g_fake.image + 0x18A0EF5u, 4);
        CHECK(Bytes(0x18A0EF1u, "c7442450") && width == F32(390.0f));
    }

    /* The game's world destroys, through their rebased fields and the caves for 8 to 11: every record of the world is
     * cleared, the others and the old place stay. */
    {
        g_case = "the world destroys";
        static const uint32_t kOfWorld[] = {1, 9, 11};
        const uint64_t world = 0x5555, other = 0x77;
        static uint8_t object[0x4000];
        const DestroyFn destroy = (DestroyFn)Thunk(kDestroy, sizeof kDestroy);
        const WorldFn byWorld = (WorldFn)Thunk(kWorld, sizeof kWorld);
        uint8_t before[OLD_SIZE];
        memcpy(before, (const void*)old, OLD_SIZE);
        memcpy(object + WORLD, &world, 8);
        CHECK(Cave(0x138FFE9u, 6) != NULL && Cave(0x1394A2Du, 7) != NULL);
        Landing(0x138FFEFu, kRet, sizeof kRet);
        Landing(0x1394A34u, kRet, sizeof kRet);
        for (int pass = 0; pass < 2 && destroy != NULL && byWorld != NULL; pass++)
        {
            for (uint32_t k = 0; k < 12; k++) memcpy(records + k * STRIDE, &other, 8);
            for (size_t j = 0; j < 3; j++) memcpy(records + kOfWorld[j] * STRIDE, &world, 8);
            if (pass == 0) destroy(object, 0x1234, g_fake.image + 0x138FF5Bu);
            else byWorld(world, g_fake.image + 0x13949A0u);
            for (uint32_t k = 0; k < 12; k++)
            {
                uint64_t value;
                const int ofWorld = k == 1 || k == 9 || k == 11;
                memcpy(&value, records + k * STRIDE, 8);
                CHECK(value == (ofWorld ? (pass == 0 ? 0x1234u : 0u) : other));
            }
            CHECK(memcmp(before, (const void*)old, OLD_SIZE) == 0);
        }
    }

    /* The release loop's tail: records 0 to 3, then 8 to 11, with the mask bit and the unit field of each. */
    {
        g_case = "the release loop";
        static const uint32_t kVisits[] = {0, 1, 2, 3, 8, 9, 10, 11};
        const LoopFn loop = (LoopFn)Thunk(kLoop, sizeof kLoop);
        LoopRegs regs = {0, 1, 0x100000, 0};
        size_t n = 0;
        CHECK(Cave(0x139C2D1u, 10) != NULL);
        Landing(0x139C040u, kOne, sizeof kOne);
        Landing(0x139C2DBu, kTwo, sizeof kTwo);
        while (loop != NULL && n < 16)
        {
            CHECK(n < 8 && regs.r15d == kVisits[n] && regs.r12d == 1u << kVisits[n] && regs.r14 == 0x100000u + kVisits[n] * STRIDE);
            n++;
            loop(&regs, g_fake.image + 0x139C2C4u);
            if (regs.eax != 1) break;
        }
        CHECK(n == 8 && regs.eax == 2 && regs.r15d == 12);
    }

    /* 0x139C330 reads a record's equipment id through a rebased field with UPS as its base: records 8 to 11 too. */
    {
        g_case = "a record's equipment";
        const ReadFn read = (ReadFn)(g_fake.image + 0x139C330u);
        for (uint32_t r = 0; r < 12; r++)
            for (uint32_t e = 0; e < 4; e++)
            {
                const uint32_t id = r << 8 | e;
                memcpy(records + r * STRIDE + 0x8C + 4 * e, &id, 4);
            }
        for (uint32_t r = 0; r < 12; r++)
            for (uint32_t e = 0; e < 4; e++)
            {
                uint32_t out = 0xFFFFFFFFu;
                read(NULL, &out, e, r);
                CHECK(out == (r << 8 | e));
            }
    }

    /* A record's session slot: 0 to 7 as before, 8 to 11 are slots 4 to 7, 12 and up the local player. */
    {
        g_case = "the player lookup";
        const ValueFn value = (ValueFn)Thunk(kEbp, sizeof kEbp);
        const uint8_t* cave = Cave(0x13951A9u, 5);
        CHECK(cave != NULL);
        Landing(0x13951AEu, kEbpBack, sizeof kEbpBack);
        Landing(0x13951D4u, kNone, sizeof kNone);
        for (uint32_t v = 0; cave != NULL && value != NULL && v < 16; v++)
            CHECK(value(v, cave) == (v < 8 ? v : v < 12 ? v - 4 : 0xFFFFFFFEu));
    }

    /* "A player's figure" for the callback: records 0 to 3 and 8 to 11. */
    {
        g_case = "the player figure flag";
        const ValueFn flag = (ValueFn)Thunk(kR13, sizeof kR13);
        const uint8_t* cave = Cave(0x1397901u, 12);
        CHECK(cave != NULL);
        Landing(0x139790Du, kSetb, sizeof kSetb);
        for (uint32_t v = 0; cave != NULL && flag != NULL && v < 16; v++)
            CHECK(flag(v, cave) == (v < 4 || (v >= 8 && v < 12)));
    }

    /* The spawn's pose z and the viewport tile of each record: positions 0 to 7 by record, 12 and up the default. */
    {
        g_case = "the figure's pose and tile";
        const FrameFn frame = (FrameFn)Thunk(kFrame, sizeof kFrame);
        const uint8_t* zCave = Cave(0x18A1B94u, 7);
        const uint8_t* rectCave = Cave(0x18A12D8u, 6);
        static uint8_t f[0x1000], stack[0x200];
        CHECK(zCave != NULL && rectCave != NULL);
        Landing(0x18A1B9Bu, kRcx, sizeof kRcx);
        Landing(0x18A1385u, kRet, sizeof kRet);
        const float* z = zCave != NULL ? ((TableFn)(uintptr_t)zCave)() : NULL;
        for (uint32_t s = 0; s < 13 && frame != NULL && rectCave != NULL; s++)
        {
            const uint32_t position = s >= 8 ? s - 4 : s;
            uint8_t* const rbp = stack + 0x100;
            float want[8], rect[4], uv[4];
            if (s < 12 && z != NULL) CHECK(z[s] == 10.0f * (float)(position + 1u));
            memset(stack, 0xCC, sizeof stack);
            memcpy(f + 0x8E4, &s, 4);
            frame(f, rbp, rectCave);
            memcpy(rect, rbp - 0x19, 16);
            memcpy(uv, rbp + 0x77, 8);
            memcpy(uv + 2, rbp + 0x67, 8);
            if (s < 12)
            {
                Atlas(position, want);
                CHECK(memcmp(rect, want, 16) == 0 && memcmp(uv, want + 4, 16) == 0);
            }
            else CHECK(uv[0] == 0.0f && uv[1] == 0.0f && uv[2] == 1.0f && uv[3] == 1.0f);
        }
    }

    /* The way back: records 0 to 7 to the game's place with what they hold now, every field and row as the game's. */
    {
        g_case = "undo";
        const uint8_t mark = 0xA7;
        memset(records + 2 * STRIDE, mark, STRIDE);
        co_counts_undo();
        CHECK(co_counts_test_figure_records() == 0 && FieldsHold(0, 0));
        CHECK(((const uint8_t*)old)[2 * STRIDE] == mark && ((const uint8_t*)old)[3 * STRIDE - 1] == mark);
        CHECK(Bytes(0x13995D4u, "4183ff08") && Bytes(0x130417Cu, "3bb008c50000") && Bytes(0x13951A9u, "83fd047326"));
        CHECK(Bytes(0x18A1B94u, "488d0de5589400") && Bytes(0x139C2D1u, "4183ff040f8265fdffff"));
    }

    /* A refusal leaves the figures out, says why once, and the rows they replace go in. */
    {
        static const struct { const char* name; uint32_t at; uint8_t value; int pointer; const char* line; } kRefusals[] = {
            {"a changed figure field", 0x1391093u + 5u, 0x11, 0, "the loadout screen shows four figures"},
            {"a changed figure cave site", 0x18A12D8u, 0x90, 0, "the loadout screen shows four figures"},
            {"a figure record in use", FAKE_UPS + MASK, 0x01, 0, "players: a figure record is in use; the loadout screen shows four figures"},
            {"the records elsewhere", 0, 0, 1, "players: the figure records are not where the game keeps them; the loadout screen shows four figures"},
        };
        for (size_t i = 0; i < sizeof kRefusals / sizeof kRefusals[0]; i++)
        {
            Start(kRefusals[i].name);
            if (kRefusals[i].pointer)
            {
                const uint64_t elsewhere = (uint64_t)(uintptr_t)(Ups() + 0x100);
                memcpy(g_fake.image + FAKE_UPS_POINTER, &elsewhere, 8);
            }
            else g_fake.image[kRefusals[i].at] = kRefusals[i].value;
            Init();
            CHECK(co_counts_test_on() == 1 && co_counts_test_refused() == 1u << G_FIGURES && fake_host_logged(kRefusals[i].line));
            CHECK(co_counts_test_figure_records() == 0 && co_counts_test_active(G_FIGURES4));
            CHECK(Bytes(0x13995D4u, "4183ff08") && Bytes(0x18A1B94u, "488d0de5589400"));
            /* The replaced rows: the figure slot's cave of the build for eight and the per-peer table's bound. */
            CHECK(Cave(0x18A1A60u, 6) != NULL && Bytes(0x13951A9u, "83fd087326"));
        }
    }

    printf("%s\n", g_failed ? "figures: failures" : "figures: all cases passed");
    return g_failed != 0;
}

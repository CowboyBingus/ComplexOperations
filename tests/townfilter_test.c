/* Level 11's town filters: a complex level widens the rules' and stamps' top level from 10 to 11 in the rule data,
 * and the first op of any later level puts each value back while the data is still the one it widened. Changed or
 * replaced data is left alone; a full list widens nothing more. Run on fake rule data. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGenLevel(float current);
void CoXlTestTownsWiden(uint64_t record);
void CoXlTestTownsRestore(void);
uint32_t CoXlTestTownsPending(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* rule +0x58: bits 1-4 the lowest level, 5-8 the top (15: none); stamp +0x60: bits 2-5 and 6-9 */
#define RULE_TOP10 0x144u
#define RULE_TOP11 0x164u
#define RULE_TOP9 0x124u
#define RULE_OPEN 0x1E4u
#define ITEM_TOP10 0x284u
#define ITEM_TOP11 0x2C4u
#define ITEM_OPEN 0x3C4u

static uint8_t g_record[0x200];
static uint64_t g_table[4];
static uint8_t* g_type;  /* +0x10 the rules, +0x18 their count */
static uint8_t* g_rules; /* 0x70 each: +0 the stamps, +8 their count, +0x58 the levels */
static uint8_t* g_items; /* 0x68 each: +0x60 the levels */

static void* Alloc(size_t n) { return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Put16(uint8_t* at, uint16_t v) { memcpy(at, &v, 2); }
static uint16_t Get16(const uint8_t* at) { uint16_t v; memcpy(&v, at, 2); return v; }
static uint16_t Rule(uint32_t r) { return Get16(g_rules + 0x70 * r + 0x58); }
static uint16_t Item(uint32_t i) { return Get16(g_items + 0x68 * i + 0x60); }
static int Count(const char* text) { int n = 0; for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p++) n++; return n; }

/* One type: rules 0 (top 10, stamps 0 and 1) and 1 (top 9, no stamps), rule 2 open; stamp 0 top 10, stamp 1 open. */
static void Data(void)
{
    memset(g_rules, 0, 0x70 * 3);
    Put64(g_type + 0x10, (uint64_t)(uintptr_t)g_rules); Put32(g_type + 0x18, 3);
    Put64(g_rules, (uint64_t)(uintptr_t)g_items); Put32(g_rules + 8, 2);
    Put16(g_rules + 0x58, RULE_TOP10); Put16(g_rules + 0x70 + 0x58, RULE_TOP9); Put16(g_rules + 0xE0 + 0x58, RULE_OPEN);
    Put16(g_items + 0x60, ITEM_TOP10); Put16(g_items + 0x68 + 0x60, ITEM_OPEN);
}

/* A level: its first op (the put-back), then, on a complex level, its op 0x2B (the widening) and a later op. */
static void Level(int complex)
{
    g_fake.log[0] = 0;
    CoXlTestGenLevel(complex ? 8.0f : 1.0f);
    CoXlTestTownsRestore();
    if (complex) { CoXlTestTownsWiden((uint64_t)(uintptr_t)g_record); CoXlTestTownsRestore(); }
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_type = Alloc(0x1000); g_rules = Alloc(0x1000); g_items = Alloc(8u * 1024u * 0x68u);
    uint8_t* other = Alloc(0x1000);
    if (g_type == NULL || g_rules == NULL || g_items == NULL || other == NULL) { printf("townfilter: no memory\n"); return 1; }
    g_table[0] = (uint64_t)(uintptr_t)g_type;
    Put64(g_record + 0x50, (uint64_t)(uintptr_t)g_table); Put32(g_record + 0x160, 1);

    g_case = "a complex level";
    Data();
    Level(1);
    CHECK(Rule(0) == RULE_TOP11 && Rule(1) == RULE_TOP9 && Rule(2) == RULE_OPEN && Item(0) == ITEM_TOP11 && Item(1) == ITEM_OPEN);
    CHECK(CoXlTestTownsPending() == 2); /* widened for the whole level: its later ops see 11 */
    g_case = "the next level, a normal one";
    Level(0);
    CHECK(Rule(0) == RULE_TOP10 && Rule(1) == RULE_TOP9 && Item(0) == ITEM_TOP10 && Item(1) == ITEM_OPEN && CoXlTestTownsPending() == 0);
    CHECK(fake_host_logged("top levels put back on 2 rules and stamps (0 changed"));
    g_case = "a normal level writes nothing";
    Level(0);
    CHECK(Rule(0) == RULE_TOP10 && Item(0) == ITEM_TOP10 && CoXlTestTownsPending() == 0);

    g_case = "complex after complex";
    Level(1); Level(1);
    CHECK(Rule(0) == RULE_TOP11 && Item(0) == ITEM_TOP11 && CoXlTestTownsPending() == 2); /* put back, then widened again */
    Level(0);
    CHECK(Rule(0) == RULE_TOP10 && Item(0) == ITEM_TOP10 && CoXlTestTownsPending() == 0);

    g_case = "the data was reloaded with other values";
    Level(1);
    Put16(g_rules + 0x58, 0x150); Put16(g_items + 0x60, ITEM_TOP10);
    Level(0);
    CHECK(Rule(0) == 0x150 && Item(0) == ITEM_TOP10 && CoXlTestTownsPending() == 0); /* left alone, dropped */

    g_case = "the type holds other rules now";
    Data();
    Level(1);
    Put64(g_type + 0x10, (uint64_t)(uintptr_t)other);
    Level(0);
    CHECK(Rule(0) == RULE_TOP11 && Item(0) == ITEM_TOP11 && CoXlTestTownsPending() == 0); /* not this data's any more: nothing written */
    g_case = "the rule holds other stamps now";
    Data();
    Level(1);
    Put64(g_rules, (uint64_t)(uintptr_t)other);
    Level(0);
    CHECK(Rule(0) == RULE_TOP11 && Item(0) == ITEM_TOP11 && CoXlTestTownsPending() == 0);

    g_case = "the data is not writable at the next level";
    Data();
    Level(1);
    DWORD old;
    VirtualProtect(g_rules, 0x1000, PAGE_READONLY, &old);
    Level(0);
    CHECK(Rule(0) == RULE_TOP11 && Item(0) == ITEM_TOP10 && CoXlTestTownsPending() == 1); /* the rule's entry stays */
    VirtualProtect(g_rules, 0x1000, PAGE_READWRITE, &old);
    Level(0);
    CHECK(Rule(0) == RULE_TOP10 && CoXlTestTownsPending() == 0);

    g_case = "the list is full";
    {
        Put32(g_type + 0x18, 8);
        for (uint32_t r = 0; r < 8; r++)
        {
            uint8_t* rule = g_rules + 0x70 * r;
            Put64(rule, (uint64_t)(uintptr_t)(g_items + 1024u * 0x68u * r)); Put32(rule + 8, 1024); Put16(rule + 0x58, RULE_TOP10);
        }
        for (uint32_t i = 0; i < 8u * 1024u; i++) Put16(g_items + 0x68 * i + 0x60, ITEM_TOP10);
        Level(1); /* 8 rules and 8192 stamps: 8192 places */
        CHECK(CoXlTestTownsPending() == 8192 && Rule(7) == RULE_TOP11 && Item(8183) == ITEM_TOP11 && Item(8184) == ITEM_TOP10 && Item(8191) == ITEM_TOP10);
        CHECK(Count("list of top levels to put back is full") == 1 && fake_host_logged("8 rules and stamps keep level 10"));
        Level(0);
        int back = CoXlTestTownsPending() == 0;
        for (uint32_t r = 0; r < 8; r++) back &= Rule(r) == RULE_TOP10;
        for (uint32_t i = 0; i < 8u * 1024u; i++) back &= Item(i) == ITEM_TOP10;
        CHECK(back);
    }

    if (g_failed) { printf("townfilter: %d checks failed\n", g_failed); return 1; }
    printf("townfilter: all cases passed\n");
    return 0;
}

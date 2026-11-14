/* The living world's fight rule (a cluster goes to the nearest fight, a split-off fight sends nothing and hands its
 * squads over, a failed reinforcement is replaced), the alert-level line, the placement gate on an extra large level's
 * navmesh (faces metres off the drawn ground), the keeper's put-down, and the network vector3 ranges. Fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestNetVec(uintptr_t cfg, float scale, uint32_t* types, uint8_t* widen, uint32_t* fields, int cap, int* pos, int* jobs);
float CoXlTestLwFaceReach(int tilesOn, int keeper, float top, float drawn, float wall);
float CoXlTestLwRequestZ(float top, float face);
int CoXlTestLwSlotHeightOk(float z, float top, float reach);
int CoXlTestMoveKeepWhich(float z, float ground, int listing);
int CoXlTestMoveSettle(uint32_t e, uint64_t until, uint32_t ask, uint64_t now);
void CoXlTestLwFight(int f, float x, float y, uint32_t age, uint32_t seenAgo, int engaged, uint64_t now);
void CoXlTestLwFightsClear(void);
int CoXlTestLwFightNearest(float x, float y, float within, int skip);
void CoXlTestLwFightSquad(int i, int f, int reinforcing);
int CoXlTestLwFightSends(int f, uint64_t now, uint64_t sentAgo);
int CoXlTestLwFightHandOver(int f);
int CoXlTestLwFightOfSquad(int i);
int CoXlTestLwAlertList(uint32_t id, uint32_t lv, float d, int note);
int CoXlTestLwAlertFights(uint32_t lv);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static int Count(const char* text)
{
    int n = 0;
    for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p += strlen(text)) n++;
    return n;
}

/* A network config in the fake image, laid out as the engine's (exe 0x34C27E..0x34C2BC): descriptors of 24 bytes
 * (+0 name, +0xC kind, +0xD bits, +0x10 min, +0x14 max) at [cfg+0x18] x [cfg+0x10]; object types of 0x50 bytes at
 * [cfg+0x78] x [cfg+0x70], each +0x18 field count, +0x20 type indices, +0x38 field hashes. */
enum { T_POS = 0, T_JOB, T_VEL, T_FAR, T_WAVE, T_INT, T_CLOUD, T_ODD, T_ASYM, T_COUNT };
static uint8_t* g_cfg;
static float* Range(int type) { return (float*)(g_cfg + 0x1000 + 24 * type + 0x10); }
static void Desc(int type, uint32_t name, uint8_t kind, uint8_t bits, float lo, float hi)
{
    uint8_t* d = g_cfg + 0x1000 + 24 * type;
    memcpy(d, &name, 4); d[0xC] = kind; d[0xD] = bits; memcpy(d + 0x10, &lo, 4); memcpy(d + 0x14, &hi, 4);
}
static void Object(int index, int fields, const uint32_t* hashes, const uint32_t* types)
{
    uint8_t* o = g_cfg + 0x2000 + 0x50 * index; uint8_t* data = g_cfg + 0x3000 + 0x100 * index;
    uint32_t n = (uint32_t)fields; uintptr_t h = (uintptr_t)data, t = (uintptr_t)(data + 0x80);
    memcpy(data, hashes, 4u * n); memcpy(data + 0x80, types, 4u * n);
    memcpy(o + 0x18, &n, 4); memcpy(o + 0x20, &t, 8); memcpy(o + 0x38, &h, 8);
}
static void Config(void)
{
    g_cfg = g_fake.image + 0x400000;
    memset(g_cfg, 0, 0x4000);
    uint32_t types = T_COUNT, objects = 3; uintptr_t descs = (uintptr_t)(g_cfg + 0x1000), objs = (uintptr_t)(g_cfg + 0x2000);
    memcpy(g_cfg + 0x10, &types, 4); memcpy(g_cfg + 0x18, &descs, 8); memcpy(g_cfg + 0x70, &objects, 4); memcpy(g_cfg + 0x78, &objs, 8);
    Desc(T_POS, 0x8BBEB160u, 3, 17, -2000.0f, 2000.0f);
    Desc(T_JOB, 0x0086F75Au, 3, 12, -2000.0f, 2000.0f);
    Desc(T_VEL, 0x942E38EDu, 3, 14, -2000.0f, 2000.0f);
    Desc(T_FAR, 0xAD43C6DCu, 3, 24, -10000.0f, 10000.0f);
    Desc(T_WAVE, 0x0B18BE60u, 3, 14, -2000.0f, 2000.0f);
    Desc(T_INT, 0x11111111u, 1, 12, 0.0f, 0.0f);
    Desc(T_CLOUD, 0xE5B502BFu, 3, 20, 0.0f, 1.0f);
    Desc(T_ODD, 0x22222222u, 3, 20, -100.0f, 100.0f);
    Desc(T_ASYM, 0x44444444u, 3, 17, -2000.0f, 1000.0f);
    /* a unit: its job point first met through target_position, then job_position; a velocity; an int */
    const uint32_t h0[] = {0x7A4BDBBEu, 0xA4C332A8u, 0x8BBEB160u, 0xA5D1F45Bu, 0x33333333u}, t0[] = {T_JOB, T_JOB, T_POS, T_VEL, T_INT};
    /* a ragdoll: the far type first met through another field, then a position field; a cloud offset; a local offset */
    const uint32_t h1[] = {0xA5F83FA0u, 0x8BBEB160u, 0xB3048954u, 0x3C01F356u}, t1[] = {T_FAR, T_FAR, T_CLOUD, T_ODD};
    /* a wave: a +-2000 vector3 no known position field uses; a type index past the table */
    /* and a position field whose type's range is not symmetric: left, with a line */
    const uint32_t h2[] = {0xFCB39823u, 0x8BBEB160u, 0x8BBEB160u, 0x8BBEB160u}, t2[] = {T_WAVE, 999, T_POS, T_ASYM};
    Object(0, 5, h0, t0); Object(1, 4, h1, t1); Object(2, 4, h2, t2);
}
static void NetRanges(void)
{
    uint32_t types[16], fields[16]; uint8_t widen[16]; int pos = 0, jobs = 0;
    Config();
    g_case = "the scan lists the +-2000 vector3 types and the position fields' types, with their users";
    CoXlTestNetVec(0, -1.0f, types, widen, fields, 16, &pos, &jobs);
    int r = CoXlTestNetVec((uintptr_t)g_cfg, 1.0f, types, widen, fields, 16, &pos, &jobs);
    int n = r & 0xFF;
    CHECK(n == 5); /* job, pos, vel, far, wave: not the int, the cloud (0..1) or the +-100 offset */
    CHECK(pos == 4 && jobs == 1);
    int seen = 0;
    for (int k = 0; k < n && k < 16; k++)
    {
        if (types[k] == T_JOB) { seen |= 1; CHECK(widen[k] == 1 && fields[k] == 2); } /* both users counted, the first met was not a position */
        if (types[k] == T_POS) { seen |= 2; CHECK(widen[k] == 1 && fields[k] == 2); }
        if (types[k] == T_VEL) { seen |= 4; CHECK(widen[k] == 0 && fields[k] == 1); }
        if (types[k] == T_FAR) { seen |= 8; CHECK(widen[k] == 1 && fields[k] == 2); }
        if (types[k] == T_WAVE) { seen |= 16; CHECK(widen[k] == 0); }
    }
    CHECK(seen == 31);
    CHECK(Count("network position type 8 is not a symmetric vector3 (-2000.0 .. 1000.0, 17 bits); left as it is") == 1);
    g_case = "a vanilla level leaves vanilla's ranges";
    CHECK(Range(T_JOB)[0] == -2000.0f && Range(T_JOB)[1] == 2000.0f && Range(T_POS)[1] == 2000.0f);
    g_case = "an extra large level widens the position types only, to +-1024 (scale + 1)";
    r = CoXlTestNetVec((uintptr_t)g_cfg, 8.0f, types, widen, fields, 16, &pos, &jobs);
    CHECK((r >> 8) == 3);
    CHECK(Range(T_JOB)[0] == -9216.0f && Range(T_JOB)[1] == 9216.0f);
    CHECK(Range(T_POS)[0] == -9216.0f && Range(T_POS)[1] == 9216.0f);
    CHECK(Range(T_FAR)[0] == -10000.0f && Range(T_FAR)[1] == 10000.0f); /* already wider: kept */
    CHECK(Range(T_VEL)[0] == -2000.0f && Range(T_VEL)[1] == 2000.0f);   /* a velocity is not widened */
    CHECK(Range(T_WAVE)[1] == 2000.0f && Range(T_ODD)[1] == 100.0f && Range(T_CLOUD)[1] == 1.0f);
    CHECK(Range(T_ASYM)[0] == -2000.0f && Range(T_ASYM)[1] == 1000.0f);
    CHECK(g_cfg[0x1000 + 24 * T_JOB + 0xD] == 12 && g_cfg[0x1000 + 24 * T_POS + 0xD] == 17); /* the bits stay: the wire layout is the same */
    g_case = "the next vanilla level gets vanilla's ranges back";
    r = CoXlTestNetVec((uintptr_t)g_cfg, 1.0f, types, widen, fields, 16, &pos, &jobs);
    CHECK((r & 0xFF) == 5 && (r >> 8) == 3); /* the scan still finds the widened types */
    CHECK(Range(T_JOB)[0] == -2000.0f && Range(T_JOB)[1] == 2000.0f && Range(T_POS)[0] == -2000.0f && Range(T_POS)[1] == 2000.0f);
    g_case = "a range that is neither vanilla's nor this mod's is left";
    Range(T_JOB)[0] = -3000.0f; Range(T_JOB)[1] = 3000.0f;
    r = CoXlTestNetVec((uintptr_t)g_cfg, 8.0f, types, widen, fields, 16, &pos, &jobs);
    CHECK((r >> 8) == 2 && Range(T_JOB)[0] == -3000.0f && Range(T_JOB)[1] == 3000.0f && Range(T_POS)[1] == 9216.0f);
    g_case = "an unreadable config";
    uint32_t zero = 0; memcpy(g_cfg + 0x70, &zero, 4);
    CoXlTestNetVec(0, -1.0f, types, widen, fields, 16, &pos, &jobs);
    CHECK(CoXlTestNetVec((uintptr_t)g_cfg, 8.0f, types, widen, fields, 16, &pos, &jobs) == -1);
}

static void Gate(void)
{
    g_case = "the face's reach: 2 m, or the walls' reach where the hit is the drawn ground on a moving-tiles level";
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 50.3f, 33.2f) == 33.2f);
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 49.5f, 33.2f) == 33.2f);  /* 0.5 m: still the drawn ground */
    CHECK(CoXlTestLwFaceReach(1, 1, 51.1f, 50.0f, 33.2f) == 2.0f);   /* run 2: hits 1.1-1.7 m over the drawn ground stay refused */
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 50.6f, 33.2f) == 2.0f);
    CHECK(CoXlTestLwFaceReach(0, 1, 50.0f, 50.0f, 33.2f) == 2.0f);   /* a vanilla level */
    CHECK(CoXlTestLwFaceReach(1, 0, 50.0f, 50.0f, 33.2f) == 2.0f);   /* no keeper: members would stay where they start */
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, -9999.0f, 33.2f) == 2.0f);
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, NAN, 33.2f) == 2.0f);
    CHECK(CoXlTestLwFaceReach(1, 1, NAN, 50.0f, 33.2f) == 2.0f);
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 50.0f, 0.0f) == 2.0f);    /* the stub is not in */
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 50.0f, -0.0f) == 2.0f);
    CHECK(CoXlTestLwFaceReach(1, 1, 50.0f, 50.0f, 1.5f) == 2.0f);    /* never under 2 m */
    g_case = "the request's height: the ground hit near the face, the face when it is more than 2 m off";
    CHECK(CoXlTestLwRequestZ(50.0f, 51.9f) == 50.0f);
    CHECK(CoXlTestLwRequestZ(50.0f, 48.0f) == 50.0f);
    CHECK(CoXlTestLwRequestZ(50.0f, 56.5f) == 56.5f); /* the member cast finds its start face within 5 m only */
    CHECK(CoXlTestLwRequestZ(50.0f, 44.0f) == 44.0f);
    g_case = "a member's start height: 0.9 m under to 2 m over near; the reach either way when wide";
    CHECK(CoXlTestLwSlotHeightOk(49.2f, 50.0f, 2.0f) && !CoXlTestLwSlotHeightOk(49.0f, 50.0f, 2.0f));
    CHECK(CoXlTestLwSlotHeightOk(51.9f, 50.0f, 2.0f) && !CoXlTestLwSlotHeightOk(52.1f, 50.0f, 2.0f));
    CHECK(CoXlTestLwSlotHeightOk(56.5f, 50.0f, 12.0f) && CoXlTestLwSlotHeightOk(43.5f, 50.0f, 12.0f));
    CHECK(!CoXlTestLwSlotHeightOk(62.5f, 50.0f, 12.0f) && !CoXlTestLwSlotHeightOk(37.5f, 50.0f, 12.0f));
    g_case = "the keeper: under the ground always; over it only while units are listed";
    CHECK(CoXlTestMoveKeepWhich(49.6f, 50.0f, 0) == 1 && CoXlTestMoveKeepWhich(49.6f, 50.0f, 1) == 1);
    CHECK(CoXlTestMoveKeepWhich(49.8f, 50.0f, 1) == 0 && CoXlTestMoveKeepWhich(50.4f, 50.0f, 1) == 0);
    CHECK(CoXlTestMoveKeepWhich(50.6f, 50.0f, 1) == 2 && CoXlTestMoveKeepWhich(56.0f, 50.0f, 0) == 0);
    CHECK(CoXlTestMoveKeepWhich(56.0f, -9999.0f, 1) == 0);
    g_case = "the put-down list: a unit is listed until its time, others never";
    const uint64_t now = GetTickCount64();
    CHECK(CoXlTestMoveSettle(0x501, now + 6000, 0, now) == 0);
    CHECK(CoXlTestMoveSettle(0x501, now + 6000, 0x501, now) == 1);
    CHECK(CoXlTestMoveSettle(0x502, now + 6000, 0, now) == 0);
    CHECK(CoXlTestMoveSettle(0x501, now + 6000, 0, now + 6000) == 0);
    CHECK(CoXlTestMoveSettle(0x501, now + 9000, 0x501, now + 7000) == 1); /* asked again: one entry, the later time */
    for (uint32_t e = 1; e <= 80; e++) CoXlTestMoveSettle(e, now + 9000, e, now); /* more than the list holds */
    CHECK(CoXlTestMoveSettle(0x501, 0, 0, now + 8000) == 1 && CoXlTestMoveSettle(1, 0, 0, now) == 1 && CoXlTestMoveSettle(80, 0, 0, now) == 0);
}

static void Fights(void)
{
    const uint64_t now = GetTickCount64() + 1000000;
    g_case = "a cluster goes to the nearest live fight within 60 m, not the first by slot";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 60000, 0, 6, now);
    CoXlTestLwFight(1, 80, 0, 20000, 0, 4, now);
    CHECK(CoXlTestLwFightNearest(45, 0, 60.0f, -1) == 1); /* 45 m from fight 0, 35 m from fight 1 */
    CHECK(CoXlTestLwFightNearest(20, 0, 60.0f, -1) == 0);
    CHECK(CoXlTestLwFightNearest(145, 0, 60.0f, -1) == -1);
    CHECK(CoXlTestLwFightNearest(45, 0, 60.0f, 1) == 0);
    CoXlTestLwFight(2, 46, 0, 40000, 0, 4, now); g_fake.log[0] = 0;
    CHECK(CoXlTestLwFightNearest(45, 0, 60.0f, -1) == 2);
    CoXlTestLwFightsClear();
    CoXlTestLwFight(3, 10, 0, 1000, 0, 4, now);
    CHECK(CoXlTestLwFightNearest(0, 0, 60.0f, -1) == 3 && CoXlTestLwFightNearest(0, 0, 60.0f, 3) == -1);

    g_case = "a fight sends up to its want, less the squads it has";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 60000, 0, 20, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 3);
    CoXlTestLwFightSquad(0, 0, 1); CoXlTestLwFightSquad(1, 0, 1);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 1);
    CoXlTestLwFightSquad(2, 0, 1);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 0);
    g_case = "a reinforcement that ended is replaced, 10 s after the last send at the earliest";
    CoXlTestLwFightSquad(1, 0, 0); /* it waited too long: a patrol again */
    CHECK(CoXlTestLwFightSends(0, now, 0) == 1);
    CHECK(CoXlTestLwFightSends(0, now, 9999) == 0);
    CHECK(CoXlTestLwFightSends(0, now, 10000) == 1);
    CoXlTestLwFightSquad(0, 0, 0); CoXlTestLwFightSquad(2, 0, 0);
    CHECK(CoXlTestLwFightSends(0, now, 60000) == 3);
    CoXlTestLwFightSquad(1, 0, 2); /* its intent is over though it still names the fight: not counted */
    CHECK(CoXlTestLwFightSends(0, now, 60000) == 3);
    g_case = "a fight never wants more than three squads";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 60000, 0, 76, now); /* run 2's fight: at most 76 engaged */
    CHECK(CoXlTestLwFightSends(0, now, 0) == 3);
    g_case = "a small fight wants one squad, and none before it has lasted 5 s";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 4000, 0, 2, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 0);
    CoXlTestLwFight(0, 0, 0, 5000, 0, 2, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 1);
    CoXlTestLwFight(0, 0, 0, 0, 0, 8, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 2); /* three engaged or more: at once */
    CoXlTestLwFight(0, 0, 0, 60000, 1000, 8, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 0); /* not seen in this scan */
    g_case = "another slot's squads, or an older fight's in this slot, do not count";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(1, 500, 0, 60000, 0, 20, now);
    CoXlTestLwFightSquad(0, 1, 1);
    CoXlTestLwFight(0, 0, 0, 60000, 0, 20, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 3 && CoXlTestLwFightSends(1, now, 0) == 2);
    CoXlTestLwFight(1, 500, 0, 30000, 0, 20, now); /* the slot's next fight */
    CHECK(CoXlTestLwFightSends(1, now, 0) == 3 && CoXlTestLwFightOfSquad(0) == -1);

    g_case = "a new fight within 100 m of an older live one sends nothing for 3 s";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 60000, 0, 6, now);
    CoXlTestLwFight(1, 70, 0, 0, 0, 5, now); /* a cluster just over 60 m away */
    CHECK(CoXlTestLwFightSends(1, now, 0) == 0);
    CoXlTestLwFight(1, 70, 0, 2999, 0, 5, now);
    CHECK(CoXlTestLwFightSends(1, now, 0) == 0);
    CoXlTestLwFight(1, 70, 0, 3000, 0, 5, now);
    CHECK(CoXlTestLwFightSends(1, now, 0) == 1);
    CoXlTestLwFight(1, 101, 0, 0, 0, 5, now); /* its own fight */
    CHECK(CoXlTestLwFightSends(1, now, 0) == 1);
    CoXlTestLwFight(1, 70, 0, 0, 0, 5, now);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 1); /* the older one is not held by the newer */

    g_case = "a fight that ends unseen within 100 m of a live fight hands its squads over";
    CoXlTestLwFightsClear();
    CoXlTestLwFight(0, 0, 0, 90000, 0, 6, now);
    CoXlTestLwFight(1, 70, 0, 31000, 31000, 5, now);
    CoXlTestLwFight(2, 800, 0, 31000, 31000, 5, now);
    CoXlTestLwFightSquad(0, 1, 1); CoXlTestLwFightSquad(1, 1, 1); CoXlTestLwFightSquad(2, 2, 1); CoXlTestLwFightSquad(3, 0, 0);
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwFightHandOver(1) == 0);
    CHECK(CoXlTestLwFightOfSquad(0) == 0 && CoXlTestLwFightOfSquad(1) == 0); /* still reinforcing, now fight 0 */
    CHECK(Count("ended unseen 70 m from fight 0: its 2 squads go on to that fight") == 1);
    CHECK(CoXlTestLwFightSends(0, now, 0) == 0); /* the fight has its one squad and one more */
    g_case = "with no fight near, its squads' fight is over";
    CHECK(CoXlTestLwFightHandOver(2) == -1);
    CHECK(CoXlTestLwFightOfSquad(2) == -1 && CoXlTestLwFightOfSquad(3) == -1);
    CHECK(Count("ended unseen") == 1);
}

static void Alerts(void)
{
    g_case = "the alert levels that make a fight";
    for (uint32_t lv = 0; lv < 10; lv++) CHECK(CoXlTestLwAlertFights(lv) == (lv == 3 || lv == 5 || lv == 6 || lv == 7));
    g_case = "the 30 s line lists each alerted unit within 60 m of a player with its level";
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwAlertList(0, 0, 0, 2) == 0);
    CoXlTestLwAlertList(0, 0, 0, 1);
    CHECK(Count("alert levels within") == 0); /* nothing alerted: no line */
    CHECK(CoXlTestLwAlertList(0x1A0, 1, 1.0f, 0) == 1);
    CHECK(CoXlTestLwAlertList(0x1A1, 2, 60.0f, 0) == 2);
    CHECK(CoXlTestLwAlertList(0x1A2, 3, 61.0f, 0) == 2); /* too far */
    CHECK(CoXlTestLwAlertList(0x1A3, 0, 5.0f, 0) == 2);  /* not alerted */
    CHECK(CoXlTestLwAlertList(0x1A4, 6, 12.0f, 1) == 3);
    CHECK(Count("alert levels within 60 m of a player (30 s): 3 alerted units, by level 1-7 and over: 1 1 0 0 0 1 0 0; 1 at a level that makes a fight (3, 5, 6, 7): "
                "000001A0 level 1 at 1 m, 000001A1 level 2 at 60 m, 000001A4 level 6 at 12 m") == 1);
    CHECK(CoXlTestLwAlertList(0, 0, 0, 2) == 0);
    for (uint32_t k = 0; k < 40; k++) CoXlTestLwAlertList(0x200 + k, 3, 10.0f, 0);
    CoXlTestLwAlertList(0, 0, 0, 1);
    CHECK(Count("40 alerted units") == 1 && Count("(and more)") == 1);
}

int main(void)
{
    g_case = "set-up";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    NetRanges();
    Gate();
    Fights();
    Alerts();
    if (g_failed) { printf("lwfight: %d checks failed\n", g_failed); return 1; }
    printf("lwfight: all cases passed\n");
    return 0;
}

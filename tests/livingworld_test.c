/* The living world in a complex operation: on without a switch, and acting only while the mission in play is complex.
 * A normal mission, the ship and a war-table preview see none of it. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_patrols.h"

void CoXlTestLive(int complex);
int CoXlTestLiving(uintptr_t game);
void CoXlTestLwPins(uint8_t* image);
LONG CoXlTestNoiseHead(void);
int CoXlTestLwSettle(uint32_t i, uint64_t peer, const float* at, uint64_t now, float* use);
int CoXlTestLwSightVerdict(const uint8_t* hits, uint32_t n, uint32_t target, const float* eye, const float* tpos, float nearR, float folR, int* at);
int CoXlTestLwStaticKind(float z, float drawn, float coarse, int tile);
int CoXlTestLwSenseRule(uint32_t alert, float body, float reach, uint32_t code, int clear);
float CoXlTestLwSightMod(float range, uint32_t op, float value);
float CoXlTestLwSenseReach(float visibility, float base, uint32_t op, float value);
float CoXlTestLwBodyDist(float d, float radius);
int CoXlTestLwSenseGate(uint8_t entity, uint64_t tags);
int CoXlTestLwPlaceHeld(const float* live, const float* held);
int CoXlTestLwSenseNextPlayer(int last, const uint8_t* ok, int n);
int CoXlTestLwSensePick(const uint64_t* checkedAt, const float* d, const uint8_t* player, const uint8_t* taken, int n, int who);
int CoXlTestLwSenseDet(int reset, int n, char op, uint32_t det, int arg, uint64_t now);
int CoXlTestLwHostileRule(uint8_t entity, uint8_t on, uint8_t always, int avatar, uint32_t lists, uint32_t faction, uint32_t mask);
int CoXlTestLwSenseRoute(float awareness);
int CoXlTestLwSenseLost(uint32_t flags, uint32_t state);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NOISE 0x64A890u
#define DEFER_SITE 0xFDAA8Au
#define SPAWN 0x953580u

typedef void (*NoiseFn)(void*, uint8_t, uint32_t, const float*, uint32_t, float, uint32_t);

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    memcpy(g_fake.image + CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp); /* the director's patrol gate */
    memcpy(g_fake.image + CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate);
    memcpy(g_fake.image + CO_PATROL_NEXT, kCoPatrolNext, sizeof kCoPatrolNext);
    memcpy(g_fake.image + CO_PATROL_SKIP, kCoPatrolSkip, sizeof kCoPatrolSkip);
    g_fake.writes = 0;
    return g_fake.image;
}

/* A ray hit as the game's ray writes it: +0 x, +4 y, +0x18 metres, +0x1C unit (0 static), +0x24 type. */
static void Hit(uint8_t* hits, int i, float x, float y, float d, uint32_t unit, uint32_t type)
{
    uint8_t* h = hits + 0x2C * i;
    memcpy(h, &x, 4); memcpy(h + 4, &y, 4); memcpy(h + 0x18, &d, 4); memcpy(h + 0x1C, &unit, 4); memcpy(h + 0x24, &type, 4);
}

/* The sight ray's hits read in order, as 0x64D1D0: the target, foliage near either end, static and other units. */
static void SightVerdict(void)
{
    enum { TARGET = 0x51, OTHER = 0x77 };
    const uint32_t FOL = 0x4B8DC02Bu, ROCK = 0x1234u;
    const float eye[3] = {0, 0, 2}, tpos[3] = {20, 0, 1};
    uint8_t h[8 * 0x2C]; int at = 7;
    g_case = "a sight ray with no hits sees the target";
    CHECK(CoXlTestLwSightVerdict(h, 0, TARGET, eye, tpos, 5, 2, &at) == 1 && at == -1);
    g_case = "the target first: seen, whatever follows";
    memset(h, 0, sizeof h); Hit(h, 0, 19.6f, 0, 19.6f, TARGET, 0); Hit(h, 1, 10, 0, 10, 0, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 1 && at == 0);
    g_case = "a static hit before the target blocks it";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, ROCK); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "another unit blocks";
    memset(h, 0, sizeof h); Hit(h, 0, 12, 0, 12, OTHER, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage halfway blocks";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, FOL); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage within the foliage radius of the target, or exactly on it, is passed";
    memset(h, 0, sizeof h); Hit(h, 0, 18, 0, 18, 0, FOL); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 1 && at == 1);
    Hit(h, 0, 17.9f, 0, 17.9f, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage nearer than the foliage radius to the eye is passed, not on it";
    memset(h, 0, sizeof h); Hit(h, 0, 1.9f, 0, 1.9f, 0, FOL); Hit(h, 1, 10, 0, 10, 0, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 1);
    Hit(h, 0, 2, 0, 2, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "with the target within the near radius across, all foliage is passed";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 20.5f, 2, &at) == 1 && at == -1);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 20, 2, &at) == 0 && at == 0);
    g_case = "a static hit against the ground";
    CHECK(CoXlTestLwStaticKind(5.2f, 5.0f, 9.0f, 0) == 3);     /* the drawn ground */
    CHECK(CoXlTestLwStaticKind(8.9f, 5.0f, 9.0f, 0) == 4);     /* the coarse collision over a live tile */
    CHECK(CoXlTestLwStaticKind(8.9f, 5.0f, 9.0f, -1) == 5);    /* off a tile: an object */
    CHECK(CoXlTestLwStaticKind(4.2f, 5.0f, 4.0f, 0) == 5);     /* under the drawn ground on the coarse rows: not the coarse body over it */
    CHECK(CoXlTestLwStaticKind(7.0f, 5.0f, 9.0f, 0) == 5);     /* an object */
    CHECK(CoXlTestLwStaticKind(7.0f, -9999.0f, 9.0f, 0) == 6); /* no drawn ground */
    g_case = "a calm enemy close beside a player notices them only with a clear line"; /* 0 yes, 1 alert, 2 far, 3 stealth, 4 in view, 5 blocked */
    CHECK(CoXlTestLwSenseRule(0, 14.9f, 15.0f, 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, 15.0f, 15.0f, 0, 1) == 0);  /* the edge counts */
    CHECK(CoXlTestLwSenseRule(0, 15.1f, 15.0f, 0, 1) == 2);  /* too far */
    CHECK(CoXlTestLwSenseRule(0, 15.1f, 20.0f, 0, 1) == 2);  /* ... whatever the reach */
    CHECK(CoXlTestLwSenseRule(1, 10.0f, 15.0f, 0, 1) == 1);  /* already suspicious: the game's own path */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 15.0f, 2, 1) == 4);  /* in view: the game's sight */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 15.0f, 1, 1) == 4);  /* seen close (within 0.3 x the range) */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 15.0f, 0, 0) == 5);  /* blocked: not through walls or units */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 15.0f, 0, -1) == 5); /* no ray */
    CHECK(CoXlTestLwSenseRule(0, 7.4f, 7.5f, 0, 1) == 0);    /* a hidden player: half the reach */
    CHECK(CoXlTestLwSenseRule(0, 7.5f, 7.5f, 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, 7.6f, 7.5f, 0, 1) == 3);
    CHECK(CoXlTestLwSenseRule(0, 1.0f, 0.0f, 0, 1) == 3);    /* invisible */
    g_case = "the reach follows the player's visibility and sight modifier"; /* 0x64C9DE..0x64CA2B: op 0 replaces, 1 or 3 adds, 2 multiplies */
    CHECK(CoXlTestLwSightMod(40.0f, 0, 28.0f) == 28.0f);
    CHECK(CoXlTestLwSightMod(40.0f, 1, 5.0f) == 45.0f);
    CHECK(CoXlTestLwSightMod(40.0f, 3, 5.0f) == 45.0f);
    CHECK(CoXlTestLwSightMod(40.0f, 2, 0.5f) == 20.0f);
    CHECK(CoXlTestLwSightMod(40.0f, 4, 0.5f) == 40.0f);   /* no such op: unchanged */
    CHECK(CoXlTestLwSightMod(40.0f, 2, 0.0f) == 40.0f);   /* only a value over 0 counts (0x64C9E8) */
    CHECK(CoXlTestLwSightMod(40.0f, 0, 0.0f) == 40.0f);
    CHECK(CoXlTestLwSightMod(40.0f, 1, -5.0f) == 40.0f);
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 0, 0.0f) == 15.0f);  /* no modifier */
    #define REACH_IS(v, base, op, val, want) (CoXlTestLwSenseReach(v, base, op, val) > (want) - 0.01f && CoXlTestLwSenseReach(v, base, op, val) < (want) + 0.01f)
    CHECK(REACH_IS(0.6f, 40.0f, 0, 0.0f, 11.619f));  /* the square root of the visibility: daylight, standing */
    CHECK(REACH_IS(0.24f, 40.0f, 0, 0.0f, 7.348f));  /* dusk, standing still */
    CHECK(REACH_IS(0.12f, 40.0f, 0, 0.0f, 5.196f));  /* dusk, prone */
    CHECK(REACH_IS(0.25f, 40.0f, 0, 0.0f, 7.5f));
    CHECK(CoXlTestLwSenseReach(0.0f, 40.0f, 0, 0.0f) == 0.0f);
    CHECK(CoXlTestLwSenseReach(-0.5f, 40.0f, 0, 0.0f) == 0.0f);  /* no root of a negative */
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 2, 0.5f) == 7.5f);   /* stealth armour halves it */
    CHECK(REACH_IS(0.6f, 40.0f, 2, 0.5f, 5.809f));               /* ... on top of the visibility */
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 0, 20.0f) == 7.5f);  /* replaced: the share of the eye's range */
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 0, 80.0f) == 15.0f); /* never lengthened */
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 1, 10.0f) == 15.0f);
    CHECK(CoXlTestLwSenseReach(1.0f, 40.0f, 2, 3.0f) == 15.0f);
    CHECK(CoXlTestLwSenseReach(1.0f, 0.0f, 2, 0.5f) == 15.0f);   /* an eye with no range ahead: no share to take */
    CHECK(CoXlTestLwSenseReach(1.0f, -40.0f, 2, 0.5f) == 15.0f);
    g_case = "the reach is measured from the unit's body"; /* a player 10 m from a charger's root (1.9 m) at visibility 0.6 */
    CHECK(CoXlTestLwBodyDist(10.0f, 0.0f) == 10.0f);
    CHECK(CoXlTestLwBodyDist(10.0f, 1.9f) == 8.1f);
    CHECK(CoXlTestLwBodyDist(1.0f, 1.9f) == 0.0f);
    CHECK(CoXlTestLwBodyDist(10.0f, -3.0f) == 10.0f);
    CHECK(CoXlTestLwBodyDist(10.0f, 50.0f) == 2.0f);       /* a radius is trusted to 8 m */
    /* reach 11.6 m at 0.6: 12 m from the root is out, from the body (10.1 m) in */
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(12.0f, 0.0f), CoXlTestLwSenseReach(0.6f, 40.0f, 0, 0.0f), 0, 1) == 3);
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(12.0f, 1.9f), CoXlTestLwSenseReach(0.6f, 40.0f, 0, 0.0f), 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(10.0f, 1.9f), CoXlTestLwSenseReach(0.6f, 40.0f, 0, 0.0f), 0, 1) == 0);
    /* 5 m behind a small unit (0.8 m): noticed standing at dusk (7.3 m) and prone (5.2 m); 6.5 m is out when prone */
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(5.0f, 0.8f), CoXlTestLwSenseReach(0.24f, 40.0f, 0, 0.0f), 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(5.0f, 0.8f), CoXlTestLwSenseReach(0.12f, 40.0f, 0, 0.0f), 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(6.5f, 0.8f), CoXlTestLwSenseReach(0.12f, 40.0f, 0, 0.0f), 0, 1) == 3);
    CHECK(CoXlTestLwSenseRule(0, CoXlTestLwBodyDist(6.5f, 0.8f), CoXlTestLwSenseReach(0.24f, 40.0f, 0, 0.0f), 0, 1) == 0);
    g_case = "the hearing writer's two exits"; /* 0 yes, 13 the detector entity's +0x14 bit 0 clear, 14 the source's tag bit 25 */
    CHECK(CoXlTestLwSenseGate(1, 0) == 0);
    CHECK(CoXlTestLwSenseGate(3, 0) == 0);
    CHECK(CoXlTestLwSenseGate(0, 0) == 13);
    CHECK(CoXlTestLwSenseGate(2, 0) == 13);
    CHECK(CoXlTestLwSenseGate(1, 1ull << 25) == 14);
    CHECK(CoXlTestLwSenseGate(1, ~(1ull << 25)) == 0);
    CHECK(CoXlTestLwSenseGate(0, 1ull << 25) == 13);       /* the entity is tested first (0x888EF2) */
    g_case = "a notice uses the player's live place";
    { const float live[3] = {100, 200, 5}, beside[3] = {103.9f, 200, 50}, away[3] = {100, 204.1f, 5};
      CHECK(CoXlTestLwPlaceHeld(live, live) == 0); CHECK(CoXlTestLwPlaceHeld(live, beside) == 0); CHECK(CoXlTestLwPlaceHeld(live, away) == 1); }
    g_case = "checks go round the units and the players";
    { const uint64_t at[5] = {900, 0, 500, 500, 100}; const float d[5] = {3, 12, 9, 6, 1}; const uint8_t pl[5] = {0, 0, 0, 0, 1}; uint8_t taken[5] = {0};
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 0) == 1); taken[1] = 1; /* never checked: first, though the farthest */
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 0) == 3); taken[3] = 1; /* checked at the same time: the nearer */
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 0) == 2); taken[2] = 1;
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 0) == 0); taken[0] = 1; /* the nearest, checked last, comes last */
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 0) == -1);              /* another player's unit is not this one's */
      CHECK(CoXlTestLwSensePick(at, d, pl, taken, 5, 1) == 4);
      const uint8_t ok[4] = {1, 0, 1, 0}, none[4] = {0};
      CHECK(CoXlTestLwSenseNextPlayer(0, ok, 4) == 2); CHECK(CoXlTestLwSenseNextPlayer(2, ok, 4) == 0); CHECK(CoXlTestLwSenseNextPlayer(1, ok, 4) == 2);
      CHECK(CoXlTestLwSenseNextPlayer(3, ok, 4) == 0); CHECK(CoXlTestLwSenseNextPlayer(7, ok, 4) == 0); CHECK(CoXlTestLwSenseNextPlayer(0, none, 4) == -1);
      const uint8_t one[1] = {1}; CHECK(CoXlTestLwSenseNextPlayer(0, one, 1) == 0); }
    g_case = "a notice is confirmed and waits per detector"; /* confirm: 0 not yet, 1 left alert 0, 2 again, 3 left alone, 4 gone */
    CHECK(CoXlTestLwSenseDet(1, 2, 'n', 0xD1, 0x51, 10000) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD1, 0x52, 10500) == -2);   /* a squad mate on the same detector: told already */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD1, 0, 10500) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD1, 0, 10500) == 0);       /* one probe on: too early to call */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD1, 1, 11000) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD1, 0, 11000) == 1);       /* a unit of it left alert 0 */
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD1, 0, 11500) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD1, 0x52, 29999) == -2);   /* 20 s from the notice */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD1, 0x52, 30000) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD1, 1, 30500) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD1, 0, 30500) == 1);       /* left alert 0 at the first probe: no need to wait */
    CHECK(CoXlTestLwSenseDet(1, 2, 'n', 0xD2, 0x61, 10000) == 1);    /* no effect: again after 2 s, three notices in all */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD2, 0, 10999) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD2, 0, 10999) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD2, 0, 11000) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD2, 0, 11000) == 2);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 12999) == -2);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 13000) == 2);
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD2, 0, 14000) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD2, 0, 14000) == 2);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 16000) == 3);
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD2, 0, 17000) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD2, 0, 17000) == 3);       /* left alone ... */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 35999) == -2);   /* ... for 20 s from the last notice */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 36000) == 1);    /* then counted afresh */
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD2, 0, 37000) == 4);       /* none of its units seen since: no verdict, no retry */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD2, 0x61, 37001) == -2);
    CHECK(CoXlTestLwSenseDet(1, 2, 'n', 0xD3, 0x71, 10000) == 1);    /* seen one probe on, then no more: gone, not "no effect" */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD3, 0, 10500) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD3, 0, 10500) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD3, 0, 11000) == 4);
    CHECK(CoXlTestLwSenseDet(1, 2, 'n', 0xD4, 0x81, 10000) == 1);    /* gone after a second notice: the count starts afresh */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xD4, 0, 11000) == 0);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD4, 0, 11000) == 2);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD4, 0x81, 13000) == 2);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xD4, 0, 14000) == 4);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xD4, 0x81, 33000) == 1);
    CHECK(CoXlTestLwSenseDet(1, 2, 'c', 0xE, 0, 500) == -1);         /* the table: asking after a detector takes no slot */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xA, 1, 1000) == 1);         /* a detector keeps its wait with no unit slot */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xB, 2, 2000) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xC, 3, 3000) == -1);        /* both waiting: no slot */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xA, 1, 1500) == 0); CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xA, 0, 1500) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xB, 1, 2500) == 0); CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xB, 0, 2500) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xC, 3, 20999) == -1);       /* confirmed, but the waits still run */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xC, 3, 22000) == 1);        /* both over: the older one (0xA) goes ... */
    CHECK(CoXlTestLwSenseDet(0, 2, 's', 0xB, 0, 22000) == 0);        /* ... and 0xB still has its slot */
    CHECK(CoXlTestLwSenseDet(0, 2, 'n', 0xB, 2, 22000) == 1);
    CHECK(CoXlTestLwSenseDet(0, 2, 'c', 0xA, 0, 22000) == -1);
    g_case = "the game's hostility test"; /* entity +0x14, +0x13F0, always, avatar, lists, faction, mask */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x2, 0x4) == 1);   /* no shared faction bit */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x6, 0x4) == 0);   /* a shared faction bit: an ally */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 0, 0x2, 0x4) == 0);   /* the list off */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x2, 0x0) == 0);   /* no mask */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 1, 0, 0x6, 0x4) == 1);   /* every avatar an enemy */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 0, 0, 0x6, 0x4) == 0);   /* ... but the target is not an avatar */
    CHECK(CoXlTestLwHostileRule(2, 1, 1, 1, 1, 0x2, 0x4) == 0);   /* the detector entity's bit 1: it scans nothing */
    CHECK(CoXlTestLwHostileRule(1, 1, 0, 1, 1, 0x2, 0x4) == 1);   /* its bit 0 alone */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 1, 1, 0x0, 0x4) == 0);   /* faction 0 */
    CHECK(CoXlTestLwHostileRule(0, 0, 1, 1, 1, 0x2, 0x4) == 0);   /* +0x13F0 off */
    g_case = "a notice takes the route the game reads"; /* 0 hearing, 1 flag 0x10 */
    CHECK(CoXlTestLwSenseRoute(0.0f) == 0);
    CHECK(CoXlTestLwSenseRoute(0.99f) == 0);
    CHECK(CoXlTestLwSenseRoute(1.0f) == 1);  /* the game's comiss/jb: 1.0 is the eye's case */
    CHECK(CoXlTestLwSenseRoute(3.0f) == 1);
    g_case = "a unit the awareness update skips is not noticed"; /* 0 yes, 10 state 16, 11 flags */
    CHECK(CoXlTestLwSenseLost(0, 0) == 0);
    CHECK(CoXlTestLwSenseLost(0x1, 3) == 0);   /* bit 0 is not one of them */
    CHECK(CoXlTestLwSenseLost(0x10, 3) == 0);  /* nor bit 4 */
    CHECK(CoXlTestLwSenseLost(0x2, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x4, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x8, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x20, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0, 0x10) == 10);
    CHECK(CoXlTestLwSenseLost(0x2, 0x10) == 10); /* state 16 is tested first (0x7EB136) */
}

int main(void)
{
    uint8_t* image = Reset("the living world's installs");
    uint8_t defer[44];
    CoXlTestLwPins(image);
    /* HellForge's spawn guard has hooked the spawn request: still the reviewed code under the jump */
    g_fake.underHook[0].rva = SPAWN; memcpy(g_fake.underHook[0].bytes, image + SPAWN, 16);
    memcpy(image + SPAWN, "\xFF\x25\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 14);
    memcpy(defer, image + DEFER_SITE, sizeof defer);
    CHECK(CoXlTestLiving((uintptr_t)image) == 1); /* on with no environment variable: the game is the reviewed build */
    /* no density guards in this game: the setup pass is the game's, not rerouted */
    CHECK(memcmp(defer, image + DEFER_SITE, sizeof defer) == 0 && fake_host_logged("no density guards"));

    NoiseFn noise = (NoiseFn)fake_host_detour(NOISE);
    CHECK(noise != NULL);
    if (noise != NULL)
    {
        const float at[3] = {10.0f, 20.0f, 0.0f};
        const LONG head = CoXlTestNoiseHead();
        CoXlTestLive(0); /* the ship or a normal mission: no noise is kept */
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head);
        CoXlTestLive(1);
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head + 1);
        CoXlTestLive(0);
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head + 1);
    }

    /* the director's patrols: the gate's stub reads the live decision, and the rows stay the game's */
    image = g_fake.image;
    CHECK(image[CO_PATROL_GATE] == 0xE9);
    if (image[CO_PATROL_GATE] == 0xE9)
    {
        int32_t rel; memcpy(&rel, image + CO_PATROL_GATE + 1, 4);
        const uint8_t* flag = image + CO_PATROL_GATE + 5 + rel + 0x1000;
        CoXlTestLive(1);
        CHECK(*flag == 1);
        CoXlTestLive(0);
        CHECK(*flag == 0);
    }

    g_case = "a passing player is held at its own place, also after the player manager shifted its index";
    {
        float use[2];
        const float a0[2] = {100.0f, 200.0f}, a1[2] = {900.0f, 200.0f}, b0[2] = {5000.0f, 7000.0f}, b1[2] = {5001.0f, 7000.0f};
        CHECK(CoXlTestLwSettle(0, 0xA, a0, 1000, use) == 0 && use[0] == 100.0f);
        CHECK(CoXlTestLwSettle(0, 0xA, a1, 1200, use) == 1 && use[0] == 100.0f && use[1] == 200.0f); /* a pod: held */
        CHECK(CoXlTestLwSettle(0, 0xB, b0, 1400, use) == 0 && use[0] == 5000.0f && use[1] == 7000.0f); /* B now at index 0 */
        CHECK(CoXlTestLwSettle(0, 0xB, b1, 1600, use) == 0 && use[0] == 5001.0f);
        CHECK(CoXlTestLwSettle(1, 0xA, a1, 1600, use) == 0 && use[0] == 900.0f); /* A at index 1: its reading as it is */
    }
    SightVerdict();

    if (g_failed) { printf("livingworld: %d checks failed\n", g_failed); return 1; }
    printf("livingworld: all cases passed\n");
    return 0;
}

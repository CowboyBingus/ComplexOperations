/* Research build only: an FRV spawned and driven along a road by test commands, so one launch says whether the road
 * can be driven. Commands: frv where arterial|spur [K], frv spawn, frv drive arterial M, frv drive spur K (-1: the
 * nearest), frv speed V, frv stop. All in game.dll 25480438; each fact is at the code that relies on it.
 * The Helldiver is not seated (the seat calls skip the interaction's own start): the drive is the vehicle manager's
 * input record, written before the game's update copies it to the motion controls (0xAAC300, drive state 1), which
 * is where a seated driver's keys arrive too. The test teleport keeps the Helldiver near, since the ground is
 * detailed around players only. */
#ifdef CO_RESEARCH
#include "../co_frv.h"

#define FRV_ENTITY 0xCC21C7FFD3EBEFB9ull
#define FRV_UNIT_TYPE 0xE0A48D0BE9A7453Full /* the engine's resource type "unit", as its own unit spawn asks (exe 0x1A7EDC) */
#define FRV_STRATAGEM 105u  /* DropoffFrv, the M-102: the stratagem enum's name list and HellForge's HF_STRATAGEM_M102_GUNNER_FRV */
#define FRV_VEHICLES 0x3326668u
#define FRV_MOTION 0x3326458u
#define FRV_API 0x3326308u
#define FRV_NEAR 150.0f     /* m: a leg starts this near the Helldiver, where the ground is detailed */
#define FRV_FOLLOW 35.0f    /* m: further behind than this, the Helldiver is put on the road behind the vehicle */
#define FRV_FOLLOW_BACK 14.0f
#define FRV_END 3.0f        /* m of the leg left when the drive counts as done */
#define FRV_LOAD_SECONDS 60.0

enum { FRV_IDLE, FRV_LOAD, FRV_SETTLE, FRV_DRIVE, FRV_BRAKE };
static struct
{
    int state, seq, flip, drove, asked, shifts; uint32_t entity, segment, packageCount; LONG mission, askedMission;
    CoFrvPath path; CoFrvStats stats; CoFrvSign sign;
    float want, speedWant, lastSteer, lastHeading[2], settledFor, legMetres, place[4];
    LONGLONG t0, last, poseAt, followAt, brakeAt, readyAt, shiftAt;
    uint64_t packages[10];
    char leg[96];
} g_frv = {.speedWant = 8.0f};

typedef struct FrvPose { float pos[3], q[4], vel[2], gameSpeed, fuel; int wheels, wheelCount, gear; uintptr_t input; } FrvPose;

static double FrvSeconds(LONGLONG a, LONGLONG b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)(b - a) / (double)f.QuadPart; }
static LONGLONG FrvNow(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }

/* The code this file's layouts were read from: any other build's bytes refuse every command. */
static int FrvPins(void)
{
    static int ok = -1;
    if (ok >= 0) return ok;
    const uintptr_t g = g_lw.game, exe = (uintptr_t)GetModuleHandleW(NULL);
    ok = ApiCodeMatches(g + 0xFD9710u, "4C 8B DC 49 89 5B 10 49 89 73 20 57 48 81 EC A0 00 00 00 48 8B 05 E6 28 66 01 48 33 C4 48 89 84 24 90 00 00 00 33 C0 49") &&
        ApiCodeMatches(g + 0xFDC0C0u, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 48 8B 05") &&
        ApiCodeMatches(g + 0xFDC140u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56") &&
        ApiCodeMatches(g + 0x581320u, "4C 8B DC 55 53 49 8D AB 78 F7 FF FF 48 81 EC 78 09 00 00 48 8B 05 D6 AC") &&
        ApiCodeMatches(g + 0xAAC300u, "4C 8B DC 53 56 57 41 54 41 57 48 81 EC 10 01 00 00 41 0F 29 73 B8 45 0F") &&
        ApiCodeMatches(g + 0x7152F0u, "40 55 57 48 8D AC 24 88 EB FF FF B8 88 15 00 00 E8 BB F2 9E 01 48 2B E0") &&
        ApiCodeMatches(g + 0x6FEF80u, "40 55 53 41 55 41 57 48 8D AC 24 28 F7 FF FF 48 81 EC F8 09 00 00 44 0F") &&
        ApiCodeMatches(g + 0x4F95C0u, "48 85 C9 74 71 48 8B 05 CC 29 F7 02 44 8B C1 4C 8B 90 38 27 F1 00 48 B8") &&
        ApiCodeMatches(exe + 0x321930u, "48 83 EC 28 48 89 4C 24 38 4C 8D 44 24 30 48 8B 0D C3 E8 6E 01 48 89 54") &&
        /* the packages: a stratagem's package list (to its lea of the stratagem table), the request, the engine's has_loaded */
        ApiCodeMatches(g + 0x1753080u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 33 FF 4C 8D 05") &&
        ApiCodeMatches(g + 0x12FE350u, "45 85 C0 0F 84 75 01 00 00 41 56 41 57 48 83 EC 38 48 89 5C 24 50 4C 8B") &&
        ApiCodeMatches(exe + 0x321330u, "48 83 EC 28 48 8B 05 CD EE 6E 01 48 8B D1 48 8B 88 00 04 00 00 48 8B 89 08 02 00 00") &&
        /* the fields: wheel contacts, in use, position, rotation, velocity, the handbrake's two sources, the gear's shifts */
        ApiCodeMatches(g + 0x716610u, "43 C7 84 37 5C 06 00 00 00 00 00 00") && ApiCodeMatches(g + 0x716734u, "83 BC 8D 58 06 00 00 00 74 08 43 FF 84 37 5C 06 00 00") &&
        ApiCodeMatches(g + 0x715432u, "41 80 7C 1F 4F 00") && ApiCodeMatches(g + 0x71615Fu, "F2 42 0F 11 7C 38 18 42 89 5C 38 20") &&
        ApiCodeMatches(g + 0x71618Fu, "42 0F 11 74 38 24") && ApiCodeMatches(g + 0x7161CCu, "F2 42 0F 11 44 39") &&
        ApiCodeMatches(g + 0x715E49u, "80 7E 0E 00 75 11 48 8B 45 B0 41 80 7C 07 4E 00 75 05") &&
        ApiCodeMatches(g + 0x7159FBu, "80 7E 0C 00 F3 0F 11 4E 08 45 8B B5 18 06 00 00 74 48 41 83 FE 04 73 42") &&
        /* the input record: has-input, accelerate, steer and brake, handbrake and the two shifts, the fuel, the drive state */
        ApiCodeMatches(g + 0xAAC69Au, "80 79 2C 00") && ApiCodeMatches(g + 0xAAC6B4u, "F3 0F 10 79 18") &&
        ApiCodeMatches(g + 0xAAC6C0u, "F3 0F 10 41 20 F3 44 0F 10 49 1C F3 0F 11 03 88 43 0E 0F B6 41 2E 88 43 0C 0F B6 41 2F") &&
        ApiCodeMatches(g + 0xAAC5BFu, "48 8B 46 70 F3 46 0F 10 44 E0 04") && ApiCodeMatches(g + 0x6FEFD0u, "49 8B 4F 60 8B D3 44 8B 44 D1 04");
    if (!ok) Note("frv: the game's vehicle code is not the reviewed one: no FRV commands in this session");
    return ok;
}

/* The vehicle's records, or why not. The input record is given only for a vehicle this machine drives the wheeled
 * way: in the manager's authoritative prefix ([VM+0x2C], the pre-pass's bound at 0x6FEFC7) with drive state 1, the
 * one 0xAAC300 serves (0 after death or drowning, 2 a walker), and a motion record this machine simulates. */
static const char* FrvRead(uint32_t entity, FrvPose* p)
{
    uintptr_t vm = 0, mm = 0, inputs = 0, states = 0, fuels = 0, recs = 0, sync = 0, st = 0; uint32_t i = 0, m = 0, drive = 0; uint8_t flags[3] = {0, 0, 0};
    memset(p, 0, sizeof *p); p->wheels = p->gear = -1;
    if (!EvpAlive(entity)) return "the vehicle is gone";
    if (!EvpPtr(g_lw.game + FRV_VEHICLES, &vm) || !EvpHash(vm + 0x38, entity, 8, 4, &i) || i >= EvpU32(vm + 0x2C, 0)) return "the vehicle is not one this machine drives";
    if (!EvpPtr(vm + 0x60, &states) || SafeRead(states + 8ull * i + 4, &drive, 4)) return "the vehicle's drive state does not read";
    if (drive != 1) return drive == 0 ? "the vehicle's drive state is 0 (destroyed, under water, or never set)" : "the vehicle's drive state is not the wheeled one";
    if (!EvpPtr(g_lw.game + FRV_MOTION, &mm) || !EvpHash(mm + 0x40, entity, 8, 4, &m) || m >= EvpU32(mm + 0x34, 0)) return "the vehicle's motion is not simulated here";
    /* sync +0x4D the handbrake as applied, +0x4E its second source beside the controls' (0x715E49), +0x4F in use (0x715432) */
    if (!EvpPtr(mm + 0x78, &sync) || SafeRead(sync + 0x58ull * m + 0x4D, flags, 3) || !flags[2]) return "the vehicle's motion record is not in use";
    if (flags[1]) return "the game holds the vehicle's handbrake (motion record +0x4E)";
    /* published by the owner every update: the unit's world position and rotation, its actor's velocity across the ground */
    if (SafeRead(sync + 0x58ull * m + 0x18, p->pos, 12) || SafeRead(sync + 0x58ull * m + 0x24, p->q, 16) || SafeRead(sync + 0x58ull * m + 0xC, p->vel, 8)) return "the vehicle's pose does not read";
    for (int k = 0; k < 3; k++) if (!isfinite(p->pos[k])) return "the vehicle's pose is not a number";
    if (EvpPtr(mm + 0x70, &st)) { p->wheels = (int)EvpU32(st + 0x670ull * m + 0x65C, 0); p->gear = (int)EvpU32(st + 0x670ull * m + 0x618, 0xFFFFFFFFu); }
    if (EvpPtr(vm + 0x68, &recs))
    {
        float kmh = 0.0f;
        p->wheelCount = (int)EvpU32(recs + 0xD28ull * i + 8, 0);
        /* the physics vehicle's own speed; taken as km/h because 0x716E90 divides a difference of it by 3.6 */
        if (!SafeRead(recs + 0xD28ull * i + 0xC24, &kmh, 4)) p->gameSpeed = kmh / 3.6f;
    }
    p->fuel = -1.0f;
    if (EvpPtr(vm + 0x70, &fuels)) SafeRead(fuels + 8ull * i + 4, &p->fuel, 4); /* 0 or less: 0xAAC300 passes no accelerate */
    if (!EvpPtr(vm + 0x58, &inputs)) return "the vehicle has no input record";
    p->input = inputs + 0x30ull * i;
    return NULL;
}

/* The input record's fields 0xAAC300 reads when +0x2C (has-input) is set: +0x18 accelerate, +0x1C brake, +0x20 steer,
 * +0x2D handbrake, +0x2E shift up (one gear a frame it is set), +0x2F shift down. Read whole first, so only a record
 * that reads is written. */
static void FrvWrite(uintptr_t input, float accelerate, float brake, float steer, int handbrake, int shiftUp, int has)
{
    uint8_t r[0x30];
    if (input == 0 || SafeRead(input, r, sizeof r)) return;
    memcpy(r + 0x18, &accelerate, 4); memcpy(r + 0x1C, &brake, 4); memcpy(r + 0x20, &steer, 4);
    r[0x2C] = (uint8_t)has; r[0x2D] = (uint8_t)handbrake; r[0x2E] = (uint8_t)shiftUp; r[0x2F] = 0;
    memcpy((void*)(input + 0x18), r + 0x18, 0x18);
}

/* The resource API's function at `slot`, only when the game's table leads to the reviewed code in the executable. */
static uintptr_t FrvResourceApi(uint32_t slot, uint32_t reviewed)
{
    uintptr_t api = 0, res = 0, fn = 0;
    if (!EvpPtr(g_lw.game + FRV_API, &api) || !EvpPtr(api + 0x10, &res) || !EvpPtr(res + slot, &fn) || fn != (uintptr_t)GetModuleHandleW(NULL) + reviewed) return 0;
    return fn;
}

/* The FRV's unit resource name, or why the FRV cannot be made at all: its definition (the construct 0x581320 reads
 * address 0 for a hash the 4096-slot table lacks) and its unit data, whose first field the unit component spawns. */
static const char* FrvUnit(uint64_t* name)
{
    uintptr_t em = 0, defs = 0;
    if (!EvpPtr(g_lw.game + 0x346BF98u, &em) || !EvpPtr(em + 0xF12EA0u, &defs)) return "no entity definitions";
    int found = 0;
    for (uint32_t n = 0, at = (uint32_t)(FRV_ENTITY & 0xFFFu); n < 4096 && !found; n++, at = (at + 1) & 0xFFFu)
    {
        uint64_t d[3];
        if (SafeRead(defs + 32ull * at, d, sizeof d) || d[0] == 0) break;
        found = d[0] == FRV_ENTITY && (uint32_t)d[2] > 0 && d[1] > 0x10000;
    }
    if (!found) return "the FRV has no entity definition";
    const uintptr_t data = ((uintptr_t (*)(uint64_t))(g_lw.game + 0x4F95C0u))(FRV_ENTITY);
    if (data == 0 || SafeRead(data, name, 8) || *name == 0) return "the FRV has no unit data";
    return NULL;
}

/* Whether the engine has the FRV's unit: its unit spawn (exe 0x1A7E50) gets the resource and reads it unchecked, so a
 * spawn before this says yes ends the game. -1: the API is not the reviewed one. */
static int FrvUnitLoaded(uint64_t name)
{
    const uintptr_t canGet = FrvResourceApi(0x388, 0x321930u);
    return canGet == 0 ? -1 : ((uint8_t (*)(uint64_t, uint64_t))canGet)(FRV_UNIT_TYPE, name) != 0;
}

/* The FRV's packages asked of the game as a loadout holding it would: 0x1753080 lists a stratagem's packages (up to
 * the 10 its own callers give room for) and 0x12FE350 counts a user for each and loads the new ones. Once a mission:
 * this file never gives the count back. NULL asked, else why not. */
static const char* FrvAsk(int seq)
{
    const uintptr_t lea = g_lw.game + 0x175309Cu; /* lea r8, the stratagem table: HellForge moves the table and rewrites it */
    uintptr_t info = 0, mgr = 0, extra = 0; int32_t rel = 0; uint32_t variants = 0, others = 0, effect = 0; uint64_t main = 0, ids[32];
    if (g_frv.asked && g_frv.askedMission == g_lwMission) return NULL; /* a mission's end may give every package back */
    if (g_lwWorld.playersAt > 1) return "the FRV is not loaded and another player is in the mission, whose game would not have it either";
    if (SafeRead(lea + 3, &rel, 4) || !EvpPtr(lea + 7 + (intptr_t)rel + 8ull * FRV_STRATAGEM, &info) || SafeRead(info + 0xA8, &main, 8) ||
        SafeRead(info + 0xF8, &effect, 4) || SafeRead(info + 0x120, &variants, 4) || SafeRead(info + 0x180, &others, 4) || variants + others > 24)
        return "the FRV's stratagem record does not read";
    if ((variants != 0 && !EvpPtr(info + 0x118, &extra)) || (others != 0 && !EvpPtr(info + 0x178, &extra))) return "the FRV's stratagem record's lists do not read";
    if (effect != 0 && !EvpPtr(g_lw.game + 0x33269E8u, &extra)) return "the stratagem's effect table is not there";
    if (!EvpPtr(g_lw.game + 0x347CEB0u, &mgr) || FrvResourceApi(0x310, 0x321330u) == 0) return "the package manager is not the reviewed one";
    memset(ids, 0, sizeof ids);
    const uint32_t n = ((uint32_t (*)(uint64_t*, uint32_t, void*, uint32_t))(g_lw.game + 0x1753080u))(ids, FRV_STRATAGEM, NULL, 0);
    if (n == 0 || n > 10) return "the FRV's stratagem lists no packages (or more than the game's own 10)";
    ((void (*)(uintptr_t, uint64_t*, uint32_t))(g_lw.game + 0x12FE350u))(mgr, ids, n);
    memcpy(g_frv.packages, ids, sizeof g_frv.packages); g_frv.packageCount = n; g_frv.asked = 1; g_frv.askedMission = g_lwMission;
    Note("living world: test command %d: frv: the FRV's unit is not loaded (no FRV in a loadout): asked the game for stratagem %u's %u packages, the first %016llX",
        seq, FRV_STRATAGEM, n, (unsigned long long)ids[0]);
    return NULL;
}

/* How many of the asked packages the engine has in whole (has_loaded: listed, every part in state 4). */
static uint32_t FrvPackagesIn(void)
{
    const uintptr_t loaded = FrvResourceApi(0x310, 0x321330u);
    uint32_t in = 0;
    for (uint32_t k = 0; loaded != 0 && k < g_frv.packageCount; k++) in += ((uint8_t (*)(uint64_t))loaded)(g_frv.packages[k]) != 0;
    return in;
}

/* An FRV on the ground at (x, y), facing (fx, fy): its entity, 0 refused (noted). */
static uint32_t FrvSpawn(int seq, float x, float y, float fx, float fy)
{
    int host = 0; float z = 0.0f; uintptr_t vm = 0; uint64_t name = 0;
    const float l = sqrtf(fx * fx + fy * fy);
    const char* why = FrvUnit(&name);
    const int loaded = why == NULL ? FrvUnitLoaded(name) : 0;
    if (why != NULL) {}
    else if (loaded < 0) why = "the resource API is not the reviewed one";
    else if (!loaded) why = "the FRV's unit is not loaded: spawning it would end the game";
    else if (!ActMission(g_lw.game, &host, NULL) || !host) why = "only the host, in a mission";
    else if (!(l > 1.0e-3f) || !isfinite(x) || !isfinite(y)) why = "no place or facing";
    else if (!LwGround(x, y, &z)) why = "no ground there";
    /* XL: where a tile's collision is not in yet the ground ray hits something far below the terrain */
    else if (g_lwWorld.radius > 1500.0f && g_near.global != NULL && g_near.level == g_world.levels && z < NearGlobalAt(x, y) - 3.0f) why = "the ground reads over 3 m under the height plan there";
    else if (!EvpPtr(g_lw.game + FRV_VEHICLES, &vm) || EvpU32(vm + 0x24, 64) >= 48) why = "the vehicle manager is nearly full";
    if (why != NULL) { Note("living world: test command %d: frv: no spawn at (%.0f, %.0f): %s", seq, (double)x, (double)y, why); return 0; }
    fx /= l; fy /= l;
    /* the spawn's 0x58 bytes, as HellForge's spawn_pelican fills them: +0 networked, +4 0x7FFF (no owner slot), +8 the
     * pose, +0x48 null for the game's own context (0xFDC22A), +0x50 null for 0xFD9710's own list of delta ids */
    _Alignas(8) uint8_t d[0x58];
    memset(d, 0, sizeof d);
    d[0] = 1;
    const uint32_t none = 0x7FFF; memcpy(d + 4, &none, 4);
    /* rows: the unit's x, y and z axes, then the position, as the engine keeps a world pose; y is taken as the nose
     * (the settled and pose lines say how far off that is) */
    const float pose[16] = {fy, -fx, 0, 0, fx, fy, 0, 0, 0, 0, 1, 0, x, y, z + 1.0f, 1};
    memcpy(d + 8, pose, sizeof pose);
    uint32_t out = 0, unit = 0;
    ((uint32_t* (*)(uint32_t*, uint64_t, void*))(g_lw.game + 0xFD9710u))(&out, FRV_ENTITY, d);
    if (!out || !EvpAlive(out) || EvpEntityResource(g_lw.game, out, &unit) != FRV_ENTITY)
    { Note("living world: test command %d: frv: the game made no FRV at (%.0f, %.0f)", seq, (double)x, (double)y); return 0; }
    Note("living world: test command %d: frv: spawned FRV %08X (unit %08X) at (%.1f, %.1f, %.1f) facing (%.2f, %.2f)", seq, out, unit, (double)x, (double)y,
        (double)(z + 1.0f), (double)fx, (double)fy);
    return out;
}

/* The spawn at g_frv.place, and the wait for the vehicle to come to rest. */
static void FrvBegin(void)
{
    g_frv.entity = FrvSpawn(g_frv.seq, g_frv.place[0], g_frv.place[1], g_frv.place[2], g_frv.place[3]);
    g_frv.state = g_frv.entity ? FRV_SETTLE : FRV_IDLE;
    g_frv.t0 = g_frv.last = FrvNow(); g_frv.settledFor = 0.0f;
    g_frv.lastHeading[0] = g_frv.place[2]; g_frv.lastHeading[1] = g_frv.place[3];
}

/* A route edge's points' range when it is a road with points; arterial: whether the roads' build marked it. */
static int FrvEdge(uint32_t e, uint32_t* range, int* arterial)
{
    const uintptr_t G = GenerationBlock(g_lw.game);
    uint8_t rec[0x48]; uint16_t flags;
    if (G == 0 || SafeRead(G + g_routeEdges + 0x48ull * e, rec, sizeof rec)) return 0;
    memcpy(range, rec + 0x38, 8); memcpy(&flags, rec + 0x44, 2);
    if (rec[0x34] == 2 || rec[0x34] == 8 || (flags & 2) || range[1] > g_lwRoads.count || range[1] < range[0] + 2) return 0;
    *arterial = g_arterials.level == g_world.levels && e < ROADS_EDGES_MAX && ((g_arterials.bits[e >> 3] >> (e & 7)) & 1);
    return 1;
}

/* The leg asked for, from the Helldiver's place: 1 with g_frv.path and g_frv.leg set, else the reason in why. */
static int FrvLeg(const char* kind, float metres, int which, char* why, size_t whySize)
{
    const float* pl = g_lwWorld.players[0];
    const float (*pts)[2] = (const float (*)[2])g_lwRoads.pts;
    if (g_lwRoads.count < 2 || g_lwWorld.playersAt == 0) { snprintf(why, whySize, "the level's road points are not read (or no player)"); return 0; }
    if (!strcmp(kind, "arterial"))
    {
        const uintptr_t G = GenerationBlock(g_lw.game);
        uint32_t edges = 0, best = 0, bestAt = 0, bestRange[2] = {0, 0}; float bd = 3.0e38f;
        if (G == 0 || SafeRead(G + 0x11A71CCu, &edges, 4) || edges > 65536) edges = 0;
        for (uint32_t e = 0; e < edges; e++)
        {
            uint32_t range[2]; int art = 0; float d;
            if (!FrvEdge(e, range, &art) || !art) continue;
            const uint32_t at = co_frv_nearest(pts, range[0], range[1], pl[0], pl[1], &d);
            if (d < bd) { bd = d; best = e; bestAt = at; bestRange[0] = range[0]; bestRange[1] = range[1]; }
        }
        if (bd > 1.0e30f) { snprintf(why, whySize, "the level has no arterial with points"); return 0; }
        /* towards the end with more road left, so a leg is as long as asked where the road allows */
        const int step = bestRange[1] - 1 - bestAt >= bestAt - bestRange[0] ? 1 : -1;
        snprintf(g_frv.leg, sizeof g_frv.leg, "arterial edge %u from (%.0f, %.0f)", best, (double)pts[bestAt][0], (double)pts[bestAt][1]);
        if (co_frv_leg(pts, bestRange[0], bestRange[1], bestAt, step, metres, &g_frv.path) < 2) { snprintf(why, whySize, "%s has no road left", g_frv.leg); return 0; }
        return 1;
    }
    if (!strcmp(kind, "spur"))
    {
        if (g_spurs.level != g_world.levels || g_spurs.count == 0) { snprintf(why, whySize, "the level has no spurs"); return 0; }
        int best = -1; float bd = 3.0e38f;
        for (uint32_t k = 0; k < g_spurs.count && k < ROADS_SPURS; k++)
        {
            const RoadSpur* sp = &g_spurs.spur[k];
            if (sp->end > g_lwRoads.count || sp->end < sp->first + 2 || (which >= 0 && (uint32_t)which != k)) continue;
            const float d = LwDist2(pts[sp->end - 1], pl[0], pl[1]);
            if (d < bd) { bd = d; best = (int)k; }
        }
        if (best < 0) { snprintf(why, whySize, "no such spur with points"); return 0; }
        /* a spur's points run from its place to the arterial: the pad entry is driven the other way */
        const RoadSpur* sp = &g_spurs.spur[best];
        snprintf(g_frv.leg, sizeof g_frv.leg, "spur %d from (%.0f, %.0f) to its place at (%.0f, %.0f)", best, (double)pts[sp->end - 1][0], (double)pts[sp->end - 1][1],
            (double)pts[sp->first][0], (double)pts[sp->first][1]);
        if (co_frv_leg(pts, sp->first, sp->end, sp->end - 1, -1, 1.0e6f, &g_frv.path) < 2) { snprintf(why, whySize, "%s has no points", g_frv.leg); return 0; }
        return 1;
    }
    snprintf(why, whySize, "a leg is \"arterial M\" or \"spur K\"");
    return 0;
}

/* The end of whatever is under way. The vehicle is left on its handbrake with no pedal: in the game's automatic
 * gears a brake held at a standstill selects reverse and then drives backwards (0x7152F0's gear block). */
static void FrvEnd(const FrvPose* p, const char* ended, int drove)
{
    char line[640];
    if (p != NULL && p->input) FrvWrite(p->input, 0.0f, 0.0f, 0.0f, 1, 0, 1);
    if (g_frv.state == FRV_DRIVE)
    {
        co_frv_summary_text(line, sizeof line, &g_frv.stats, g_frv.leg, g_frv.legMetres, ended, drove);
        Note("%s", line);
        if (g_frv.shifts) Note("frv: shift-up was pressed %d times: the game's automatic gears are off in this profile", g_frv.shifts);
    }
    else Note("frv: ended before a drive: %s", ended);
    g_frv.state = p != NULL && p->input ? FRV_BRAKE : FRV_IDLE; g_frv.brakeAt = FrvNow(); g_frv.drove = drove;
}

/* Every game update, before the game's own: the drive's step. */
static void FrvTick(void)
{
    if (g_frv.state == FRV_IDLE) return;
    FrvPose p; const LONGLONG now = FrvNow();
    const int left = g_frv.mission != g_lwMission || !LwLive();
    if (g_frv.state == FRV_LOAD)
    {
        uint64_t name = 0;
        const double waited = FrvSeconds(g_frv.t0, now);
        if (left) { Note("frv: ended before a drive: the level left play"); g_frv.state = FRV_IDLE; return; }
        const uint32_t in = FrvPackagesIn();
        /* in whole and the unit to be had for a second: the packages' parts come in over several updates */
        if (in == g_frv.packageCount && FrvUnit(&name) == NULL && FrvUnitLoaded(name) == 1) { if (g_frv.readyAt == 0) g_frv.readyAt = now; }
        else g_frv.readyAt = 0;
        if (g_frv.readyAt != 0 && FrvSeconds(g_frv.readyAt, now) >= 1.0)
        {
            Note("frv: the FRV's %u packages are loaded after %.1f s and its unit can be had: spawning", g_frv.packageCount, waited);
            FrvBegin();
        }
        else if (waited > FRV_LOAD_SECONDS)
        {
            Note("living world: test command %d: frv: no spawn at (%.0f, %.0f): %u of the FRV's %u packages loaded after %.0f s and its unit %s", g_frv.seq, (double)g_frv.place[0],
                (double)g_frv.place[1], in, g_frv.packageCount, waited, g_frv.readyAt != 0 ? "can be had" : "cannot be had");
            g_frv.state = FRV_IDLE;
        }
        return;
    }
    const char* gone = left ? "the level left play" : FrvRead(g_frv.entity, &p);
    if (g_frv.state == FRV_BRAKE)
    {
        /* the handbrake is held for two seconds, then the record is given back as an empty vehicle's; the controls'
         * handbrake stays as last copied (0xAAC300 leaves it without has-input), so the vehicle stays parked */
        if (gone != NULL || FrvSeconds(g_frv.brakeAt, now) > 2.0) { if (gone == NULL) FrvWrite(p.input, 0.0f, 0.0f, 0.0f, 0, 0, 0); g_frv.state = FRV_IDLE; }
        else FrvWrite(p.input, 0.0f, 0.0f, 0.0f, 1, 0, 1);
        return;
    }
    /* a new vehicle's records are made over its first updates */
    if (gone != NULL && g_frv.state == FRV_SETTLE && !left && EvpAlive(g_frv.entity) && FrvSeconds(g_frv.t0, now) < 3.0) return;
    if (gone != NULL) { Note("frv: %s", gone); FrvEnd(NULL, gone, 0); return; }
    const float dt = (float)FrvSeconds(g_frv.last, now), t = (float)FrvSeconds(g_frv.t0, now);
    g_frv.last = now;
    float up[3], nose[2], dir[2], pitch = 0.0f, roll = 0.0f;
    const float planar = sqrtf(p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1]);
    co_frv_axes(p.q, up, nose);
    /* the travel's direction once it moves; standing, the nose, else the leg's first stretch */
    if (planar > 1.5f) { dir[0] = p.vel[0] / planar; dir[1] = p.vel[1] / planar; }
    else if (nose[0] != 0.0f || nose[1] != 0.0f) { dir[0] = nose[0]; dir[1] = nose[1]; }
    else { dir[0] = g_frv.lastHeading[0]; dir[1] = g_frv.lastHeading[1]; }
    co_frv_tilt(up, dir, &pitch, &roll);
    if (g_frv.state == FRV_SETTLE)
    {
        g_frv.settledFor = p.wheels >= 3 && planar < 0.5f ? g_frv.settledFor + dt : 0.0f;
        if (g_frv.settledFor < 0.5f && t < 8.0f) return;
        /* how far the unit's y axis sits from the facing asked for: 0 when y is the nose, as the spawn's pose takes it */
        const float off = atan2f(g_frv.place[2] * nose[1] - g_frv.place[3] * nose[0], g_frv.place[2] * nose[0] + g_frv.place[3] * nose[1]) * 57.29578f;
        Note("frv: FRV %08X settled after %.1f s at (%.1f, %.1f, %.1f): %d of %d wheels on the ground, pitch %+.1f roll %+.1f deg, gear %d, fuel %.2f, nose %+.0f deg off the facing asked%s",
            g_frv.entity, (double)t, (double)p.pos[0], (double)p.pos[1], (double)p.pos[2], p.wheels, p.wheelCount, (double)pitch, (double)roll, p.gear, (double)p.fuel, (double)off,
            g_frv.settledFor < 0.5f ? " (not at rest: driven anyway)" : "");
        if (g_frv.path.count < 2) { g_frv.state = FRV_IDLE; return; } /* "frv spawn": not driven */
        if (!(p.fuel > 0.0f)) Note("frv: the FRV has no fuel: the game passes no accelerate (0xAAC300), so this drive will not move");
        g_frv.state = FRV_DRIVE; g_frv.t0 = g_frv.poseAt = g_frv.followAt = g_frv.shiftAt = now; g_frv.segment = 0; g_frv.shifts = 0;
        co_frv_stats_start(&g_frv.stats); memset(&g_frv.sign, 0, sizeof g_frv.sign); g_frv.flip = 0; g_frv.lastSteer = 0.0f;
        g_frv.lastHeading[0] = dir[0]; g_frv.lastHeading[1] = dir[1];
        return;
    }
    CoFrvAim aim;
    co_frv_aim(&g_frv.path, &g_frv.segment, p.pos, dir, 6.0f + 0.6f * planar, &aim);
    /* which way a steering input turns is learnt from the turn the last one got */
    const float turned = atan2f(g_frv.lastHeading[0] * dir[1] - g_frv.lastHeading[1] * dir[0], g_frv.lastHeading[0] * dir[0] + g_frv.lastHeading[1] * dir[1]);
    if (!g_frv.flip && co_frv_sign(&g_frv.sign, g_frv.lastSteer, turned, planar) < 0)
    { g_frv.flip = 1; Note("frv: a positive steering input turned the FRV left: the input's sign is flipped from here"); }
    g_frv.lastHeading[0] = dir[0]; g_frv.lastHeading[1] = dir[1];
    const float steer = co_frv_steer(aim.error, 0.6f) * (g_frv.flip ? -1.0f : 1.0f), throttle = co_frv_throttle(planar, g_frv.want, aim.error);
    /* with the game's automatic gears off nothing leaves neutral by itself: shift-up for one update, as a key press */
    const int shift = co_frv_shift(p.gear, throttle, (float)FrvSeconds(g_frv.shiftAt, now));
    if (shift)
    {
        g_frv.shiftAt = now;
        if (g_frv.shifts++ == 0) Note("frv: gear %d under throttle: pressing shift-up (the game's automatic gears did not engage)", p.gear);
    }
    FrvWrite(p.input, throttle, 0.0f, steer, 0, shift, 1);
    g_frv.lastSteer = steer * (g_frv.flip ? -1.0f : 1.0f);
    CoFrvSample a = {.t = t, .pos = {p.pos[0], p.pos[1], p.pos[2]}, .speed = planar, .gameSpeed = p.gameSpeed, .pitch = pitch, .roll = roll, .steer = steer, .throttle = throttle,
        .along = aim.along, .cross = aim.cross, .wheels = p.wheels, .wheelCount = p.wheelCount, .gear = p.gear, .station = aim.station, .stations = g_frv.path.count,
        .legMetres = g_frv.legMetres};
    a.headingOff = nose[0] != 0.0f || nose[1] != 0.0f ? atan2f(dir[0] * nose[1] - dir[1] * nose[0], dir[0] * nose[0] + dir[1] * nose[1]) * 57.29578f : 0.0f;
    if (co_frv_stats_add(&g_frv.stats, &a))
        Note("frv: stuck at (%.1f, %.1f, %.1f), station %u of %u, %.0f m along: under %.0f m gained in %.0f s with the throttle down (pitch %+.1f roll %+.1f deg, %d of %d wheels)",
            (double)p.pos[0], (double)p.pos[1], (double)p.pos[2], aim.station, g_frv.path.count, (double)aim.along, (double)CO_FRV_STUCK_METRES, (double)CO_FRV_STUCK_SECONDS,
            (double)pitch, (double)roll, p.wheels, p.wheelCount);
    if (FrvSeconds(g_frv.poseAt, now) >= 0.1)
    {
        char line[400];
        g_frv.poseAt = now;
        co_frv_pose_text(line, sizeof line, &a);
        Note("%s", line);
    }
    /* the Helldiver, left behind, is put on the road behind the vehicle: the ground is detailed around players only */
    if (FrvSeconds(g_frv.followAt, now) >= 1.0 && aim.along > FRV_FOLLOW_BACK + 2.0f && g_lwWorld.playersAt &&
        LwDist2(g_lwWorld.players[0], p.pos[0], p.pos[1]) > FRV_FOLLOW * FRV_FOLLOW)
    {
        float back[2] = {g_frv.path.xy[0][0], g_frv.path.xy[0][1]};
        g_frv.followAt = now;
        co_frv_point(&g_frv.path, aim.along - FRV_FOLLOW_BACK, back);
        g_lwTeleportForce = g_lwTeleportQuiet = 1;
        LwTestTeleport(g_frv.seq, back[0], back[1]);
        g_lwTeleportForce = g_lwTeleportQuiet = 0;
    }
    if (aim.along >= g_frv.legMetres - FRV_END) FrvEnd(&p, "reached the leg's end", g_frv.stats.stuck == 0 && g_frv.stats.upsideDown == 0);
    else if (g_frv.stats.stuck >= 2) FrvEnd(&p, "stuck twice", 0);
    else if (g_frv.stats.upsideDown >= 120) FrvEnd(&p, "rolled over", 0);
    else if (t > 60.0f + g_frv.legMetres / 2.0f) FrvEnd(&p, "out of time", 0);
}

static void FrvCmd(int seq, const char* cmd)
{
    char kind[16] = "", why[160] = ""; float v = 0.0f; int k = -1;
    if (!FrvPins()) return;
    if (sscanf(cmd, "frv speed %f", &v) == 1 && v >= 1.0f && v <= 30.0f) { g_frv.speedWant = v; Note("living world: test command %d: frv: the next drive wants %.1f m/s", seq, (double)v); return; }
    if (!strcmp(cmd, "frv stop"))
    {
        FrvPose p;
        if (g_frv.state == FRV_LOAD) { g_frv.state = FRV_IDLE; Note("frv: ended before a drive: stopped by command"); }
        else if (g_frv.state == FRV_SETTLE || g_frv.state == FRV_DRIVE) FrvEnd(FrvRead(g_frv.entity, &p) == NULL ? &p : NULL, "stopped by command", 0);
        else Note("living world: test command %d: frv: no drive to stop", seq);
        return;
    }
    if (g_frv.state != FRV_IDLE) { Note("living world: test command %d: frv: a drive is under way (frv stop ends it)", seq); return; }
    if (!LwLive() || g_lwWorld.playersAt == 0) { Note("living world: test command %d: frv: no complex mission in play", seq); return; }
    const float* pl = g_lwWorld.players[0];
    const int where = sscanf(cmd, "frv where %15s %d", kind, &k) >= 1, drive = !where && sscanf(cmd, "frv drive %15s %f", kind, &v) == 2;
    g_frv.path.count = 0;
    if (where || drive)
    {
        if (drive && !strcmp(kind, "spur")) k = (int)v;
        if (!FrvLeg(kind, drive && !strcmp(kind, "arterial") ? v : 100.0f, k, why, sizeof why)) { Note("living world: test command %d: frv: no leg: %s", seq, why); return; }
        const float* s = g_frv.path.xy[0];
        const float d = sqrtf(LwDist2(pl, s[0], s[1]));
        g_frv.legMetres = g_frv.path.at[g_frv.path.count - 1];
        /* where to stand for it: 12 m back from the start, off the vehicle's way */
        float bx = s[0] - g_frv.path.xy[1][0], by = s[1] - g_frv.path.xy[1][1]; const float bl = sqrtf(bx * bx + by * by);
        bx = s[0] + 12.0f * bx / bl; by = s[1] + 12.0f * by / bl;
        Note("living world: test command %d: frv: leg %s: %u points, %.0f m, starts at (%.1f, %.1f), %.0f m from the Helldiver at (%.0f, %.0f); stand at (%.1f, %.1f)", seq, g_frv.leg,
            g_frv.path.count, (double)g_frv.legMetres, (double)s[0], (double)s[1], (double)d, (double)pl[0], (double)pl[1], (double)bx, (double)by);
        if (where) return;
        if (d < 5.0f) { Note("living world: test command %d: frv: no drive: the Helldiver stands on the leg's start (the FRV would land on it)", seq); return; }
        if (d > FRV_NEAR) { Note("living world: test command %d: frv: no drive: the leg starts over %.0f m from the Helldiver (teleport there first)", seq, (double)FRV_NEAR); return; }
        g_frv.place[0] = s[0]; g_frv.place[1] = s[1]; g_frv.place[2] = -(bx - s[0]) / 12.0f; g_frv.place[3] = -(by - s[1]) / 12.0f;
    }
    else if (!strcmp(cmd, "frv spawn"))
    {
        float f[2] = {1, 0};
        LwCameraForward(f);
        const float fl = sqrtf(f[0] * f[0] + f[1] * f[1]);
        if (!(fl > 1.0e-3f)) { f[0] = 1.0f; f[1] = 0.0f; } else { f[0] /= fl; f[1] /= fl; }
        g_frv.place[0] = pl[0] + 8.0f * f[0]; g_frv.place[1] = pl[1] + 8.0f * f[1]; g_frv.place[2] = f[0]; g_frv.place[3] = f[1];
    }
    else { Note("living world: test command %d: frv: not a command (where, spawn, drive, speed, stop)", seq); return; }
    g_frv.seq = seq; g_frv.mission = g_lwMission; g_frv.want = g_frv.speedWant;
    co_frv_stats_start(&g_frv.stats);
    /* a mission with no FRV in a loadout has not loaded its unit: the game is asked for it, and the spawn waits */
    uint64_t name = 0;
    const char* no = FrvUnit(&name);
    if (no == NULL && FrvUnitLoaded(name) == 0)
    {
        if ((no = FrvAsk(seq)) == NULL) { g_frv.state = FRV_LOAD; g_frv.t0 = FrvNow(); g_frv.readyAt = 0; return; }
        Note("living world: test command %d: frv: no spawn at (%.0f, %.0f): %s", seq, (double)g_frv.place[0], (double)g_frv.place[1], no);
        return;
    }
    FrvBegin(); /* refuses, noted, whatever else is missing */
}
#endif

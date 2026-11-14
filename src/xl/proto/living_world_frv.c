/* Research build only: an FRV driven along a road by test commands, so one launch says whether the road can be driven.
 *   frv where arterial|spur [K]   the nearest arterial point (or spur K's arterial end) and how far the Helldiver is
 *   frv spawn                     an FRV 8 m ahead of the Helldiver along the camera's view; not driven
 *   frv drive arterial M          an FRV at the nearest arterial point, driven M metres along that road
 *   frv drive spur K              an FRV at spur K's arterial end (K -1: the nearest spur), driven to its place
 *   frv speed V                   the wanted speed of the next drive in m/s (default 8)
 *   frv stop                      ends the drive with its summary
 * What is read and written, checked in game.dll 25480438 (KB vehicle-replication-static-findings, world-update-dll-2b;
 * the sites re-read for this file):
 * - Spawn 0xFD9710(out id, entity hash, 0x58 bytes: +0 networked, +4 0x7FFF, +8 a 4x4 pose with the position in its
 *   last row, +0x48 / +0x50 null for the game's defaults), as HellForge's spawn_pelican calls it. Its construct
 *   0x581320 reads address 0 for a hash the definition table lacks, and the unit component's init spawns the unit
 *   without asking whether its resource is loaded, so both are asked here first.
 * - Drive: the vehicle manager's input record ([game+0x3326668]+0x58, 0x30 a vehicle: +0x18 accelerate, +0x1C brake,
 *   +0x20 steer, +0x2C has-input, +0x2D handbrake) is what 0xAAC300 copies to the motion component's controls each
 *   update for a vehicle of drive state 1; only a seated Helldiver's state writes it, so with no one seated the
 *   record written here before the game's update is the one read. The Helldiver is not seated: the seat calls skip
 *   the interaction's own start. It is kept near by the test teleport, since the ground is detailed near players only.
 * - Pose: the motion component ([game+0x3326458]) publishes its owned vehicles' position (+0x18), rotation (+0x24)
 *   and velocity across the ground (+0xC) into the 0x58-byte sync record every update (0x71615F..0x7161DD), and
 *   counts the wheels whose ray met something into its state's +0x65C (0x716610, 0x71673E). */
#ifdef CO_RESEARCH
#include "../co_frv.h"

#define FRV_ENTITY 0xCC21C7FFD3EBEFB9ull
#define FRV_UNIT_TYPE 0xE0A48D0BE9A7453Full /* the engine's resource type "unit" */
#define FRV_VEHICLES 0x3326668u
#define FRV_MOTION 0x3326458u
#define FRV_NEAR 150.0f     /* m: a leg starts this near the Helldiver, where the ground is detailed */
#define FRV_FOLLOW 35.0f    /* m: further behind than this, the Helldiver is put on the road behind the vehicle */
#define FRV_FOLLOW_BACK 14.0f
#define FRV_END 3.0f        /* m of the leg left when the drive counts as done */

enum { FRV_IDLE, FRV_SETTLE, FRV_DRIVE, FRV_BRAKE };
static struct
{
    int state, seq, flip, drove; uint32_t entity, segment; LONG mission;
    CoFrvPath path; CoFrvStats stats; CoFrvSign sign;
    float want, speedWant, lastSteer, lastHeading[2], settledFor, legMetres;
    LONGLONG t0, last, poseAt, followAt, brakeAt;
    char leg[96];
} g_frv = {.speedWant = 8.0f};

typedef struct FrvPose { float pos[3], q[4], vel[2], gameSpeed; int wheels, wheelCount, gear; uintptr_t input; } FrvPose;

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
        /* the fields: wheel contacts, position, rotation, has-input, accelerate, steer and brake, the drive state */
        ApiCodeMatches(g + 0x716610u, "43 C7 84 37 5C 06 00 00 00 00 00 00") && ApiCodeMatches(g + 0x71615Fu, "F2 42 0F 11 7C 38 18 42 89 5C 38 20") &&
        ApiCodeMatches(g + 0x71618Fu, "42 0F 11 74 38 24") && ApiCodeMatches(g + 0xAAC69Au, "80 79 2C 00") && ApiCodeMatches(g + 0xAAC6B4u, "F3 0F 10 79 18") &&
        ApiCodeMatches(g + 0xAAC6C0u, "F3 0F 10 41 20 F3 44 0F 10 49 1C F3 0F 11 03") && ApiCodeMatches(g + 0x6FEFD0u, "49 8B 4F 60 8B D3 44 8B 44 D1 04");
    if (!ok) Note("frv: the game's vehicle code is not the reviewed one: no FRV commands in this session");
    return ok;
}

/* The vehicle's records, or why not. The input record is given only for a vehicle this machine drives the wheeled
 * way (the authoritative prefix, drive state 1) whose motion record is owned, in use and not held by an event. */
static const char* FrvRead(uint32_t entity, FrvPose* p)
{
    uintptr_t vm = 0, mm = 0, inputs = 0, states = 0, recs = 0, sync = 0, st = 0; uint32_t i = 0, m = 0, drive = 0; uint8_t flags[3] = {0, 0, 0};
    memset(p, 0, sizeof *p); p->wheels = -1;
    if (!EvpAlive(entity)) return "the vehicle is gone";
    if (!EvpPtr(g_lw.game + FRV_VEHICLES, &vm) || !EvpHash(vm + 0x38, entity, 8, 4, &i) || i >= EvpU32(vm + 0x2C, 0)) return "the vehicle is not one this machine drives";
    if (!EvpPtr(vm + 0x60, &states) || SafeRead(states + 8ull * i + 4, &drive, 4) || drive != 1) return "the vehicle's drive state is not the wheeled one";
    if (!EvpPtr(g_lw.game + FRV_MOTION, &mm) || !EvpHash(mm + 0x40, entity, 8, 4, &m) || m >= EvpU32(mm + 0x34, 0)) return "the vehicle's motion is not simulated here";
    if (!EvpPtr(mm + 0x78, &sync) || SafeRead(sync + 0x58ull * m + 0x4D, flags, 3) || !flags[2]) return "the vehicle's motion record is not in use";
    if (flags[1]) return "an event holds the vehicle's handbrake";
    if (SafeRead(sync + 0x58ull * m + 0x18, p->pos, 12) || SafeRead(sync + 0x58ull * m + 0x24, p->q, 16) || SafeRead(sync + 0x58ull * m + 0xC, p->vel, 8)) return "the vehicle's pose does not read";
    for (int k = 0; k < 3; k++) if (!isfinite(p->pos[k])) return "the vehicle's pose is not a number";
    if (EvpPtr(mm + 0x70, &st)) { p->wheels = (int)EvpU32(st + 0x670ull * m + 0x65C, 0); p->gear = (int)EvpU32(st + 0x670ull * m + 0x618, 0); }
    if (EvpPtr(vm + 0x68, &recs))
    {
        float kmh = 0.0f;
        p->wheelCount = (int)EvpU32(recs + 0xD28ull * i + 8, 0);
        if (!SafeRead(recs + 0xD28ull * i + 0xC24, &kmh, 4)) p->gameSpeed = kmh / 3.6f; /* the engine's speed x 3.6 (0x716E90 divides it back) */
    }
    if (!EvpPtr(vm + 0x58, &inputs)) return "the vehicle has no input record";
    p->input = inputs + 0x30ull * i;
    return NULL;
}

/* The input record as a seated driver's state leaves it: read whole first, so only a record that reads is written. */
static void FrvWrite(uintptr_t input, float accelerate, float brake, float steer, int handbrake, int has)
{
    uint8_t r[0x30];
    if (input == 0 || SafeRead(input, r, sizeof r)) return;
    memcpy(r + 0x18, &accelerate, 4); memcpy(r + 0x1C, &brake, 4); memcpy(r + 0x20, &steer, 4);
    r[0x2C] = (uint8_t)has; r[0x2D] = (uint8_t)handbrake; r[0x2E] = r[0x2F] = 0;
    memcpy((void*)(input + 0x18), r + 0x18, 0x18);
}

/* Whether the FRV can be made here: its definition (a miss reads address 0 in 0x581320) and its unit's resource. */
static const char* FrvLoaded(void)
{
    uintptr_t em = 0, defs = 0, api = 0, res = 0, slot = 0; uint64_t name = 0;
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
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
    if (data == 0 || SafeRead(data, &name, 8) || name == 0) return "the FRV has no unit data";
    /* the game's own table must lead to the function pinned above */
    if (!EvpPtr(g_lw.game + 0x3326308u, &api) || !EvpPtr(api + 0x10, &res) || !EvpPtr(res + 0x388, &slot) || slot != exe + 0x321930u) return "the resource API is not the reviewed one";
    if (!((uint8_t (*)(uint64_t, uint64_t))slot)(FRV_UNIT_TYPE, name))
        return "the FRV's unit is not loaded in this mission (no player brought an FRV stratagem): spawning it would crash the game";
    return NULL;
}

/* An FRV on the ground at (x, y), facing (fx, fy): its entity, 0 refused (noted). */
static uint32_t FrvSpawn(int seq, float x, float y, float fx, float fy)
{
    int host = 0; float z = 0.0f; uintptr_t vm = 0;
    const float l = sqrtf(fx * fx + fy * fy);
    const char* why = FrvLoaded();
    if (!ActMission(g_lw.game, &host, NULL) || !host) why = "only the host, in a mission";
    else if (!(l > 1.0e-3f) || !isfinite(x) || !isfinite(y)) why = "no place or facing";
    else if (!LwGround(x, y, &z)) why = "no ground there";
    else if (!EvpPtr(g_lw.game + FRV_VEHICLES, &vm) || EvpU32(vm + 0x24, 64) >= 48) why = "the vehicle manager is nearly full";
    if (why != NULL) { Note("living world: test command %d: frv: no spawn at (%.0f, %.0f): %s", seq, (double)x, (double)y, why); return 0; }
    fx /= l; fy /= l;
    _Alignas(8) uint8_t d[0x58];
    memset(d, 0, sizeof d);
    d[0] = 1;
    const uint32_t none = 0x7FFF; memcpy(d + 4, &none, 4);
    /* rows: the unit's x, y and z axes, then the position; y is taken as the nose (the pose line says how far off) */
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

static void FrvEnd(const FrvPose* p, const char* ended, int drove)
{
    char line[640];
    if (p != NULL && p->input) FrvWrite(p->input, 0.0f, 1.0f, 0.0f, 1, 1);
    co_frv_summary_text(line, sizeof line, &g_frv.stats, g_frv.leg, g_frv.legMetres, ended, drove);
    Note("%s", line);
    g_frv.state = p != NULL && p->input ? FRV_BRAKE : FRV_IDLE; g_frv.brakeAt = FrvNow(); g_frv.drove = drove;
}

/* Every game update, before the game's own: the drive's step. */
static void FrvTick(void)
{
    if (g_frv.state == FRV_IDLE) return;
    FrvPose p; const LONGLONG now = FrvNow();
    const char* gone = g_frv.mission != g_lwMission || !LwLive() ? "the level left play" : FrvRead(g_frv.entity, &p);
    if (g_frv.state == FRV_BRAKE)
    {
        /* the brake is held for two seconds, then the record is given back as an empty vehicle's */
        if (gone != NULL || FrvSeconds(g_frv.brakeAt, now) > 2.0) { if (gone == NULL) FrvWrite(p.input, 0.0f, 0.0f, 0.0f, 0, 0); g_frv.state = FRV_IDLE; }
        else FrvWrite(p.input, 0.0f, 1.0f, 0.0f, 1, 1);
        return;
    }
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
        Note("frv: FRV %08X settled after %.1f s at (%.1f, %.1f, %.1f): %d of %d wheels on the ground, pitch %+.1f roll %+.1f deg, gear %d%s", g_frv.entity, (double)t,
            (double)p.pos[0], (double)p.pos[1], (double)p.pos[2], p.wheels, p.wheelCount, (double)pitch, (double)roll, p.gear, g_frv.settledFor < 0.5f ? " (not at rest: driven anyway)" : "");
        if (g_frv.path.count < 2) { g_frv.state = FRV_IDLE; return; } /* "frv spawn": not driven */
        g_frv.state = FRV_DRIVE; g_frv.t0 = g_frv.poseAt = g_frv.followAt = now; g_frv.segment = 0;
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
    FrvWrite(p.input, throttle, 0.0f, steer, 0, 1);
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
        CoFrvAim back; uint32_t seg = 0; const float none[2] = {1, 0};
        g_frv.followAt = now;
        co_frv_aim(&g_frv.path, &seg, g_frv.path.xy[0], none, aim.along - FRV_FOLLOW_BACK, &back);
        g_lwTeleportForce = g_lwTeleportQuiet = 1;
        LwTestTeleport(g_frv.seq, back.target[0], back.target[1]);
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
        FrvPose p; const char* gone = FrvRead(g_frv.entity, &p);
        if (g_frv.state == FRV_DRIVE) FrvEnd(gone == NULL ? &p : NULL, "stopped by command", 0);
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
        g_frv.entity = FrvSpawn(seq, s[0], s[1], g_frv.path.xy[1][0] - s[0], g_frv.path.xy[1][1] - s[1]);
    }
    else if (!strcmp(cmd, "frv spawn"))
    {
        float f[2] = {1, 0};
        LwCameraForward(f);
        g_frv.entity = FrvSpawn(seq, pl[0] + 8.0f * f[0], pl[1] + 8.0f * f[1], f[0], f[1]);
    }
    else { Note("living world: test command %d: frv: not a command (where, spawn, drive, speed, stop)", seq); return; }
    if (!g_frv.entity) return;
    co_frv_stats_start(&g_frv.stats);
    g_frv.state = FRV_SETTLE; g_frv.seq = seq; g_frv.mission = g_lwMission; g_frv.t0 = g_frv.last = FrvNow(); g_frv.settledFor = 0.0f; g_frv.want = g_frv.speedWant;
    g_frv.lastHeading[0] = 1.0f; g_frv.lastHeading[1] = 0.0f;
}
#endif

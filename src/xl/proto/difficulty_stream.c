/* Extra large maps' content streaming, v1: the world of difficulty_world.c grows, but the map's content is spawned near
 * the players while they play instead of all at generation.
 *
 * The generator's SpawnLevelLayers (op 0x6C, game.dll + 0xF46940) walks the location records (0xE0 bytes each) from
 * *(uint8_t**)(pass + 0x08), count **(uint32_t**)(pass + 0x48), starting at the resume index *(uint32_t*)(pass + 0xD8).
 * A location whose stamp unit at +0xA8 is non-null has its level layers queued on the engine world's asynchronous spawn
 * queue; a location at +0xA8 == 0 is skipped by the original. SpawnStamps (op 0x31) already stored each location's stamp
 * unit there.
 *
 * This file hooks SpawnLevelLayers. On the generator's first call for an extra large level (g_world.current > 1.0f) it
 * copies each location's stamp unit into its own array and zeroes +0xA8 for every location outside the start area (the
 * squad's drop point on the war board, or the map centre when none reads), so the generator loads only that area and
 * skips the rest. When a generator call returns with the resume index at the count (the op is done) the units are put
 * back. The game thread then streams a deferred location when a player's Helldiver comes within 500 m: it copies up to
 * 16 location records into a staging array, points the captured pass at that array (resume 0, count 16), calls the
 * original, restores the pass fields and copies the records back. Locations before the returned resume index are done;
 * the rest stay due for the next frame (the engine's queue was full).
 *
 * Claude's in-game test of 2026-10-30 made the same call with one location record and the content appeared within a
 * frame or two, physics bodies included. It only works with difficulty_world.c's kept layer heightfield (WorldLayerSnap):
 * the generator frees its heightfield after generation and a layer spawned later would crash without the kept copy.
 *
 * The pin below is SpawnLevelLayers' first 23 bytes, checked against .deepcode/xl2/spawn_level_layers.asm (the task's
 * pin had 48 8D AB there; the reviewed bytes are 49 8D AB: lea rbp, [r11 - 0x1c8] needs REX.WB):
 *   0xF46940: 4C 8B DC 55 53 41 56 41 57 49 8D AB 38 FE FF FF 48 81 EC A8 02 00 00
 * A streamed location whose nearest player is farther than kStreamOutRadius (800 m) for 5 s is removed: DestroyUnit
 * (the world interface's +0x388 entry) destroys its stamp unit, the real record's +0xA8 goes to 0 and the location waits
 * for a new unit. When it becomes due again SpawnUnit (+0x380) makes a new stamp unit from the resource and pose saved at
 * capture, SpawnStamps' set-data call puts the location index on it, and its layers stream as before. Only locations this
 * streamer spawned are removed; the start area's content and the generator's own spawns stay. The layer units go, but the
 * game entities some layers queue stay (logged at the 20th removal; a later task).
 *
 * The mission's own locations (every important location's stamp: objectives, extraction, outposts, points of interest,
 * satellites) are never deferred: the game links their entities by the stamp unit and registers objectives and
 * garrisons at level setup (WorldStreamMissionLocked). Only the rest (stamps, formations) streams. */

#ifndef HF_LUA_API
#define HF_LUA_API __declspec(dllexport)
#endif

#define DIF_STREAM_SPAWN 0xF46940u /* uint8_t SpawnLevelLayers(uint8_t* pass, uint8_t* record, uint8_t* context) */
static const char kStreamSpawnPin[] = "4C 8B DC 55 53 41 56 41 57 49 8D AB 38 FE FF FF 48 81 EC A8 02 00 00";
/* The galactic war board's drop point (Claude's in-game test, 2026-10-30; asked for 800,0, landed at 789,37): the board
 * object at [game + 0x347CEE8], a u8 at +0x17A2B0 = 1 when a drop point is set, and its world-space metres at
 * +0x17A2B4 (x) and +0x17A2B8 (y). */
#define DIF_STREAM_BOARD 0x347CEE8u
#define DIF_STREAM_BOARD_FLAG 0x17A2B0u
#define DIF_STREAM_BOARD_X 0x17A2B4u
#define DIF_STREAM_BOARD_Y 0x17A2B8u
/* The world interface ([game + 0x3326308]): the table at +8 holds SpawnUnit (+0x380) and DestroyUnit (+0x388); the
 * object at +0xB8 has the method at its +0x10 object's +8 (SpawnStamps calls it after spawning to store the location
 * index on the unit). A stamp unit's pose is its 4x4 float matrix at +0x20; a location's resource is [[loc+0x90]+8]. */
#define DIF_STREAM_INTERFACE 0x3326308u
#define DIF_STREAM_TABLE 0x8u
#define DIF_STREAM_SPAWN_ENTRY 0x380u
#define DIF_STREAM_DESTROY_ENTRY 0x388u
/* The table's +0x410 (exe 0x3340A0: mov rcx,[rcx+0x230050]; jmp 0x556040) waits for the world's asynchronous animation
 * update. The animation state machine manager (world+0x230050, built by 0x554870) starts its async jobs (0x555BB0, kicked
 * from game 0x127A38D through the table's +0x400) over a copy of its list (+0xE0) and does not wait; every game destroy
 * waits first (+0x410 before +0x388 at game 0x664A2B, 0x6652AE, 0xAB3A31, 0xAC04DE, 0x1304659), and the frame's end waits
 * at 0xAB61AD. A unit destroyed between the kick and that wait leaves its state machine in the jobs' copy: the update
 * 0x5530C0 reads [[asm]+0x30] of the freed unit (run s75q: exe+0x55310E, reading -1; static, subagent crash55310e).
 * The streamer's tick also runs from the late update (0x13270B0 called at 0x13F8938, after the kick), so every
 * removal waits first; the wait returns at once when no job is in flight (+0xF0 == 0). */
#define DIF_STREAM_ANIM_WAIT_ENTRY 0x410u
static const char kStreamAnimWaitPin[] = "48 8B 89 50 00 23 00 E9";
typedef void (*WorldStreamAnimWaitFn)(uint64_t world);
/* The wait entry from the world interface's table when it is the reviewed one (exe 0x3340A0's bytes), else 0. */
static uintptr_t WorldStreamAnimWait(uintptr_t table)
{
    uintptr_t wait = 0, exe = ApiModuleBase("helldivers2.exe", NULL);
    if (table == 0 || exe == 0 || SafeRead(table + DIF_STREAM_ANIM_WAIT_ENTRY, &wait, 8) || wait != exe + 0x3340A0u ||
        ApiCodeMatches(wait, kStreamAnimWaitPin) != 1) return 0;
    return wait;
}
#define DIF_STREAM_SETDATA_ENTRY 0xB8u
#define DIF_STREAM_SETDATA_OBJECT 0x10u
#define DIF_STREAM_SETDATA_METHOD 0x8u
#define DIF_STREAM_UNIT_POSE 0x20u
#define DIF_STREAM_LOC_RESOURCE 0x90u

enum
{
    kStreamMaxLocations = 65536, /* the location records this streamer holds; above that nothing is deferred */
    kStreamBatch = 16,           /* location records per SpawnLevelLayers call */
    kStreamRecord = 0xE0,        /* one location record */
    kStreamScanChunk = 64,       /* location records read per SafeRead while deciding */
    kStreamOutTimeout = 5000,    /* ms beyond kStreamOutRadius before a streamed location is removed */
    kStreamReplaceBatch = 32,    /* stamps put back on new ground per pass at most (a tile's change flags hundreds) */
    kStreamDropQueue = 256,      /* removals decided and not yet done */
    kStreamDropReserve = 96,     /* queue room the out-of-reach removals leave to put-backs and far evictions (32 + 64) */
    /* Any static body added or removed makes the next physics step rebuild the whole static broad phase layer (exe
     * 0xD3DAA9), so the streamer's adds and removals go together into frames at least kStreamChangeGap ms apart. */
    kStreamChangeGap = 250,
    kStreamChangeDrops = 64,     /* stamp units destroyed in one change frame at most */
    /* With a backlog, a change frame every second frame instead of each kStreamChangeGap ms: each frame stays in its
     * budget under the frame target, so throughput comes from how often they come, not from how much each takes. */
    kStreamChangeTicksBusy = 2,
    kStreamChangeDropsBusy = 128,
    /* Locations a change frame spawns at most: their units are made in the same frame (the ring's flush), about 30
     * bodies each. The budget holds a frame first; the count bounds one before the making is estimated. */
    kStreamChangeSpawns = 16,
    kStreamChangeSpawnsBusy = 24,
    /* Locations a change frame spawns whatever its budget and the making's estimate say, so a frame without room, or
     * an estimate a burst of the game's own spawns raised, still moves the queue on. */
    kStreamSpawnFloor = 2,
    /* Removals a change frame at its budget's floor makes whatever the time: the stamps that left reach in the last
     * scan's interval a change frame, at least the spawn floor and at most kStreamDrainFloorMost so one floor frame
     * stays short. Removals keep pace with exits, and the window stays near its design (WorldStreamDrainLeast). */
    kStreamDrainFloor = 2,
    kStreamDrainFloorMost = 8,
    kStreamBacklogDue = 64       /* a backlog: more due stamps not spawned than this, or a queue or expired count past a frame's */
};
/* A change frame's time for its removals, spawns and their making (WorldStreamBudget): the frame target less the
 * recent quiet frames' longest (CO_FC_RECENT), its layer's rebuild and this frame's tile step, within these;
 * kStreamChangeMs until the quiet frames are measured. */
static const double kStreamChangeMs = 3.0;
static const double kStreamBudgetFloor = 0.5;
static const double kStreamBudgetMost = 6.0;
/* A change frame's world update over the quiet frames', until measured: a frame that removed stamps 3.0 ms (run 1 and
 * run 2a: 2.4-3.1 ms whatever the count, ANALYSIS-r2a §2.3), one that only spawned about 0 (0.5 held: a rebuild of
 * 100-230k leaves cost nothing measurable, §2.2). They replace the per-leaf rebuild estimate. */
static const double kStreamRemoveMsStart = 3.0;
static const double kStreamSpawnMsStart = 0.5;
/* Removals go in frames of their own, gathered: their step cost is mostly per frame, so one frame of 8 costs about what
 * one of 1 does. A removal frame comes when its frame before left room under the target for the learned step cost and
 * kStreamRemoveGather removals' own time (the drain's, learned per removal from kStreamRemoveOwnMsStart: run 2c 0.31),
 * with kStreamRemoveGather queued or the oldest waiting kStreamRemoveGatherMs; whatever the frames, once the oldest has
 * waited kStreamRemoveWaitMs, or kStreamRemoveWaitBusyMs while the backlog or the exits' pace is past one forced
 * frame's most (WorldStreamDrainLeast): forced frames then remove up to 240 a second (run 2c needed 60-154). */
static const double kStreamRemoveOwnMsStart = 0.3;
static const double kStreamRemoveForcedOwnMs = 8.0; /* a forced frame's own time at most, past its kStreamRemoveGather */
enum { kStreamRemoveGather = 8, kStreamRemoveGatherMs = 250, kStreamRemoveWaitMs = 500, kStreamRemoveWaitBusyMs = 100, kStreamRemoveForcedMost = 24 };
/* No change frame while a far-grass bake job is out (its frames ran 21 ms against 13, run 2a), at most this long: a
 * view's job is out about 3 frames (195 job frames for 64 views), so this fires only for a job that stalls. */
enum { kStreamGrassHoldMs = 500 };
/* The governor: while the quiet frames' world update (their own time, kGovWindowMs means) is over kGovOnMs, the far
 * ring takes no new stamps and its farthest go, never one within kStreamOutRadius of a player; off under kGovOffMs.
 * Its turns are only counted unless a research build asks it to act (g_streamGovActs): run 2c's 6.4 and 10.4 ms were
 * frames right after change frames, its quiet ones 3.9-4.0 ms at 484-497k bodies (adv REPORT-3c1ad6b BL1). */
static const double kGovOnMs = 7.0;
static const double kGovOffMs = 6.0;
/* A turn at most each kGovHoldMs: an on/off cycle takes 6 s at least, whatever the far ring's coming back does to the
 * mean, and the release after a recovery comes within kGovHoldMs + kGovWindowMs of the turn on. */
enum { kGovWindowMs = 1000, kGovMinFrames = 10, kGovHoldMs = 3000 };
static const float kStreamStartRadius = 600.0f; /* kept at generation: inside this of the start point */
static const float kStreamInRadius = 500.0f;    /* streamed: a player closer than this makes a location due */
static const float kStreamOutRadius = 800.0f;   /* removed: every player farther than this for kStreamOutTimeout */
static const float kStreamFar = 3.402823466e38f; /* no player: beyond any distance in a map */

/* The start area's centre, in world space (map position - half). WorldStreamCaptureLocked sets it from the war board's
 * drop point when that reads, else to the map centre (0, 0). */
static float g_streamStart[2] = {0.0f, 0.0f};

/* Research: the Lua export's extra player position. WorldStreamPlayersLocked counts it as one more player, and it is
 * set and cleared under g_streamLock. */
static int g_streamObserverOn;
static float g_streamObserverAt[2] = {0.0f, 0.0f};

typedef uint8_t (*WorldStreamSpawnFn)(uint8_t* pass, uint8_t* record, uint8_t* context);
static WorldStreamSpawnFn g_streamSpawn;
/* The memory probe's view of one generator call (difficulty_world_memprobe.c). */
typedef struct { uint32_t resume; LONG64 commit, da; uint32_t handles; LARGE_INTEGER t0; uintptr_t world; } MemProbeSpawnCall;
static int MemProbeSpawnBefore(MemProbeSpawnCall* c, uint8_t* pass, uint8_t* context);
static void MemProbeSpawnAfter(const MemProbeSpawnCall* c, uint8_t* pass);
/* The engine calls SpawnStamps makes for a stamp unit: SpawnUnit (the world interface's +0x380) and the set-data call
 * after it (the +0xB8 object's +0x10 object, method at +8), whose 24-byte argument is {&location index, 2, 4, 0}.
 * DestroyUnit is the table's +0x388 entry. */
typedef uint64_t (*WorldStreamSpawnUnitFn)(uint64_t world, uint64_t resource, const float* pose, uint64_t zero, int zero2);
typedef void (*WorldStreamDestroyUnitFn)(uint64_t world, uint64_t unit, uint32_t flags, uint8_t extra);
typedef void (*WorldStreamSetDataFn)(uint64_t unit, void* data, uint64_t one, uint64_t hash);
typedef struct WorldStreamSetData
{
    uint32_t* index;
    uint32_t two, four;
    uint64_t zero;
} WorldStreamSetData;

/* The captured generator call and what was decided for its locations. All of it is read and written on one side of
 * g_streamLock: the generator thread captures and restores, the game thread streams. */
typedef struct WorldStreamState
{
    uintptr_t game;          /* game.dll */
    uintptr_t level;         /* g_worldLevel the state belongs to */
    LONG levels;             /* g_world.levels then: the next level can get the same address, and is not this one */
    uint8_t* pass;           /* the generator's SpawnLevelLayers arguments (valid for the mission) */
    uint8_t* record;
    uint8_t* context;
    uint8_t* array;          /* *(pass + 0x08): the location records */
    uintptr_t countAt;       /* *(pass + 0x48): the count */
    float half;              /* g_world.values->half when captured (map space -> world space) */
    uint32_t count, kept, deferred, bad;  /* locations, kept at the start, deferred, unreadable at capture */
    uint32_t bodiesUsed, bodySlots;       /* the mission world's physics bodies at the last scan, and its store */
    uint32_t spawned, broken, removed, respawned; /* streamed now, records skipped, removals and respawns (cumulative) */
    uint32_t replaced;       /* removals because a tile changed the ground under them (cumulative) */
    uint32_t respawnHeld;    /* spawn passes that held a level location's new unit over the window's budgets (cumulative) */
    uint32_t orderCount, orderCursor;     /* the due locations, nearest first */
    int captured, failed, generatorDone, played;  /* state, the capture was refused, the generator's op is at its count */
    int playerCount;
    int resumeBad, passBad, recordBad, heightBad, destroyBad, respawnBad; /* one note per failure */
    uint32_t bodyBefore;                  /* the physics bodies before the first removal (bodyBeforeSeen) */
    int bodyBeforeSeen, bodyPending, bodyNoted; /* before count read; after count due; the 20-removal line is done */
    ULONGLONG nextScan, nextNote;
    double spentMs;          /* time in the streamer's own SpawnLevelLayers calls */
    uintptr_t forest;        /* the SpeedTree forest the stamps' trees go to (co_forest.h), from a stamp unit's world */
    uint32_t treesMost;      /* the most trees one of the streamer's calls added */
    uint32_t treeHolds;      /* the streamer's calls held back: the forest's grid had no room for one more (cumulative) */
    int forestBad;           /* one note when the forest does not read */
    uint32_t gone;           /* slots given up: their unit was no longer this world's at its removal (cumulative) */
    uint32_t looks, listed;  /* the world's unit list searched before a removal, and its length at the last */
    uint32_t lookBad, lookFails; /* the looks that did not read: in a row now, and in all */
    double lookMs, lookMost; /* those searches' time in all, and the longest */
    ULONGLONG nextChange;    /* the next frame the streamer may add or remove bodies in (kStreamChangeGap) */
    uint32_t nextChangeTick; /* ... and its g_streamTicks with a backlog (kStreamChangeTicksBusy) */
    int pickTurn;            /* SlPick's turn between an overfull layer's removals and the smallest one's spawns */
    double budgetSum;        /* since the note: the change frames' budgets */
    uint32_t budgetFloors, buildFrames; /* since the note: budgets at their floor, frames a tile was building in */
    uint32_t floorHeld;      /* since the note: floor frames that spawned near stamps only (WorldStreamWindowHolds) */
    uint32_t floorDry;       /* since the note: floor frames with stamps due that spawned none, whatever the cause */
    uint32_t floorStuck;     /* since the note: floor frames the quiet frames did not cause (WorldStreamFloorStuck) */
    double rebuiltSum;       /* since the note: the change frames' rebuilt layer's leaves, summed */
    uint32_t exitsNew, exitsChanges, changesSinceScan; /* the last scan's new exits, the change frames before it */
    ULONGLONG scanAt;        /* the last scan's time: a stamp whose out timer ran out after it is a new exit */
    uint32_t floorDue, floorSpawned; /* since the note: floor frames with a due stamp in reach and no layer fill, their locations */
    uint32_t drainLeast;     /* the last floor frame's removal minimum */
    uint32_t spawnTried;     /* the last change frame's spawn layer + 1 (1: layer 4), 0 when it tried none */
    float spawnWithin;       /* the spawn pass's squared reach: due stamps past it wait (0: all) */
    double quietHiSum;       /* since the note: the change frames' quiet frames' longest, summed */
    uint32_t streamedAt, streamedLeaves, streamedMoved, streamedQuiet; /* layers 6..9 as layer 4's static* above */
    uint32_t changes;        /* frames it did since the last note */
    uint32_t expired, expiredMost; /* live stamps past their removal radius and timer at the last scan, the most since the note */
    uint32_t staticAt, staticLeaves; /* g_streamTicks and the leaf count at the last layer 4 read */
    uint32_t staticFrames, staticMoved, staticQuiet; /* since the note: reads, leaf counts changed, changed after no streamer change */
    uint32_t staticTile; /* changed while a tile builds: its frames or the next (MoveBuildRecent) */
    int changedLast;         /* the last tick's change step added or removed bodies */
    int drainCut, spawnCut;  /* the last drain or spawn pass stopped at its time limit with work left */
    uint32_t spawnLayer;     /* the broad phase layer the change frame's spawns go to (6..9), 0: none of its own */
    uint32_t backlogs, drainCuts, spawnCuts, changeRemoved; /* since the note: change frames with a backlog, cut short, removals */
    double drainMs, spawnMs, drainMost, spawnMost; /* since the note: change frames' removal and spawn time, and the longest */
    uint32_t layerDrains[5], layerSpawns[5]; /* since the note: change frames that drained, spawned into layer 4, 6..9 */
    int spawnCapped;         /* the last spawn pass stopped at its location count with due ones left */
    uint32_t spawnCaps, tileDefers, spawnedIn; /* since the note: passes capped, frames deferred for a tile step, locations */
    uint32_t ringFull;          /* since the layers' note: SpawnLevelLayers calls stopped by a full spawn ring (0xF46A76) */
} WorldStreamState;
/* The change frame's flush of the spawn ring, run after g_streamLock is released (WorldStreamChangeAfter), and its
 * cost. A flush also makes the game's own ring entries, which the world's flush (0x335330, game.dll 0xAB61C5) would
 * make later in the same frame anyway: so slope, a location's making, is fitted apart from the fixed part. */
static struct
{
    int pending; uint64_t world; uintptr_t wait; uint32_t locations;
    uint32_t frames; double ms, most, waitMs, waitMost; /* since the note; the anim wait before each flush too */
    double sw, sn, sm, snn, snm; /* decaying sums of the flushes' weights, locations, ms and their products */
    double slope, fixed;
} g_streamFlush;
/* A layer rebuild's cost per leaf, learned from each change frame's world update over the quiet frames' (ns), and the
 * leaves of the layer the last change frame rebuilt (0: none to learn from). Kept over levels: the machine's. */
static struct { double ns; uint32_t leaves; } g_streamRebuild = {0.0, 0}; /* the leaves of the last change frame's layer */
/* A change frame's world update and its after-change frame's over two quiet frames' by kind, learned from each pair
 * (ms; kept over levels: the machine's), a removal's own drain time, and the kind of the last change frame not yet
 * learned from (1 removal, 2 spawn; 0 none). */
#define SL_LAYERS 5u /* the static layers: layer 4, then the streamed 6..9 (difficulty_stream_layers.c) */
/* Each 10 s by kind (removal, spawn) and the layer the change marked (4, then 6..9): the clean pairs' cost (a change
 * frame's and its after-change frame's world update over two quiet frames') and the leaves its step rebuilt, so a run
 * shows that a change pays for its own layer's leaves alone. */
typedef struct { uint32_t n; double ms, most, leaves; } LayerCostSum;
static LayerCostSum g_streamLayerCost[2][SL_LAYERS];
static struct { double removeMs, spawnMs, ownMs; int kind; uint32_t layer, leaves; } g_streamCost = {kStreamRemoveMsStart, kStreamSpawnMsStart, kStreamRemoveOwnMsStart, 0, 0, 0};
/* Removal frames, far-grass holds and the governor: their state for the level, and the counts since the note. */
static struct
{
    ULONGLONG removeWaitAt;  /* the oldest queued removal has waited since (0: none queued) */
    ULONGLONG grassSince;    /* change tries held for a far-grass job since (0: none held) */
    int lastKind;            /* the last change frame: 1 removal, 2 spawn */
    uint32_t removeFrames, removeForced, spawnFrames, grassHolds, grassForced; /* since the note */
    double removeSum, spawnSum; uint32_t removeSamples, spawnSamples; /* since the note: learned samples */
    double quietHiGrassSum;  /* since the note: the change frames' quiet frames' longest with the far-grass frames in */
    double ownSum; uint32_t ownSamples; /* since the note: learned per-removal own time samples */
    double exitRate;         /* the scans' new exits a second, a quarter of the way each scan */
    uint32_t exitsSum, noteExpired, noteQueued; /* since the note: new exits; at the last note: live past radius, queued */
    uint32_t farNearSkipped; /* since the note: the governor's removals not done at the drain, a player within kStreamOutRadius */
    uint32_t farNearRemoved[2]; /* since the note: far removals done with a player within kStreamOutRadius: the window's, the governor's */
    /* the governor */
    int govOn; ULONGLONG govAt, govTurnAt; double govSum; uint32_t govN; double govLast; /* govTurnAt: the last turn (0: none) */
    uint32_t govOnTurns, govOffTurns, govSeconds, govHeldScans, govHeldRespawns, govDrained, govNearKept; /* since the note */
} g_streamPace;
/* A research switch (HELLFORGE_XL_GOVERNOR=1, read at install): the governor holds and evicts the far ring; else it only
 * counts the turns it would make. Not a level's state. */
static int g_streamGovActs;
static WorldStreamState g_stream;
/* The engine calls made with a stamp unit's address this session, and what the last level to leave play left: the
 * line some frames after it says how many destroys came since (none can). Not a level's state: kept over the clear. */
static LONG g_streamDestroys;
static struct { int frames; LONG destroys, levels; } g_streamLeft;
enum { kStreamLeftFrames = 120 }; /* frames after a level left play until that line */

static uint64_t g_streamUnit[kStreamMaxLocations];       /* each location's real stamp unit */
static float g_streamAt[kStreamMaxLocations][2];         /* its world-space x, y */
static uint8_t g_streamDeferred[kStreamMaxLocations];    /* 1: its +0xA8 was zeroed for the generator */
static uint8_t g_streamDue[kStreamMaxLocations];         /* 1: a player has been within kStreamInRadius (sticky) */
static uint8_t g_streamSpawned[kStreamMaxLocations];     /* 1: queued, or skipped because its record did not read */
static float g_streamDistance[kStreamMaxLocations];      /* its squared distance to the nearest player, for the order */
static uint32_t g_streamOrder[kStreamMaxLocations];      /* the due locations, nearest first */
static uint64_t g_streamChunk[(kStreamScanChunk * kStreamRecord) / 8];   /* records read while deciding */
static uint64_t g_streamStage[(kStreamBatch * kStreamRecord) / 8];       /* records of one streamed call */
static uint64_t g_streamPlayerPeer[EVT_MAX_PLAYERS + 1]; /* whose place each is: the tree cutters' centres are these same places */
static float g_streamPlayerAt[EVT_MAX_PLAYERS + 1][2];   /* the players' Helldivers' x, y; the last slot can be the observer */
static uint64_t g_streamResource[kStreamMaxLocations];   /* [[loc+0x90]+8] at capture */
static float g_streamPose[kStreamMaxLocations][16];      /* its stamp unit's pose (unit+0x20) at capture */
static uint8_t g_streamPoseGood[kStreamMaxLocations];    /* 1: resource and pose saved, so it may be removed */
static uint8_t g_streamLive[kStreamMaxLocations];        /* 1: the streamer queued its layers and they are live */
static uint8_t g_streamDetached[kStreamMaxLocations];    /* 1: the real record's +0xA8 stays 0 (a respawned unit) */
static uint8_t g_streamNeedsUnit[kStreamMaxLocations];   /* 1: removed; a new stamp unit is needed before layers */
static uint8_t g_streamFormation[kStreamMaxLocations];   /* 1: a large formation of the outer stamp rings: streamed in
                                                            under the physics budget wherever the players are, kept */
static ULONGLONG g_streamOutSince[kStreamMaxLocations];  /* tick it first went beyond kStreamOutRadius (0: not out) */
static uint8_t g_streamMission[kStreamMaxLocations];     /* 1: an important location's stamp (WorldStreamMissionLocked) */
static float g_streamOut2[kStreamMaxLocations];          /* > 0: this slot's own removal radius, squared (a far window stamp) */
static uint8_t g_streamReplace[kStreamMaxLocations];     /* 1: the ground under it changed (a tile came or went): out and in again */
static float g_streamSeen2[kStreamMaxLocations];         /* a flagged one's distance its meshes are drawn to, squared (WindowVisible) */
/* Removals the scans decide, done a few a frame: a scan after a long move or a tile change asked for 64 DestroyUnit
 * calls and a far eviction for 64 more, each taking its stamp's layer units down in that one frame. */
/* STREAM_DROP_FAR: the window's far eviction over the near budget (from 550 m: it makes room near the players);
 * STREAM_DROP_GOV: the governor's (past 800 m only, asked again at the drain). Both are far removals otherwise. */
enum { STREAM_DROP_OUT = 1, STREAM_DROP_REPLACE, STREAM_DROP_FAR, STREAM_DROP_GOV };
#define STREAM_DROP_IS_FAR(kind) ((kind) == STREAM_DROP_FAR || (kind) == STREAM_DROP_GOV)
static uint8_t g_streamDrop[kStreamMaxLocations];        /* the queued removal's kind, 0: none */
static uint8_t g_streamLayer[kStreamMaxLocations];       /* the broad phase layer its bodies went to (6..9), 0: layer 4 */
static uint32_t g_streamDropList[kStreamDropQueue], g_streamDropHead, g_streamDropCount, g_streamDropFar;

static SRWLOCK g_streamLock = SRWLOCK_INIT;
static volatile LONG g_streamOwn;     /* the streamer's own call: the thread id inside it (0: none) */
static volatile LONG g_streamInside;  /* a generator call is inside (capture, the original, restore): no streaming then */
static int g_streamInstallState;      /* 0 not tried, 1 installed, -1 refused */
static LONGLONG g_streamFrequency;    /* QueryPerformanceFrequency */

/* Back to no captured level. The arrays are cleared too: a capture fills only the indices below its count. */
static void WorldStreamClearLocked(void)
{
    g_stream.level = 0;
    g_stream.pass = NULL;
    g_stream.record = NULL;
    g_stream.context = NULL;
    g_stream.array = NULL;
    g_stream.countAt = 0;
    g_stream.half = 0.0f;
    g_stream.count = 0;
    g_stream.kept = 0;
    g_stream.deferred = 0;
    g_stream.bad = 0;
    g_stream.spawned = 0;
    g_stream.broken = 0;
    g_stream.removed = 0;
    g_stream.respawned = 0;
    g_stream.replaced = 0;
    g_stream.respawnHeld = 0;
    g_stream.orderCount = 0;
    g_stream.orderCursor = 0;
    g_stream.captured = 0;
    g_stream.played = 0;
    g_stream.failed = 0;
    g_stream.generatorDone = 0;
    g_stream.playerCount = 0;
    g_stream.resumeBad = 0;
    g_stream.passBad = 0;
    g_stream.recordBad = 0;
    g_stream.heightBad = 0;
    g_stream.destroyBad = 0;
    g_stream.respawnBad = 0;
    g_stream.bodyBefore = 0;
    g_stream.bodyBeforeSeen = 0;
    g_stream.bodyPending = 0;
    g_stream.bodyNoted = 0;
    g_stream.nextScan = 0;
    g_stream.nextNote = 0;
    g_stream.spentMs = 0.0;
    g_stream.forest = 0;
    g_stream.treesMost = 0;
    g_stream.treeHolds = 0;
    g_stream.forestBad = 0;
    g_stream.gone = 0;
    g_stream.looks = g_stream.listed = 0;
    g_stream.lookBad = g_stream.lookFails = 0;
    g_stream.lookMs = g_stream.lookMost = 0.0;
    g_stream.changes = g_stream.backlogs = g_stream.drainCuts = g_stream.spawnCuts = g_stream.changeRemoved = 0; /* the next level's note */
    g_stream.drainMs = g_stream.spawnMs = g_stream.drainMost = g_stream.spawnMost = 0.0;
    memset(g_stream.layerDrains, 0, sizeof g_stream.layerDrains); memset(g_stream.layerSpawns, 0, sizeof g_stream.layerSpawns);
    g_stream.spawnCaps = g_stream.tileDefers = g_stream.spawnedIn = 0;
    g_stream.budgetSum = 0.0; g_stream.budgetFloors = g_stream.buildFrames = 0; g_stream.nextChangeTick = 0; g_stream.pickTurn = 0;
    g_stream.floorHeld = 0; g_stream.quietHiSum = 0.0; g_stream.streamedAt = g_stream.streamedLeaves = g_stream.streamedMoved = g_stream.streamedQuiet = 0;
    g_stream.floorDry = g_stream.floorStuck = 0; g_stream.rebuiltSum = 0.0; g_stream.exitsNew = g_stream.exitsChanges = g_stream.changesSinceScan = 0;
    g_stream.drainLeast = 0; g_stream.spawnWithin = 0.0f; g_stream.scanAt = 0;
    g_stream.floorDue = g_stream.floorSpawned = 0;
    g_streamFlush.waitMs = g_streamFlush.waitMost = 0.0;
    g_streamFlush.frames = 0; g_streamFlush.ms = g_streamFlush.most = 0.0;
    g_streamRebuild.leaves = 0; g_streamCost.kind = 0; memset(g_streamLayerCost, 0, sizeof g_streamLayerCost);
    memset(&g_streamPace, 0, sizeof g_streamPace);
    memset(g_streamUnit, 0, sizeof g_streamUnit);         /* a unit's address means nothing once its level is not this one */
    memset(g_streamResource, 0, sizeof g_streamResource);
    memset(g_streamDeferred, 0, sizeof g_streamDeferred);
    memset(g_streamFormation, 0, sizeof g_streamFormation);
    memset(g_streamOut2, 0, sizeof g_streamOut2);
    memset(g_streamDue, 0, sizeof g_streamDue);
    memset(g_streamSpawned, 0, sizeof g_streamSpawned);
    memset(g_streamPoseGood, 0, sizeof g_streamPoseGood);
    memset(g_streamLive, 0, sizeof g_streamLive);
    memset(g_streamDetached, 0, sizeof g_streamDetached);
    memset(g_streamNeedsUnit, 0, sizeof g_streamNeedsUnit);
    memset(g_streamOutSince, 0, sizeof g_streamOutSince);
    memset(g_streamReplace, 0, sizeof g_streamReplace);
    memset(g_streamDrop, 0, sizeof g_streamDrop);
    memset(g_streamLayer, 0, sizeof g_streamLayer);
    g_streamDropHead = g_streamDropCount = g_streamDropFar = 0;
}

/* Whether the whole range is committed and writable: the generator's records have their +0xA8 zeroed in place. */
static int WorldStreamWritable(uint8_t* at, size_t bytes)
{
    uintptr_t p = (uintptr_t)at, end = p + bytes;
    while (p < end)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void*)p, &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0) return 0;
        DWORD protect = mbi.Protect & 0xFF;
        if (protect != PAGE_READWRITE && protect != PAGE_WRITECOPY && protect != PAGE_EXECUTE_READWRITE && protect != PAGE_EXECUTE_WRITECOPY)
            return 0;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p) return 0;
        p = next;
    }
    return 1;
}

static void WorldStreamSift(uint32_t root, uint32_t count)
{
    for (;;)
    {
        uint32_t child = root * 2 + 1;
        if (child >= count) return;
        if (child + 1 < count && g_streamDistance[g_streamOrder[child + 1]] > g_streamDistance[g_streamOrder[child]]) child++;
        if (g_streamDistance[g_streamOrder[root]] >= g_streamDistance[g_streamOrder[child]]) return;
        uint32_t swap = g_streamOrder[root];
        g_streamOrder[root] = g_streamOrder[child];
        g_streamOrder[child] = swap;
        root = child;
    }
}

/* The due locations by squared distance, ascending (a max-heap, then its largest moved to the end). */
static void WorldStreamSort(uint32_t count)
{
    for (uint32_t start = count / 2; start > 0; start--) WorldStreamSift(start - 1, count);
    for (uint32_t end = count; end > 1; end--)
    {
        uint32_t swap = g_streamOrder[0];
        g_streamOrder[0] = g_streamOrder[end - 1];
        g_streamOrder[end - 1] = swap;
        WorldStreamSift(0, end - 1);
    }
}

/* Every player's Helldiver's world position: EvpPlayers' avatars, their health records' units, the units' scene graph
 * roots. Players without a Helldiver or a readable position are skipped. The research observer, when on, is one more
 * position and is counted like a player. */
static void NearReportPlayer(const float* at);
static void WindowLeaveLocked(uint32_t s);   /* difficulty_stream_window.c */
static void WindowScanLocked(ULONGLONG now);
static int WindowRespawnRoomLocked(uint32_t i, int* gates);
static void WindowNoteLocked(void);
static uint32_t WindowEntered(void);
static void DestroyTick(void);                /* difficulty_world_destroy.c */
static void DestroyNote(void);
static const char* InstallDestroyQueue(void);
/* The streamed broad phase layers (difficulty_stream_layers.c). */
static int SlLeaves(uint32_t leaves[SL_LAYERS]);
static void SlPick(const uint32_t leaves[SL_LAYERS], const uint32_t queued[SL_LAYERS], int due, int* turn, int* drain, uint32_t* spawn);
static void SlFrameBegin(uint32_t layer);
static void SlFrameEnd(uint64_t world);
static void SlNote(void);
static const char* SlInstall(uintptr_t exe);
static int SlMainLayers(void);
static int SlWorld(uintptr_t exe, uintptr_t* w, uintptr_t* bp);
/* The streamer's tick of its last change frame that changed something (0: none): the makers' sampler tells bodies made
 * in it and in the frame after it from the others by the tick. */
static volatile uint32_t g_slChangeTick;
#if defined(CO_RESEARCH) || defined(CO_TEST)
/* Research instruments only: a package build has neither hook (adv REPORT-3c1ad6b). */
static const char* SlSamplerInstall(uintptr_t exe);
static const char* StInstall(uintptr_t exe); /* difficulty_stream_steptime.c */
#endif
static void StFrame(int kind);
static void StNote(void);
static int MoveStepRecent(void); /* difficulty_world_move.c */
static int FarGrassBusy(void);   /* difficulty_world_fargrass.c: a bake job is out */
static int MoveBuildRecent(void);
static double MoveStepMsNow(void);
/* A player passing in a hellpod streams no stamps on the way, and the ones at the place held stay (co_settle.h). */
static CoSettle g_streamSettle;
static void WorldStreamPlayersLocked(void)
{
    g_stream.playerCount = 0;
    if (g_stream.game != 0)
    {
        EvpPlayer players[EVT_MAX_PLAYERS];
        int count = EvpPlayers(g_stream.game, players);
        const ULONGLONG now = GetTickCount64();
        co_settle_level(&g_streamSettle, g_world.levels);
        for (int i = 0; count > 0 && i < count && g_stream.playerCount < EVT_MAX_PLAYERS; i++)
        {
            if (players[i].avatar == 0) continue;
            ActHealth h;
            float at[3] = {0.0f, 0.0f, 0.0f};
            if (!ActHealthOf(g_stream.game, players[i].avatar, &h) || h.unit == 0 || !EvpUnitPosition(h.unit, at)) continue;
            co_settle_at(&g_streamSettle, (uint32_t)i, at, now, g_streamPlayerAt[g_stream.playerCount]);
            if (g_stream.playerCount == 0) NearReportPlayer(at);
            g_streamPlayerPeer[g_stream.playerCount] = players[i].peer;
            g_stream.playerCount++;
        }
    }
    if (g_streamObserverOn)
    {
        g_streamPlayerAt[g_stream.playerCount][0] = g_streamObserverAt[0];
        g_streamPlayerAt[g_stream.playerCount][1] = g_streamObserverAt[1];
        g_stream.playerCount++;
    }
}

/* 1 with *distance when (x, y) is within `radius` of a player, or of the start area before one has landed. Read without
 * the lock from spawn threads: a torn position only moves a probe line in or out. */
static int WorldStreamNearPlayer(float x, float y, float radius, float* distance)
{
    int count = g_stream.playerCount;
    if (count > EVT_MAX_PLAYERS + 1) count = EVT_MAX_PLAYERS + 1;
    float best = 3.4e38f;
    for (int p = 0; p < count; p++)
    {
        float dx = x - g_streamPlayerAt[p][0], dy = y - g_streamPlayerAt[p][1];
        if (dx * dx + dy * dy < best) best = dx * dx + dy * dy;
    }
    if (count <= 0)
    {
        if (!g_stream.captured || g_stream.levels != g_world.levels) return 0;
        float dx = x - g_streamStart[0], dy = y - g_streamStart[1];
        best = dx * dx + dy * dy;
    }
    if (best > radius * radius) return 0;
    *distance = sqrtf(best);
    return 1;
}

/* The first player's x, y (for the terrain diagnostics); 0 without one. */
static int WorldStreamFirstPlayer(float* xy)
{
    if (g_stream.playerCount <= 0) return 0;
    xy[0] = g_streamPlayerAt[0][0];
    xy[1] = g_streamPlayerAt[0][1];
    return 1;
}

/* The mission's physics bodies in use, as difficulty_diag.c's NoteMissionMap reads them. */
static int WorldStreamBodies(uint32_t* bodies)
{
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
    uint64_t physics = 0;
    if (exe == 0 || SafeRead(exe + 0x27BA890u + 0xB0u * 2u, &physics, 8) || physics == 0 ||
        SafeRead((uintptr_t)physics + 0x4C, bodies, 4)) return 0;
    return 1;
}

/* The mission world's physics bodies in use and the body store's size (Havok's body manager at world + 0x38: bodies
 * [+0x38], slot count [+0x40] compared by the allocator 0xD456B0, free list [+0x48]; in use [+0x4C], 0x814F00's lock
 * xadd). The far formations stream in only while the bodies in use stay under kStreamFormationShare of the store: a
 * formation's trees each take a lightweight body (0x814F00) and its rocks their actors, and a full store makes the
 * allocator's "none" (0xFFFFFF) a body that the navmesh cutter reads (exe 0xCB304C; run s70i spawned 3069 at load). */
static const float kStreamFormationShare = 0.40f;
static const uint32_t kStreamFormationsPerScan = 16;
static int WorldStreamBodyStore(uint32_t* used, uint32_t* slots)
{
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
    uint64_t physics = 0;
    if (exe == 0 || SafeRead(exe + 0x27BA890u + 0xB0u * 2u, &physics, 8) || physics == 0 ||
        SafeRead((uintptr_t)physics + 0x4C, used, 4) || SafeRead((uintptr_t)physics + 0x40, slots, 4)) return 0;
    *slots &= 0x3FFFFFFFu;
    return *slots >= 1024 && *slots <= (1u << 24) && *used <= *slots;
}

/* The SpeedTree forest's trees and its grid's room (co_forest.h). The forest comes from a stamp unit's world, the one
 * op 0x6C spawns the stamps' trees into, and is kept for the level. 0 when it does not read. */
static int WorldStreamTrees(uint32_t* trees, uint32_t* room)
{
    const int pins = g_stream.forest != 0 || co_forest_pins(ApiModuleBase("helldivers2.exe", NULL));
    for (uint32_t i = 0; pins && g_stream.forest == 0 && i < g_stream.count && i < kStreamMaxLocations; i++)
        if (g_streamUnit[i] != 0) { co_forest_of_unit(g_streamUnit[i], &g_stream.forest); break; }
    if (g_stream.forest != 0 && co_forest_trees(g_stream.forest, trees, room)) return 1;
    if (!g_stream.forestBad)
    {
        g_stream.forestBad = 1;
        Note("difficulty: extra large maps: content streaming: the SpeedTree forest %s; its trees are not counted",
            pins ? "does not read" : "code is not the code this mod was made for");
    }
    return 0;
}

/* The captured level is in play (state 3, as MoveTick asks). The mission's unload (game 0xAB36F0, once a frame) steps
 * the generator to 0 (0xAB3CAE..0xAB3CED) and then destroys every unit of the main world itself, 20 a call
 * (0xAB3CF3..0xAB3DCB), while the game state still says mission. A stamp unit kept here is freed memory from then on,
 * and DestroyUnit frees whatever ends the world's list when it does not find its unit (exe 0x3389C3..0x3389F2). */
#define kStreamStateUnread 0xFFFFFFFFu /* the state did not read: neither in play nor left */
static uint32_t WorldStreamGenerator(void)
{
    uint32_t state = 0;
    return g_stream.level != 0 && !SafeRead(g_stream.level + 0x1E4, &state, 4) ? state : kStreamStateUnread;
}
static int WorldStreamInPlay(void) { return WorldStreamGenerator() == 3; }

/* DestroyUnit (exe 0x338960) trusts its unit: it destroys and frees its argument whether the unit list
 * ([world+0x230590], count +0x230588) held it or not (0x3389D1..0x338A3F). 1 when the list holds the unit and it is
 * this world's ([unit+0x18], the constructor at 0xD7879); 0 when not; -1 when the list does not read. The destroy reads
 * the same list unlocked on this thread a moment later, so the look is as safe as the call. The world's queued
 * destroys (world+0x30) are not searched: their one writer (0x12D2D0) stores objects of the handle table only, which
 * a stamp unit is not (its +8 is the world's allocator, 0xD784B, where the drain reads a handle, 0x12E71B). */
#define DIF_STREAM_UNITS 0x230588u      /* u32 count; the array's address 8 bytes on */
enum { kStreamUnitsMost = 1u << 22 };   /* the search's bound, not an engine limit: a count above it is taken for a misread */
static int WorldStreamListed(uint64_t world, uint64_t unit)
{
    uint32_t count = 0;
    uint64_t list = 0, in = 0;
    if (SafeRead((uintptr_t)world + DIF_STREAM_UNITS, &count, 4) || SafeRead((uintptr_t)world + DIF_STREAM_UNITS + 8, &list, 8) || count > kStreamUnitsMost) return -1;
    const double t0 = HitchNow();
    const int found = SafeFind((uintptr_t)list, count, unit);
    const double ms = HitchNow() - t0;
    g_stream.looks++; g_stream.listed = count; g_stream.lookMs += ms;
    if (ms > g_stream.lookMost) g_stream.lookMost = ms;
    if (found < 0) return -1;
    return found == 1 && !SafeRead((uintptr_t)unit + 0x18, &in, 8) && in == world;
}

/* A slot whose unit is no longer this world's: the engine took it, and what it left of the location is not known. The
 * slot is given up for the level with no engine call on the unit: not live, never due again (a window slot is freed). */
static void WorldStreamGoneLocked(uint32_t i, uint64_t unit)
{
    const uint64_t zero = 0;
    memcpy((void*)((uintptr_t)g_stream.array + (size_t)i * kStreamRecord + 0xA8), &zero, 8);
    g_streamUnit[i] = 0;
    g_streamLive[i] = 0;
    g_streamSpawned[i] = 1;
    g_streamDetached[i] = 1;
    g_streamPoseGood[i] = 0;
    g_streamNeedsUnit[i] = 0;
    g_streamDue[i] = 0;
    g_streamOutSince[i] = 0;
    g_streamReplace[i] = 0;
    if (g_stream.spawned > 0) g_stream.spawned--;
    if (g_stream.gone++ == 0)
        Note("difficulty: extra large maps: content streaming: slot %u's stamp unit 0x%llX is not in the world's unit list any more: "
            "the slot is given up with no destroy call; further ones are counted", i, (unsigned long long)unit);
    WindowLeaveLocked(i);
}

/* Remove one streamed location: DestroyUnit (the table's +0x388) takes its stamp unit and the layer units go; the real
 * record's +0xA8 is zeroed and the location waits for a new stamp unit. Only a location this streamer queued
 * (g_streamLive) is ever passed here. 1 removed; 0 not, for good (given up, or removal is off); -1 not this frame: the
 * world's unit list did not read, which one torn read can do, so the slot is asked again and removal goes off only
 * after kStreamLookFails such looks in a row. */
enum { kStreamLookFails = 8 };
static int WorldStreamRemoveLocked(uint32_t i)
{
    if (g_stream.destroyBad || g_streamInside || !WorldStreamInPlay()) return 0;
    uint64_t world = 0;
    uintptr_t wi = 0, table = 0, destroy = 0;
    if (SafeRead((uintptr_t)g_stream.context + 0x40, &world, 8) || world == 0 ||
        SafeRead(g_stream.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 ||
        SafeRead(wi + DIF_STREAM_TABLE, &table, 8) || table == 0 ||
        SafeRead(table + DIF_STREAM_DESTROY_ENTRY, &destroy, 8) || destroy == 0)
    {
        g_stream.destroyBad = 1;
        Note("difficulty: extra large maps: content streaming: removal off: the world interface's +0x388 entry does not read");
        return 0;
    }
    uintptr_t wait = WorldStreamAnimWait(table);
    if (wait == 0)
    {
        g_stream.destroyBad = 1;
        Note("difficulty: extra large maps: content streaming: removal off: the world interface's +0x410 entry (the animation wait) is not the reviewed one");
        return 0;
    }
    uint64_t unit = g_streamUnit[i];
    const int listed = WorldStreamListed(world, unit);
    if (listed < 0)
    {
        g_stream.lookFails++;
        if (++g_stream.lookBad >= kStreamLookFails)
        {
            g_stream.destroyBad = 1;
            Note("difficulty: extra large maps: content streaming: removal off: the world's unit list did not read %u times in a row", g_stream.lookBad);
        }
        return -1;
    }
    g_stream.lookBad = 0;
    if (listed == 0) { WorldStreamGoneLocked(i, unit); return 0; }
    if (g_stream.removed == 0) g_stream.bodyBeforeSeen = WorldStreamBodies(&g_stream.bodyBefore);
    ((WorldStreamAnimWaitFn)wait)(world);
    InterlockedIncrement(&g_streamDestroys);
    ((WorldStreamDestroyUnitFn)destroy)(world, unit, 0, 0);
    uint64_t zero = 0;
    memcpy((void*)((uintptr_t)g_stream.array + (size_t)i * kStreamRecord + 0xA8), &zero, 8);
    g_streamUnit[i] = 0;
    g_streamDeferred[i] = 1;
    g_streamSpawned[i] = 0;
    g_streamLive[i] = 0;
    g_streamDetached[i] = 1;
    g_streamDue[i] = 0;
    g_streamOutSince[i] = 0;
    g_streamNeedsUnit[i] = 1;
    g_streamReplace[i] = 0;
    if (g_stream.spawned > 0) g_stream.spawned--;
    g_stream.removed++;
    if (!g_stream.bodyNoted && g_stream.removed >= 20) g_stream.bodyPending = 1; /* read after the next scan (a few frames on) */
    WindowLeaveLocked(i); /* a window slot: out of the index and free (difficulty_stream_window.c) */
    return 1;
}

/* Spawn a new stamp unit for a removed location: SpawnUnit (table +0x380) with the saved resource and pose, then
 * SpawnStamps' set-data call (the +0xB8 object's +0x10 object, method +8) with {&location index, 2, 4, 0}. The new unit
 * stays in g_streamUnit; the real record's +0xA8 stays 0. 0 when a call cannot be made (the location stays due). */
static int WorldStreamRespawnLocked(uint32_t i)
{
    if (g_streamInside || !g_streamPoseGood[i] || g_streamResource[i] == 0) return 0;
    uint64_t world = 0;
    uintptr_t wi = 0, table = 0, spawn = 0, holder = 0, object = 0, method = 0;
    if (SafeRead((uintptr_t)g_stream.context + 0x40, &world, 8) || world == 0 ||
        SafeRead(g_stream.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 ||
        SafeRead(wi + DIF_STREAM_TABLE, &table, 8) || table == 0 ||
        SafeRead(table + DIF_STREAM_SPAWN_ENTRY, &spawn, 8) || spawn == 0 ||
        SafeRead(wi + DIF_STREAM_SETDATA_ENTRY, &holder, 8) || holder == 0 ||
        SafeRead(holder + DIF_STREAM_SETDATA_OBJECT, &object, 8) || object == 0 ||
        SafeRead(object + DIF_STREAM_SETDATA_METHOD, &method, 8) || method == 0)
    {
        if (!g_stream.respawnBad)
        {
            g_stream.respawnBad = 1;
            Note("difficulty: extra large maps: content streaming: a removed location's new stamp unit cannot be made (the world interface's +0x380 entry or the set-data call does not read)");
        }
        return 0;
    }
    uint64_t unit = ((WorldStreamSpawnUnitFn)spawn)(world, g_streamResource[i], g_streamPose[i], 0, 0);
    if (unit == 0)
    {
        if (!g_stream.respawnBad)
        {
            g_stream.respawnBad = 1;
            Note("difficulty: extra large maps: content streaming: SpawnUnit returned no stamp unit for a removed location");
        }
        return 0;
    }
    g_streamUnit[i] = unit;
    g_streamDetached[i] = 1;
    uint32_t index = i;
    WorldStreamSetData data = {&index, 2, 4, 0};
    ((WorldStreamSetDataFn)method)(unit, &data, 1, 0x39D268A5u);
    g_streamNeedsUnit[i] = 0;
    g_stream.respawned++;
    return 1;
}

/* The mission's own stamps: every stamp record an important location points to (G+0x8996B0, 0x108 each, count
 * G+0x11A715C; loc+0x98 = &stamp[i], op 0x16 at 0xF90275..0xF90280): objectives, extraction, outposts, points of
 * interest, satellites. The game ties their entities to the location by the stamp unit alone ([loc+0x98]+0xA8 against
 * the entity's spawning unit: Objective init 0x5D1067/0x5D122B, ExtractionZone 0x8D1216, ShowOnMap 0x677D85,
 * GuardForce 0x90C459), builds GuardForce once at level setup (0xAD0010 -> 0xAD1FD5) from what exists then, and unlocks
 * extraction for good once no live main objective is pending (0x5D91B0, Mission +0xAD8 at 0x8F53FF, never cleared).
 * So these spawn with the level like vanilla's and are never removed: a deferred main objective meant extraction at
 * landing, and a removed one came back as a fresh instance with no location. Returns how many were marked. */
static uint32_t WorldStreamMissionLocked(const uint8_t* array, uint32_t count)
{
    memset(g_streamMission, 0, count);
    uintptr_t block = 0;
    uint32_t locations = 0, marked = 0, outside = 0;
    if (g_worldLevelLive == 0 || SafeRead(g_worldLevelLive + 0x2C8, &block, 8) || block == 0 || SafeRead(block + 0x11A715Cu, &locations, 4))
    {
        Note("difficulty: extra large maps: content streaming: the important locations do not read; every far location streams");
        return 0;
    }
    if (locations > 256) Note("difficulty: extra large maps: content streaming: %u important locations, more than the game's 256 places", locations);
    if (locations > 1024) locations = 1024;
    const uintptr_t first = (uintptr_t)array, end = first + (uintptr_t)count * kStreamRecord;
    for (uint32_t l = 0; l < locations; l++)
    {
        uint64_t P = 0;
        if (SafeRead(block + g_importantList + 0x108ull * l + 0x98, &P, 8) || P == 0) continue;
        if (P < first || P >= end || (P - first) % kStreamRecord != 0) { outside++; continue; }
        uint32_t i = (uint32_t)((P - first) / kStreamRecord);
        if (!g_streamMission[i]) { g_streamMission[i] = 1; marked++; }
    }
    if (outside) Note("difficulty: extra large maps: content streaming: %u important location(s) point outside the location records", outside);
    return marked;
}

/* The generator's first call for this level: read its records and zero the stamp units outside the start area. The
 * original then spawns the start area and skips the deferred locations; WorldStreamEnd puts the units back when the op
 * reaches its count. `resume` is normally 0; locations before a non-zero one were already done before this call. */
static void WorldStreamCaptureLocked(uint8_t* pass, uint8_t* record, uint8_t* context)
{
    g_stream.level = g_worldLevel;
    g_stream.levels = g_world.levels;
    uint8_t* array = NULL;
    uint64_t countAt = 0;
    uint32_t count = 0, resume = 0;
    if (SafeRead((uintptr_t)pass + 8, &array, 8) || array == NULL || SafeRead((uintptr_t)pass + 0x48, &countAt, 8) || countAt == 0 ||
        SafeRead((uintptr_t)countAt, &count, 4))
    {
        g_stream.failed = 1;
        Note("difficulty: extra large maps: content streaming off for this level: the generator's pass (0x%llX) does not read",
            (unsigned long long)(uintptr_t)pass);
        return;
    }
    SafeRead((uintptr_t)pass + 0xD8, &resume, 4);
    if (g_world.values == NULL || !(g_world.values->half > 0.0f))
    {
        g_stream.failed = 1;
        Note("difficulty: extra large maps: content streaming off for this level: the world's values are not set");
        return;
    }
    g_stream.pass = pass;
    g_stream.record = record;
    g_stream.context = context;
    g_stream.array = array;
    g_stream.countAt = (uintptr_t)countAt;
    g_stream.half = g_world.values->half;
    g_stream.count = count;
    /* The start area's centre: the squad's drop point on the war board when it reads, else the map centre. */
    {
        uint64_t board = 0;
        uint8_t flag = 0;
        float at[2] = {0.0f, 0.0f};
        int fromBoard = g_stream.game != 0 && !SafeRead(g_stream.game + DIF_STREAM_BOARD, &board, 8) && board != 0 &&
            !SafeRead((uintptr_t)board + DIF_STREAM_BOARD_FLAG, &flag, 1) && flag == 1 &&
            !SafeRead((uintptr_t)board + DIF_STREAM_BOARD_X, &at[0], 4) &&
            !SafeRead((uintptr_t)board + DIF_STREAM_BOARD_Y, &at[1], 4) &&
            isfinite(at[0]) && isfinite(at[1]) && fabsf(at[0]) <= g_stream.half && fabsf(at[1]) <= g_stream.half;
        if (fromBoard) { g_streamStart[0] = at[0]; g_streamStart[1] = at[1]; }
        else { g_streamStart[0] = 0.0f; g_streamStart[1] = 0.0f; }
        Note("difficulty: extra large maps: content streaming: the start area is %s (%.1f, %.1f)",
            fromBoard ? "the squad's drop point from the war board" : "the map centre; no usable drop point on the war board",
            (double)g_streamStart[0], (double)g_streamStart[1]);
    }
    if (count == 0)
    {
        g_stream.captured = 1;
        g_stream.generatorDone = 1;
        Note("difficulty: extra large maps: content streaming: level 0x%llX has no locations", (unsigned long long)g_stream.level);
        return;
    }
    if (count > kStreamMaxLocations)
    {
        g_stream.count = 0;
        g_stream.captured = 1;
        g_stream.generatorDone = 1;
        Note("difficulty: extra large maps: content streaming: level 0x%llX has %u locations (more than %d); nothing is deferred",
            (unsigned long long)g_stream.level, count, (int)kStreamMaxLocations);
        return;
    }
    if (resume >= count)
    {
        g_stream.captured = 1;
        g_stream.generatorDone = 1;
        Note("difficulty: extra large maps: content streaming: level 0x%llX is already past its locations (%u of %u); nothing is deferred",
            (unsigned long long)g_stream.level, resume, count);
        return;
    }
    if (!WorldStreamWritable(array, (size_t)count * kStreamRecord))
    {
        g_stream.count = 0;
        g_stream.captured = 1;
        g_stream.generatorDone = 1;
        Note("difficulty: extra large maps: content streaming: level 0x%llX's %u location records are not writable; nothing is deferred",
            (unsigned long long)g_stream.level, count);
        return;
    }
    uint32_t mission = WorldStreamMissionLocked(array, count);
    uint32_t kept = 0, deferred = 0, bad = 0, formations = 0, missionFar = 0;
    float keep = kStreamStartRadius * kStreamStartRadius;
    float full = 2.0f * g_stream.half;
    for (uint32_t i = 0; i < count;)
    {
        uint32_t n = count - i;
        if (n > kStreamScanChunk) n = kStreamScanChunk;
        if (SafeRead((uintptr_t)array + (size_t)i * kStreamRecord, g_streamChunk, (size_t)n * kStreamRecord))
        {
            bad += n; kept += n; /* the chunk did not read: leave those locations to the generator */
            i += n;
            continue;
        }
        for (uint32_t j = 0; j < n; j++, i++)
        {
            if (i < resume) { kept++; continue; } /* done before this call: no unit to hold back */
            const uint8_t* loc = (const uint8_t*)g_streamChunk + (size_t)j * kStreamRecord;
            float at[2] = {0.0f, 0.0f};
            uint64_t unit = 0;
            memcpy(at, loc, 8);
            memcpy(&unit, loc + 0xA8, 8);
            g_streamUnit[i] = unit;
            uint64_t holder = 0, resource = 0;
            memcpy(&holder, loc + DIF_STREAM_LOC_RESOURCE, 8);
            if (holder != 0) SafeRead((uintptr_t)holder + 8, &resource, 8);
            g_streamResource[i] = resource;
            if (unit != 0 && !SafeRead((uintptr_t)unit + DIF_STREAM_UNIT_POSE, g_streamPose[i], 64))
                g_streamPoseGood[i] = resource != 0; /* no resource: it could not be spawned again */
            else
                g_streamPoseGood[i] = 0;
            if (!isfinite(at[0]) || !isfinite(at[1]) || at[0] < -1.0f || at[1] < -1.0f || at[0] > full + 1.0f || at[1] > full + 1.0f)
            {
                kept++; bad++; /* no distance: spawn it with the generator */
                continue;
            }
            g_streamAt[i][0] = at[0] - g_stream.half;
            g_streamAt[i][1] = at[1] - g_stream.half;
            float dx = g_streamAt[i][0] - g_streamStart[0], dy = g_streamAt[i][1] - g_streamStart[1];
            if (unit == 0 || dx * dx + dy * dy <= keep) { kept++; continue; }
            if (g_streamMission[i]) { kept++; missionFar++; continue; } /* the mission's own: spawned with the level, never removed */
            uint64_t zero = 0;
            memcpy((void*)((uintptr_t)array + (size_t)i * kStreamRecord + 0xA8), &zero, 8);
            g_streamDeferred[i] = 1;
            if (StampsKeepFar(i)) { g_streamFormation[i] = 1; formations++; }
            deferred++;
        }
    }
    g_stream.kept = kept;
    g_stream.deferred = deferred;
    g_stream.bad = bad;
    g_stream.captured = 1;
    if (deferred == 0) g_stream.generatorDone = 1; /* nothing held back: the original's op needs no restore */
    Note("difficulty: extra large maps: content streaming: level 0x%llX: %u locations, %u kept at the start (%u of them the mission's own beyond the start area, of %u), %u deferred (%u of them far formations, streamed under the physics budget)",
        (unsigned long long)g_stream.level, count, kept, missionFar, mission, deferred, formations);
    if (bad) Note("difficulty: extra large maps: content streaming: %u location record(s) did not read; they stay with the generator", bad);
}

/* 1 when this call is the generator's own call of the deferring op (the caller must run WorldStreamEnd after it). */
static int WorldStreamBegin(uint8_t* pass, uint8_t* record, uint8_t* context)
{
    if (pass == NULL || !(g_world.current > 1.0f)) return 0;
    AcquireSRWLockExclusive(&g_streamLock);
    if (g_stream.level != g_worldLevel || g_stream.levels != g_world.levels) WorldStreamClearLocked();
    if (!g_stream.captured && !g_stream.failed) WorldStreamCaptureLocked(pass, record, context);
    int watched = g_stream.captured && !g_stream.generatorDone && g_stream.deferred > 0 && g_stream.pass == pass;
    ReleaseSRWLockExclusive(&g_streamLock);
    return watched;
}

/* The generator's record and context live in the generation block [level+0x2B0] (records = blk, ctx = blk + 0x19B008,
 * 0xC14950 at game 0xF6E49C..0xF6E4C7), which the game frees when the level goes live (0xFBF1CB..0xFBF22C, state 3).
 * Their fields point into G (records-2: record 137's +0x10..+0xD0 are all G+ offsets), which stays, so the streamer
 * keeps its own copies of both from the moment the op is done and calls SpawnLevelLayers with those. A context field
 * that points back into the freed block is logged. */
static uint8_t g_streamRecordCopy[0x19B0];
static uint8_t g_streamContextCopy[0x100];
/* The context's arena ([ctx+0], the generation thread's) is destroyed when the level is done (0xF7412D -> 0x173BB30:
 * its chunks freed, its head zeroed). SpawnLevelLayers allocates from it for every layer whose scale ([r13+0xC]) is not
 * 1 (0xF47041..0xF47053 -> 0xF470C6, 16 bytes; the allocator 0x173B8D0 reads the head at 0x173B936): the cities' scaled
 * buildings. So a streamed city location ended the game (s75e/s75f, c0000005 at game+0x173B936 as the first city
 * stamps came within 500 m of a player), while the open land's unscaled stamps never asked. The copy gets its own arena,
 * laid out as 0x173B700 makes one (as difficulty_world_move.c's MoveArena: arena {head chunk +0, lock +8, owner thread
 * index +0xC (-1: no index call), growth +0x10}; chunk {used +0, data +8, capacity +0x10, next +0x18}); after each of
 * the streamer's calls it is reset as the game resets its own after an op (0x173BAB0, 0xF261FE): chunks the allocator
 * grew are freed, ours is the last and stays. */
#define STREAM_ARENA_BYTES (4u << 20)
static struct { uint64_t head; LONG lock; int32_t owner; uint64_t growth; } g_streamArena;
static struct { uint64_t used, data, capacity, next; } g_streamArenaChunk;
static int g_streamArenaReset = -1; /* 1: 0x173BAB0 is the reviewed reset */
static int WorldStreamArena(void)
{
    if (g_streamArenaChunk.data == 0)
    {
        void* p = VirtualAlloc(NULL, STREAM_ARENA_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p == NULL) return 0;
        g_streamArenaChunk.data = (uint64_t)(uintptr_t)p; g_streamArenaChunk.capacity = STREAM_ARENA_BYTES;
    }
    if (g_streamArenaReset < 0) g_streamArenaReset = ApiCodeMatches(g_stream.game + 0x173BAB0u, "40 53 48 83 EC 20 48 8B 01 48 8B D9 48 85 C0 74") ? 1 : 0;
    if (g_streamArenaReset != 1) return 0;
    g_streamArenaChunk.used = 0; g_streamArenaChunk.next = 0;
    g_streamArena.head = (uint64_t)(uintptr_t)&g_streamArenaChunk; g_streamArena.lock = 0; g_streamArena.owner = -1; g_streamArena.growth = 0x100000;
    return 1;
}
static void WorldStreamArenaReset(void)
{
    typedef void (*ArenaFn)(uint64_t arena);
    if (g_streamArena.head != 0 && g_streamArenaReset == 1) ((ArenaFn)(g_stream.game + 0x173BAB0u))((uint64_t)(uintptr_t)&g_streamArena);
}
static void WorldStreamDetachLocked(void)
{
    uintptr_t ctx = (uintptr_t)g_stream.context, blk = ctx - 0x19B008u, end = ctx + 0xE8;
    if (SafeRead((uintptr_t)g_stream.record, g_streamRecordCopy, sizeof g_streamRecordCopy) || SafeRead(ctx, g_streamContextCopy, 0xE8))
    {
        Note("difficulty: extra large maps: content streaming: the generator's record or context did not read; streaming keeps the originals");
        return;
    }
    char inside[160]; int len = 0;
    for (uint32_t k = 0; k < 0xE8; k += 8)
    {
        uint64_t v; memcpy(&v, g_streamContextCopy + k, 8);
        if (v >= blk && v < end && len < (int)sizeof inside - 8) len += snprintf(inside + len, sizeof inside - len, " +0x%X", k);
    }
    g_stream.pass = g_streamRecordCopy + ((uint8_t*)g_stream.pass - (uint8_t*)g_stream.record);
    g_stream.record = g_streamRecordCopy;
    g_stream.context = g_streamContextCopy;
    int ownArena = WorldStreamArena();
    if (ownArena) { uint64_t a = (uint64_t)(uintptr_t)&g_streamArena; memcpy(g_streamContextCopy, &a, 8); }
    else Note("difficulty: extra large maps: content streaming: no arena of its own (no memory, or the reset is not the reviewed one); scaled layers (city buildings) would read the level's freed one");
    Note("difficulty: extra large maps: content streaming: kept its own copy of the generator's record and context (the generation block is freed when the level goes live)%s%s",
        len ? "; context fields into that block:" : "", len ? inside : "");
}

/* After a generator call: at the count the op is done, so every zeroed stamp unit goes back. */
static void WorldStreamEnd(uint8_t* pass)
{
    AcquireSRWLockExclusive(&g_streamLock);
    if (g_stream.captured && !g_stream.generatorDone && g_stream.deferred > 0 && pass == g_stream.pass && g_stream.level == g_worldLevel && g_stream.levels == g_world.levels)
    {
        uint32_t resume = 0;
        if (SafeRead((uintptr_t)pass + 0xD8, &resume, 4))
        {
            if (!g_stream.resumeBad)
            {
                g_stream.resumeBad = 1;
                Note("difficulty: extra large maps: content streaming: the generator's resume index does not read; its locations stay held");
            }
        }
        else if (resume >= g_stream.count)
        {
            for (uint32_t i = 0; i < g_stream.count; i++)
                if (g_streamDeferred[i])
                    memcpy((void*)((uintptr_t)g_stream.array + (size_t)i * kStreamRecord + 0xA8), &g_streamUnit[i], 8);
            g_stream.generatorDone = 1;
            Note("difficulty: extra large maps: content streaming: the generator finished; %u deferred locations are streamed near the players",
                g_stream.deferred);
            WorldStreamDetachLocked();
        }
    }
    ReleaseSRWLockExclusive(&g_streamLock);
}

/* SpawnLevelLayers: the generator's calls capture, defer and restore; the streamer's own call has g_streamOwn set. While
 * a generator call is inside (Begin, the original, End) g_streamInside keeps the game thread from calling the original. */
static uint8_t WorldStreamSpawn(uint8_t* pass, uint8_t* record, uint8_t* context)
{
    if (g_streamSpawn == NULL) return 0;
    if (g_streamOwn == (LONG)GetCurrentThreadId()) return g_streamSpawn(pass, record, context);
    InterlockedIncrement(&g_streamInside);
    int watched = WorldStreamBegin(pass, record, context);
    MemProbeSpawnCall probe; /* research (HELLFORGE_XL_MEMPROBE=1): what each generator call keeps */
    int probing = MemProbeOn() && g_world.current > 1.0f && MemProbeSpawnBefore(&probe, pass, context);
    uint8_t result = g_streamSpawn(pass, record, context);
    if (probing) MemProbeSpawnAfter(&probe, pass);
    if (watched) WorldStreamEnd(pass);
    InterlockedDecrement(&g_streamInside);
    return result;
}

/* A removal for a later frame; 0 when the slot is queued already or the queue is full (a later scan asks again). */
static int WorldStreamDropLocked(uint32_t i, int kind)
{
    if (g_streamDrop[i] || g_streamDropCount >= kStreamDropQueue) return 0;
    g_streamDrop[i] = (uint8_t)kind;
    if (STREAM_DROP_IS_FAR(kind)) g_streamDropFar++;
    g_streamDropList[(g_streamDropHead + g_streamDropCount++) % kStreamDropQueue] = i;
    return 1;
}

static void WindowEvictedLocked(void); /* difficulty_stream_window.c */
/* A player (or the observer) within kStreamOutRadius of slot s: the near stamps' keep radius, which no far removal may
 * cross. */
static int WorldStreamWithinOut(uint32_t s)
{
    for (int p = 0; p < g_stream.playerCount; p++)
    {
        const float dx = g_streamAt[s][0] - g_streamPlayerAt[p][0], dy = g_streamAt[s][1] - g_streamPlayerAt[p][1];
        if (dx * dx + dy * dy <= kStreamOutRadius * kStreamOutRadius) return 1;
    }
    return 0;
}
/* A change frame: up to `most` queued removals, until they took `ms`; with `layer` 4 or 6..9, only stamps whose bodies
 * are in that broad phase layer, so the frame rebuilds one layer (the others go to the queue's tail). Each is asked
 * again, as frames went by since its scan: still live with its own unit, and still out of reach (its timer runs),
 * flagged for new ground, or a far slot; the governor's only while no player came within kStreamOutRadius (its wait
 * in the queue, 2-8 s, is 90-360 m at 45 m/s; the near ring would take it again), the window's whatever the distance
 * (it makes room for the near stamps). A slot whose look did not read goes back to the queue's head
 * and ends the frame's removals. The removals done. */
static uint32_t WorldStreamDrainLocked(uint32_t most, double ms, int layer, uint32_t least)
{
    uint32_t done = 0, given = 0; /* a slot given up cost a search of the world's unit list: it counts as a removal here */
    const uint32_t queued = g_streamDropCount;
    const double t0 = HitchNow();
    g_stream.drainCut = 0;
    for (uint32_t looked = 0; g_streamDropCount > 0 && looked < queued && done + given < most; looked++)
    {
        const uint32_t i = g_streamDropList[g_streamDropHead];
        g_streamDropHead = (g_streamDropHead + 1) % kStreamDropQueue; g_streamDropCount--;
        const int kind = g_streamDrop[i];
        g_streamDrop[i] = 0;
        if (STREAM_DROP_IS_FAR(kind)) g_streamDropFar--;
        if (i >= g_stream.count || !g_streamLive[i] || !g_streamPoseGood[i] || g_streamNeedsUnit[i] || g_streamUnit[i] == 0) continue;
        if (kind == STREAM_DROP_OUT ? g_streamOutSince[i] == 0 : kind == STREAM_DROP_REPLACE ? !g_streamReplace[i] : !(g_streamOut2[i] > 0.0f)) continue;
        const int nearPlayer = STREAM_DROP_IS_FAR(kind) && WorldStreamWithinOut(i);
        if (kind == STREAM_DROP_GOV && nearPlayer) { g_streamPace.farNearSkipped++; continue; }
        if (layer >= 0 && (g_streamLayer[i] ? g_streamLayer[i] : 4) != layer)
        {
            WorldStreamDropLocked(i, kind); /* the slot just freed takes it */
            continue;
        }
        const uint32_t gone = g_stream.gone;
        const int removed = WorldStreamRemoveLocked(i);
        if (removed < 0)
        {
            g_streamDropHead = (g_streamDropHead + kStreamDropQueue - 1) % kStreamDropQueue; g_streamDropCount++;
            g_streamDropList[g_streamDropHead] = i;
            g_streamDrop[i] = (uint8_t)kind;
            if (STREAM_DROP_IS_FAR(kind)) g_streamDropFar++;
            break;
        }
        const int left = removed || g_stream.gone != gone; /* the slot left the world's count: destroyed, or given up */
        if (!left) continue;
        done += removed != 0; given += removed == 0;
        if (removed && kind == STREAM_DROP_REPLACE) /* due again: a window slot is free and enters again from the plan */
        {
            g_stream.replaced++;
            if (g_streamDeferred[i]) g_streamDue[i] = 1;
        }
        else if (STREAM_DROP_IS_FAR(kind)) WindowEvictedLocked(); /* a far slot given up is free as well */
        if (removed && nearPlayer) g_streamPace.farNearRemoved[kind == STREAM_DROP_GOV]++;
        if (HitchNow() - t0 >= ms && done + given >= least) { g_stream.drainCut = g_streamDropCount > 0 && looked + 1 < queued && done + given < most; break; }
    }
    return done;
}

/* A stamp flagged for new ground into the scan's kStreamReplaceBatch picks, which keep the largest keys: its nearest
 * player's distance over the distance its meshes are drawn to, squared. Past 1 no player sees the swap; below it the
 * farthest for its size goes first, where a swap shows least. */
static void WorldStreamReplacePick(uint32_t* pick, float* key, uint32_t* picks, uint32_t i, float k)
{
    if (*picks < kStreamReplaceBatch) { pick[*picks] = i; key[(*picks)++] = k; return; }
    uint32_t m = 0;
    for (uint32_t j = 1; j < kStreamReplaceBatch; j++) if (key[j] < key[m]) m = j;
    if (k > key[m]) { pick[m] = i; key[m] = k; }
}

/* An out-of-reach stamp into the scan's picks, a min-heap of the `room` largest keys (its nearest player's distance over
 * its own removal radius, squared): over the queue's room the least far wait for a later scan. */
static void WorldStreamOutPick(uint32_t* pick, float* key, uint32_t* picks, uint32_t room, uint32_t i, float k)
{
    uint32_t at;
    if (*picks < room)
    {
        for (at = (*picks)++; at > 0 && key[(at - 1) / 2] > k; at = (at - 1) / 2) { pick[at] = pick[(at - 1) / 2]; key[at] = key[(at - 1) / 2]; }
        pick[at] = i; key[at] = k;
        return;
    }
    if (room == 0 || !(k > key[0])) return;
    for (at = 0;;)
    {
        uint32_t c = 2 * at + 1;
        if (c >= *picks) break;
        if (c + 1 < *picks && key[c + 1] < key[c]) c++;
        if (!(key[c] < k)) break;
        pick[at] = pick[c]; key[at] = key[c]; at = c;
    }
    pick[at] = i; key[at] = k;
}

/* The picks' heap sorted in place, the farthest first: the queue drains in that order, so a stamp lingering farthest
 * past its radius goes before one that has only just crossed it. */
static void WorldStreamOutOrder(uint32_t* pick, float* key, uint32_t picks)
{
    for (uint32_t end = picks; end > 1; end--)
    {
        const uint32_t li = pick[end - 1]; const float lk = key[end - 1];
        pick[end - 1] = pick[0]; key[end - 1] = key[0];
        uint32_t at = 0, n = end - 1;
        for (;;)
        {
            uint32_t c = 2 * at + 1;
            if (c >= n) break;
            if (c + 1 < n && key[c + 1] < key[c]) c++;
            if (!(key[c] < lk)) break;
            pick[at] = pick[c]; key[at] = key[c]; at = c;
        }
        pick[at] = li; key[at] = lk;
    }
}
static uint32_t g_streamOutPick[kStreamDropQueue - kStreamDropReserve];
static float g_streamOutKey[kStreamDropQueue - kStreamDropReserve];

/* The stamps whose layers spawned on the level's heights by a live tile (NearLayerPendingAdd): once a grid covers one,
 * a stamp the streamer may take out goes out and comes back on it, as after a tile's change; any other is counted
 * (the start area's and the mission's stamps stay). One with no grid after 60 s is dropped. */
static float MoveStampFootprint(uint32_t i, float f[4]); /* difficulty_world_move.c */
static float MoveStampSeen2(float side);
static void WorldStreamPendingLocked(ULONGLONG now)
{
    AcquireSRWLockExclusive(&g_layerPending.lock);
    if (g_layerPending.level != g_world.levels) { ReleaseSRWLockExclusive(&g_layerPending.lock); return; }
    uint32_t keep = 0;
    for (uint32_t p = 0; p < g_layerPending.count; p++)
    {
        float x = g_layerPending.e[p].x, y = g_layerPending.e[p].y;
        if (NearLayerGridAt(x, y, NEAR_LAYER_REACH) < 0)
        {
            if (now - g_layerPending.e[p].at > 60000) g_layerPending.expired++;
            else g_layerPending.e[keep++] = g_layerPending.e[p];
            continue;
        }
        uint32_t i = 0;
        while (i < g_stream.count && i < kStreamMaxLocations && g_streamUnit[i] != g_layerPending.e[p].unit) i++;
        if (i < g_stream.count && i < kStreamMaxLocations && g_streamDeferred[i] && g_streamLive[i] && g_streamPoseGood[i] && !g_streamNeedsUnit[i] &&
            !g_streamMission[i])
        {
            g_streamReplace[i] = 1;
            float f[4];
            g_streamSeen2[i] = MoveStampSeen2(MoveStampFootprint(i, f)); /* ranked by how far it is seen, as MoveStampsReplace */
            g_layerPending.replaced++;
        }
        else g_layerPending.kept++;
    }
    g_layerPending.count = keep;
    ReleaseSRWLockExclusive(&g_layerPending.lock);
}

/* Every 0.25 s: the players' positions and every deferred location's nearest one. A location below kStreamInRadius
 * becomes due (and stays due); the due ones are ordered nearest first. A live location beyond its removal radius starts
 * a timer and is queued for removal after kStreamOutTimeout, the farthest first, as many as the queue has room for: a
 * cap below the window's entry rate let stamps linger to 2 km and hold the far ring's 5000. */
static void WorldStreamScanLocked(ULONGLONG now)
{
    if (now < g_stream.nextScan) return;
    g_stream.nextScan = now + 250;
    WorldStreamPlayersLocked();
    WorldStreamPendingLocked(now);
    if (g_stream.bodyPending && !g_stream.bodyNoted) /* the after count, a scan after the 20th removal */
    {
        uint32_t after = 0;
        if (WorldStreamBodies(&after))
        {
            g_stream.bodyPending = 0;
            g_stream.bodyNoted = 1;
            if (g_stream.bodyBeforeSeen)
            {
                int leaked = (int32_t)after - (int32_t)g_stream.bodyBefore;
                Note("difficulty: extra large maps: streaming: physics bodies %u -> %u over the first %u removals (%d, %d per removal)",
                    g_stream.bodyBefore, after, g_stream.removed, leaked, leaked / (int)g_stream.removed);
            }
            else Note("difficulty: extra large maps: streaming: physics bodies %u after %u removals (the count before the first did not read)",
                after, g_stream.removed);
        }
    }
    float keep = kStreamInRadius * kStreamInRadius;
    float out = kStreamOutRadius * kStreamOutRadius;
    uint32_t order = 0, outs = 0, expired = 0, picks = 0, formationsDue = 0, bodies = 0, slots = 0, exits = 0;
    uint32_t pick[kStreamReplaceBatch];
    float pickKey[kStreamReplaceBatch];
    int budget = WorldStreamBodyStore(&bodies, &slots) && (float)bodies < kStreamFormationShare * (float)slots;
    g_stream.bodiesUsed = bodies; g_stream.bodySlots = slots;
    for (uint32_t i = 0; i < g_stream.count; i++)
    {
        if (!g_streamDeferred[i]) continue;
        float best = kStreamFar;
        for (int p = 0; p < g_stream.playerCount; p++)
        {
            float dx = g_streamAt[i][0] - g_streamPlayerAt[p][0];
            float dy = g_streamAt[i][1] - g_streamPlayerAt[p][1];
            float distance = dx * dx + dy * dy;
            if (distance < best) best = distance;
        }
        if (g_streamLive[i] && g_streamReplace[i]) /* its ground changed: picked below */
        {
            /* not while a layer fill runs: its respawn waits for the fill, and the stamp would be missing until then */
            if (g_streamPoseGood[i] && !g_streamNeedsUnit[i] && g_streamUnit[i] != 0 && !g_streamDrop[i] && !g_layerJob.running)
                WorldStreamReplacePick(pick, pickKey, &picks, i, best / g_streamSeen2[i]);
            continue;
        }
        if (g_streamLive[i] && g_streamFormation[i]) continue; /* a far formation stays */
        if (g_streamLive[i])
        {
            if (g_stream.playerCount > 0 && best > (g_streamOut2[i] > out ? g_streamOut2[i] : out))
            {
                if (g_streamOutSince[i] == 0) g_streamOutSince[i] = now;
            }
            else g_streamOutSince[i] = 0;
            if (g_streamOutSince[i] != 0 && now - g_streamOutSince[i] >= kStreamOutTimeout && g_streamPoseGood[i] && !g_streamNeedsUnit[i] &&
                g_streamUnit[i] != 0)
            {
                expired++;
                /* its timer ran out since the last scan (all of them at the first, scanAt 0): a new exit, counted
                 * whatever the queue's room */
                exits += g_streamOutSince[i] + kStreamOutTimeout > g_stream.scanAt;
                if (!g_streamDrop[i])
                    WorldStreamOutPick(g_streamOutPick, g_streamOutKey, &outs, kStreamDropQueue - kStreamDropReserve, i,
                        best / (g_streamOut2[i] > out ? g_streamOut2[i] : out));
            }
            continue;
        }
        if (g_streamSpawned[i]) continue; /* a record that did not read: never streamed */
        if (best < keep) g_streamDue[i] = 1;
        else if (g_streamFormation[i] && !g_streamDue[i] && budget && g_stream.playerCount > 0 && formationsDue < kStreamFormationsPerScan)
        {
            g_streamDue[i] = 1; /* in the stamps' order (largest apparent size first); the due list then goes nearest first */
            formationsDue++;
        }
        if (g_streamDue[i])
        {
            g_streamDistance[i] = best;
            g_streamOrder[order++] = i;
        }
    }
    for (uint32_t k = 0; k < picks; k++) WorldStreamDropLocked(pick[k], STREAM_DROP_REPLACE);
    WorldStreamOutOrder(g_streamOutPick, g_streamOutKey, outs);
    for (uint32_t k = 0; k < outs && g_streamDropCount < kStreamDropQueue - kStreamDropReserve; k++) WorldStreamDropLocked(g_streamOutPick[k], STREAM_DROP_OUT);
    if (g_stream.scanAt != 0 && now > g_stream.scanAt) /* the forced removal frames' pace (WorldStreamRemoveWait) */
        g_streamPace.exitRate += ((double)exits * 1000.0 / (double)(now - g_stream.scanAt) - g_streamPace.exitRate) * 0.25;
    g_streamPace.exitsSum += exits;
    g_stream.exitsNew = exits; g_stream.exitsChanges = g_stream.changesSinceScan; g_stream.changesSinceScan = 0; g_stream.scanAt = now;
    g_stream.expired = expired;
    if (expired > g_stream.expiredMost) g_stream.expiredMost = expired;
    g_stream.orderCount = order;
    g_stream.orderCursor = 0;
    WorldStreamSort(order);
}

static uint32_t WorldStreamDueCountLocked(void)
{
    uint32_t due = 0;
    for (uint32_t i = 0; i < g_stream.count; i++)
        if (g_streamDeferred[i] && g_streamDue[i] && !g_streamSpawned[i]) due++;
    return due;
}

/* Up to kStreamBatch due records per call, nearest first. The original queues their layers on the engine world's
 * asynchronous spawn queue; a full queue leaves the rest for the next frame (the returned resume index). Its records
 * are copied back (the call may write them), their +0xA8 kept as the real unit. At most `most` locations; calls stop
 * once their time and the units' estimated making (g_streamFlush.slope each) reach `ms`, after kStreamSpawnFloor at
 * least. The calls made and the new stamp units: 0 when no body can have come of it. */
static uint32_t WorldStreamSpawnDueLocked(double ms, uint32_t most)
{
    if (!g_stream.captured || !g_stream.generatorDone || g_stream.deferred == 0 || g_stream.orderCount == 0) return 0;
    if (g_streamSpawn == NULL) return 0;
    uint32_t made = 0;
    if (g_heightCopy == NULL || g_heightSource == 0) /* a layer spawned after generation crashes without the kept copy */
    {
        if (!g_stream.heightBad)
        {
            g_stream.heightBad = 1;
            Note("difficulty: extra large maps: content streaming waits: the generator's layer heightfield was not kept");
        }
        return 0;
    }
    uint8_t* stage = (uint8_t*)g_streamStage;
    int gates[2] = {-1, -1}; /* the near and far budgets for a level location's new unit, read once (WindowRespawnRoomLocked) */
    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);
    g_stream.spawnCut = g_stream.spawnCapped = 0;
    const uint32_t respawned = g_stream.respawned;
    uint32_t cursor = g_stream.orderCursor, queued = 0;
    double elapsed = 0.0;
    while (cursor < g_stream.orderCount)
    {
        if (queued >= most) { g_stream.spawnCapped = 1; g_stream.spawnCaps++; break; }
        uint32_t picked[kStreamBatch], pickedAt[kStreamBatch], count = 0, room = most - queued;
        if (g_streamFlush.slope > 0.0) /* what the time left after the queued ones' making makes, kStreamSpawnFloor at least */
        {
            const double fit = (ms - elapsed - g_streamFlush.slope * queued) / g_streamFlush.slope;
            const uint32_t floor = queued < kStreamSpawnFloor ? kStreamSpawnFloor - queued : 0;
            const uint32_t fits = fit >= 1.0 ? (fit < (double)room ? (uint32_t)fit : room) : 0;
            if (fits < floor) { if (floor < room) room = floor; }
            else if (fits == 0) { g_stream.spawnCut = 1; break; }
            else room = fits;
        }
        /* the due order is nearest first: past the pass's reach the rest wait, the cursor on the first of them */
        while (count < kStreamBatch && count < room && cursor < g_stream.orderCount &&
            !(g_stream.spawnWithin > 0.0f && g_streamDistance[g_streamOrder[cursor]] >= g_stream.spawnWithin))
        {
            uint32_t i = g_streamOrder[cursor];
            if (g_streamSpawned[i]) { cursor++; continue; }
            if (SafeRead((uintptr_t)g_stream.array + (size_t)i * kStreamRecord, stage + (size_t)count * kStreamRecord, kStreamRecord))
            {
                g_streamSpawned[i] = 1;
                g_stream.broken++;
                if (!g_stream.recordBad)
                {
                    g_stream.recordBad = 1;
                    Note("difficulty: extra large maps: content streaming: a location record did not read; it is skipped");
                }
                cursor++;
                continue;
            }
            if (g_streamNeedsUnit[i] && (!WindowRespawnRoomLocked(i, gates) || !WorldStreamRespawnLocked(i)))
            {
                cursor++;
                continue;
            }
            memcpy(stage + (size_t)count * kStreamRecord + 0xA8, &g_streamUnit[i], 8);
            picked[count] = i;
            pickedAt[count] = cursor;
            count++;
            cursor++;
        }
        if (count == 0) { g_stream.orderCursor = cursor; break; }
        uint8_t* oldArray = NULL;
        uint64_t oldCountAt = 0;
        uint32_t oldResume = 0, resume = 0, countAt = count;
        if (SafeRead((uintptr_t)g_stream.pass + 8, &oldArray, 8) || SafeRead((uintptr_t)g_stream.pass + 0x48, &oldCountAt, 8) ||
            SafeRead((uintptr_t)g_stream.pass + 0xD8, &oldResume, 4))
        {
            if (!g_stream.passBad)
            {
                g_stream.passBad = 1;
                Note("difficulty: extra large maps: content streaming: the generator's pass no longer reads; streaming stops for this level");
            }
            break;
        }
        if (oldArray != g_stream.array)
        {
            if (!g_stream.passBad)
            {
                g_stream.passBad = 1;
                Note("difficulty: extra large maps: content streaming: the generator's pass no longer holds the captured location array; streaming stops for this level");
            }
            break;
        }
        /* Every tree of a call takes a slot of the forest's grid, and a full grid crashes the spawn (exe 0x20CF57):
         * the call waits, its records stay due, until removals give room back. */
        uint32_t trees = 0, treeRoom = 0, treesAfter = 0;
        const int counted = WorldStreamTrees(&trees, &treeRoom);
        if (counted && !co_forest_call_fits(trees, treeRoom, g_stream.treesMost))
        {
            if (g_stream.treeHolds++ == 0)
                Note("difficulty: extra large maps: content streaming waits: the SpeedTree forest holds %u trees of %u (the most one call added: %u)",
                    trees, treeRoom, g_stream.treesMost);
            g_stream.orderCursor = pickedAt[0];
            break;
        }
        uint64_t countHere = (uint64_t)(uintptr_t)&countAt;
        uint32_t zero = 0;
        memcpy((void*)((uintptr_t)g_stream.pass + 8), &stage, 8);
        memcpy((void*)((uintptr_t)g_stream.pass + 0x48), &countHere, 8);
        memcpy((void*)((uintptr_t)g_stream.pass + 0xD8), &zero, 4);
        g_streamOwn = (LONG)GetCurrentThreadId();
        g_streamSpawn(g_stream.pass, g_stream.record, g_stream.context);
        g_streamOwn = 0;
        made++;
        if (counted && co_forest_trees(g_stream.forest, &treesAfter, &treeRoom) && treesAfter > trees && treesAfter - trees > g_stream.treesMost)
            g_stream.treesMost = treesAfter - trees;
        if (g_stream.context == g_streamContextCopy) WorldStreamArenaReset();
        if (SafeRead((uintptr_t)g_stream.pass + 0xD8, &resume, 4))
        {
            resume = 0;
            if (!g_stream.resumeBad)
            {
                g_stream.resumeBad = 1;
                Note("difficulty: extra large maps: content streaming: the streamed call's resume index does not read; its locations stay due");
            }
        }
        memcpy((void*)((uintptr_t)g_stream.pass + 8), &oldArray, 8);
        memcpy((void*)((uintptr_t)g_stream.pass + 0x48), &oldCountAt, 8);
        memcpy((void*)((uintptr_t)g_stream.pass + 0xD8), &oldResume, 4);
        if (resume > count) resume = count;
        for (uint32_t k = 0; k < count; k++)
        {
            uintptr_t at = (uintptr_t)g_stream.array + (size_t)picked[k] * kStreamRecord;
            memcpy((void*)at, stage + (size_t)k * kStreamRecord, kStreamRecord);
            if (g_streamDetached[picked[k]])
            {
                uint64_t zero = 0;
                memcpy((void*)(at + 0xA8), &zero, 8); /* a respawned unit stays only in g_streamUnit */
            }
            else memcpy((void*)(at + 0xA8), &g_streamUnit[picked[k]], 8);
            if (k < resume)
            {
                g_streamSpawned[picked[k]] = 1;
                g_streamLive[picked[k]] = 1;
                g_streamLayer[picked[k]] = (uint8_t)g_stream.spawnLayer;
                g_stream.spawned++;
                queued++;
            }
        }
        if (resume < count)
        {
            g_stream.ringFull++;
            g_stream.orderCursor = pickedAt[resume]; /* the queue was full: the rest stay due */
            break;
        }
        g_stream.orderCursor = cursor;
        QueryPerformanceCounter(&now);
        elapsed = g_streamFrequency > 0 ? (double)(now.QuadPart - start.QuadPart) * 1000.0 / (double)g_streamFrequency : 0.0;
        if (elapsed >= ms && queued >= kStreamSpawnFloor) { g_stream.spawnCut = cursor < g_stream.orderCount; break; } /* making: next batch */
    }
    g_stream.spawnedIn += queued;
    QueryPerformanceCounter(&now);
    if (g_streamFrequency > 0)
        g_stream.spentMs += (double)(now.QuadPart - start.QuadPart) * 1000.0 / (double)g_streamFrequency;
    return made + (g_stream.respawned - respawned);
}

/* The game thread's frames since the last note (WorldStreamTick's dt): the cost of what is spawned, as frame rate. */
static uint32_t g_streamFrames;
static float g_streamFrameSum, g_streamFrameMax;

/* The mission's objective instances as the extraction test reads them (0x5D91B0): the manager [game+0x3326DA0], count
 * +0x24, instances [+0x60] + i x 0x1078 with the state at +0x1018 and the class at +0x1038 (classes 2..4 are not
 * waited for; a waited-for one in state 3, or all of them in state 2, opens extraction). Each "class/state". */
static void WorldStreamObjectivesNote(void)
{
    uint64_t manager = 0, data = 0;
    uint32_t count = 0;
    if (g_stream.game == 0 || SafeRead(g_stream.game + 0x3326DA0u, &manager, 8) || manager == 0 || SafeRead(manager + 0x24, &count, 4) ||
        SafeRead(manager + 0x60, &data, 8))
    {
        Note("difficulty: extra large maps: objectives: the objective manager does not read");
        return;
    }
    char list[512], full[512]; int n = 0;
    uint32_t waited = 0, open = 0;
    MissionListsFull(full, sizeof full);
    uint32_t shown[2] = {0, 0}; /* the map's slots and trackers in use when it last updated (map object +0xF948 / +0xF94C) */
    if (g_markerMap != 0) { SafeRead(g_markerMap + 0xF948, &shown[0], 4); SafeRead(g_markerMap + 0xF94C, &shown[1], 4); }
    for (uint32_t i = 0; i < count && i < 64 && data != 0; i++)
    {
        uint32_t state = 0, cls = 0;
        if (SafeRead(data + 0x1078ull * i + 0x1018, &state, 4) || SafeRead(data + 0x1078ull * i + 0x1038, &cls, 4)) break;
        if (!(cls >= 2 && cls <= 4)) { waited++; if (state != 2 && state != 3) open++; }
        if (n < (int)sizeof list - 16) n += snprintf(list + n, sizeof list - n, " %u/%u", cls, state);
    }
    Note("difficulty: extra large maps: objectives: %u instance(s), %u waited for (%u not done); class/state:%s; physics stream swaps under the world's lock so far: %lld; convoy escort spawns stopped at 8 so far: %ld; units past their mission list:%s; patrol spawners left out of GuardForce's groups (no group or a full one): %ld, alarms: %ld; spawner positions past a group's 16: %ld; sample owners past 256: %ld, samples past an owner's 256: %ld; AI tasks %u of 2048 (guard points left out at load: %ld); map markers last shown %u, trackers %u",
        count, waited, open, n ? list : " none", (long long)g_sceneSwapsLocked, g_convoyFull != NULL ? (long)*g_convoyFull : -1L, full[0] ? full : " none", g_garrisonSkipped != NULL ? (long)*g_garrisonSkipped : -1L,
        g_alarmSkipped != NULL ? (long)*g_alarmSkipped : -1L, g_spawnerPlacesFull != NULL ? (long)*g_spawnerPlacesFull : -1L,
        g_sampleGroupsFull != NULL ? (long)g_sampleGroupsFull[0] : -1L, g_sampleGroupsFull != NULL ? (long)g_sampleGroupsFull[1] : -1L,
        TasksUsed(), g_tasksLeftOut != NULL ? (long)*g_tasksLeftOut : -1L, shown[0], shown[1]);
}

static double WorldStreamRemoveRoom(void);
static uint32_t WorldStreamForcedMost(void);
static ULONGLONG WorldStreamRemoveWait(void);
/* The change cost by layer since the last note (g_streamLayerCost), then cleared: per kind each layer that had a clean
 * pair, its count, mean and most ms and its leaves' mean, and the kind's ns a leaf over all its pairs. */
static void WorldStreamLayerCostNote(void)
{
    static const char* const kKind[2] = {"removal", "spawn"};
    char text[900];
    size_t o = 0;
    for (int k = 0; k < 2 && o < sizeof text; k++)
    {
        uint32_t n = 0; double ms = 0.0, leaves = 0.0;
        o += (size_t)snprintf(text + o, sizeof text - o, "%s%s frames:", k ? "; " : "", kKind[k]);
        for (uint32_t l = 0; l < SL_LAYERS && o < sizeof text; l++)
        {
            const LayerCostSum* c = &g_streamLayerCost[k][l];
            if (c->n == 0) continue;
            n += c->n; ms += c->ms; leaves += c->leaves;
            o += (size_t)snprintf(text + o, sizeof text - o, " layer %u %u at %.2f ms (the most %.2f) over %.0f leaves,", l == 0 ? 4u : 5u + l, c->n, c->ms / c->n,
                c->most, c->leaves / c->n);
        }
        if (o < sizeof text && n == 0) o += (size_t)snprintf(text + o, sizeof text - o, " none");
        else if (o < sizeof text) o += (size_t)snprintf(text + o, sizeof text - o, " %.1f ns a leaf over %u", leaves > 0.0 ? ms * 1e6 / leaves : 0.0, n);
    }
    memset(g_streamLayerCost, 0, sizeof g_streamLayerCost);
    Note("difficulty: extra large maps: streaming change cost by the layer it rebuilt in 10 s (a change frame's and its after-change frame's world update over two quiet frames'): %s", text);
}
/* Every 10 s while streaming: what is spawned now, what is due, removals and respawns so far, the players and time,
 * and the frame rate over those 10 s (mean and the slowest frame). */
static void WorldStreamNoteLocked(ULONGLONG now)
{
    if (g_stream.deferred == 0) return;
    if (g_stream.nextNote == 0) { g_stream.nextNote = now + 10000; return; }
    if (now < g_stream.nextNote) return;
    g_stream.nextNote = now + 10000;
    uint32_t due = WorldStreamDueCountLocked();
    uint32_t formations = 0, formationsLive = 0;
    for (uint32_t i = 0; i < g_stream.count; i++) if (g_streamFormation[i]) { formations++; if (g_streamLive[i]) formationsLive++; }
    Note("difficulty: extra large maps: streaming: %u of %u spawned (%u due now, %u skipped; far formations %u of %u), %u removed so far (%u for new ground), %u respawned so far (%u passes held over budget), %d player(s), %.1f ms spent; physics bodies %u of %u; %.1f fps, slowest frame %.1f ms",
        g_stream.spawned, g_stream.deferred, due, g_stream.broken, formationsLive, formations, g_stream.removed, g_stream.replaced, g_stream.respawned, g_stream.respawnHeld, g_stream.playerCount, g_stream.spentMs,
        g_stream.bodiesUsed, g_stream.bodySlots,
        g_streamFrameSum > 0 ? (double)g_streamFrames / (double)g_streamFrameSum : 0.0, (double)g_streamFrameMax * 1000.0);
    g_streamFrames = 0; g_streamFrameSum = g_streamFrameMax = 0;
    char leaves[24] = "not read";
    if (g_stream.staticFrames > 0) snprintf(leaves, sizeof leaves, "%u", g_stream.staticLeaves);
    Note("difficulty: extra large maps: streaming changes: %u change frames in 10 s (%d ms apart at least without a backlog), %u removals queued; %u live stamps past their removal radius for %d s at the last scan (the most %u); static broad phase layer 4: %s leaves, changed in %u of %u frames, %u of them after a frame the streamer changed nothing in, %u while a tile built; layers 6..9 changed in %u frames, %u of them after a frame the streamer changed nothing in",
        g_stream.changes, (int)kStreamChangeGap, g_streamDropCount, g_stream.expired, (int)(kStreamOutTimeout / 1000), g_stream.expiredMost, leaves,
        g_stream.staticMoved, g_stream.staticFrames, g_stream.staticQuiet, g_stream.staticTile, g_stream.streamedMoved, g_stream.streamedQuiet);
    const double frames = g_stream.changes > 0 ? (double)g_stream.changes : 1.0;
    Note("difficulty: extra large maps: streaming change frames: %u of %u with a backlog (every %d frames); budget %.2f ms a frame, %u at its floor of %.1f ms (the quiet frames' longest of 32: mean %.1f ms), %u of those not caused by the quiet frames, %u spawning near stamps only, %u with a due stamp in reach and no layer fill (%u locations spawned in them, %u of them none), the last removal frame removing %u at least (exits %u over %u removal frames); change frame cost with its after-change frame estimated: removal %.2f ms (its %u samples' mean %.2f), spawn %.2f ms (its %u samples' mean %.2f)",
        g_stream.backlogs, g_stream.changes, (int)kStreamChangeTicksBusy, g_stream.budgetSum / frames, g_stream.budgetFloors, kStreamBudgetFloor,
        g_stream.quietHiSum / frames, g_stream.floorStuck, g_stream.floorHeld, g_stream.floorDue, g_stream.floorSpawned, g_stream.floorDry,
        g_stream.drainLeast, g_stream.exitsNew, g_stream.exitsChanges, g_streamCost.removeMs, g_streamPace.removeSamples,
        g_streamPace.removeSamples > 0 ? g_streamPace.removeSum / g_streamPace.removeSamples : 0.0, g_streamCost.spawnMs, g_streamPace.spawnSamples,
        g_streamPace.spawnSamples > 0 ? g_streamPace.spawnSum / g_streamPace.spawnSamples : 0.0);
    Note("difficulty: extra large maps: streaming pace: %u removal frames (%u forced by a %d ms wait, %d ms with a backlog), %u spawn frames; %u change tries held for a far-grass job, %u let through after %d ms; "
        "the quiet frames' longest of 32: mean %.1f ms without the far-grass and after-change frames, %.1f ms with them; a removal's own time %.3f ms estimated (its %u samples' mean %.3f), "
        "a chosen removal frame's room %.2f ms, a forced one's most %u",
        g_streamPace.removeFrames, g_streamPace.removeForced, (int)kStreamRemoveWaitMs, (int)kStreamRemoveWaitBusyMs, g_streamPace.spawnFrames, g_streamPace.grassHolds, g_streamPace.grassForced,
        (int)kStreamGrassHoldMs, g_stream.quietHiSum / frames, g_streamPace.quietHiGrassSum / frames, g_streamCost.ownMs, g_streamPace.ownSamples,
        g_streamPace.ownSamples > 0 ? g_streamPace.ownSum / g_streamPace.ownSamples : 0.0, WorldStreamRemoveRoom(), WorldStreamForcedMost());
    /* keeping up (adv REPORT-3c1ad6b BL3): neither may grow over consecutive windows of a traversal */
    Note("difficulty: extra large maps: streaming keep-up: %u live stamps past their removal radius at the last scan (%+d since the last note), %u removals queued (%+d); "
        "in 10 s %u new exits and %u removals (the exits' pace %.0f a second); the oldest removal forces a frame after %d ms now",
        g_stream.expired, (int)g_stream.expired - (int)g_streamPace.noteExpired, g_streamDropCount, (int)g_streamDropCount - (int)g_streamPace.noteQueued,
        g_streamPace.exitsSum, g_stream.changeRemoved, g_streamPace.exitRate, (int)WorldStreamRemoveWait());
    g_streamPace.noteExpired = g_stream.expired; g_streamPace.noteQueued = g_streamDropCount; g_streamPace.exitsSum = 0;
    uint32_t nearLive = 0, farLive = 0;
    for (uint32_t i = 0; i < g_stream.count && i < kStreamMaxLocations; i++)
    {
        if (!g_streamLive[i]) continue;
        float d2 = kStreamFar;
        for (int p = 0; p < g_stream.playerCount; p++)
        {
            const float dx = g_streamAt[i][0] - g_streamPlayerAt[p][0], dy = g_streamAt[i][1] - g_streamPlayerAt[p][1];
            if (dx * dx + dy * dy < d2) d2 = dx * dx + dy * dy;
        }
        if (d2 <= kStreamOutRadius * kStreamOutRadius) nearLive++; else farLive++;
    }
    Note("difficulty: extra large maps: streaming governor (%s): %s (the quiet frames' world update, a second's mean, after-change frames out: the last %.1f ms; on over %.1f, off under %.1f); "
        "on %u of the last 10 s, turned on %u and off %u times; far ring scans held %u, far respawns held %u, far stamps queued out %u, near ones kept %u times; "
        "the governor's removals not done at the drain, a player within %.0f m: %u; far removals done with a player within it: the governor's %u, the window's near budget's %u; "
        "live stamps within %.0f m %u, past it %u",
        g_streamGovActs ? "acting" : "observing only: would be", g_streamPace.govOn ? "ON" : "off", g_streamPace.govLast, kGovOnMs, kGovOffMs, g_streamPace.govSeconds,
        g_streamPace.govOnTurns, g_streamPace.govOffTurns, g_streamPace.govHeldScans, g_streamPace.govHeldRespawns, g_streamPace.govDrained, g_streamPace.govNearKept,
        (double)kStreamOutRadius, g_streamPace.farNearSkipped, g_streamPace.farNearRemoved[1], g_streamPace.farNearRemoved[0], (double)kStreamOutRadius, nearLive, farLive);
    g_streamPace.removeFrames = g_streamPace.removeForced = g_streamPace.spawnFrames = g_streamPace.grassHolds = g_streamPace.grassForced = 0;
    g_streamPace.removeSum = g_streamPace.spawnSum = 0.0; g_streamPace.removeSamples = g_streamPace.spawnSamples = 0; g_streamPace.quietHiGrassSum = 0.0;
    g_streamPace.ownSum = 0.0; g_streamPace.ownSamples = 0; g_streamPace.farNearSkipped = 0;
    g_streamPace.farNearRemoved[0] = g_streamPace.farNearRemoved[1] = 0;
    g_streamPace.govSeconds = g_streamPace.govOnTurns = g_streamPace.govOffTurns = g_streamPace.govHeldScans = g_streamPace.govHeldRespawns = g_streamPace.govDrained = g_streamPace.govNearKept = 0;
    /* the loader writes about 1000 characters a line (HellForge logging.c WriteLog): the change frames' work apart */
    Note("difficulty: extra large maps: streaming change work: %u removals, %.1f a frame; removals %.2f ms a frame (the most %.2f), spawns %.2f ms (the most %.2f); %u locations spawned, %.1f a frame; their units made in %u flushes, %.2f ms each (the most %.2f) after an animation wait of %.2f ms (the most %.2f), %.3f ms a location and %.2f ms fixed estimated; cut short by the time limit: removals in %u, spawns in %u; by the location count: %u; deferred for a tile step: %u frames of %u a tile built in; %u due not spawned at the last scan; drained layer 4 %u, 6..9 %u %u %u %u; spawned into layer 4 %u, 6..9 %u %u %u %u",
        g_stream.changeRemoved, g_stream.changeRemoved / frames,
        g_stream.drainMs / frames, g_stream.drainMost, g_stream.spawnMs / frames, g_stream.spawnMost, g_stream.spawnedIn, g_stream.spawnedIn / frames,
        g_streamFlush.frames, g_streamFlush.frames > 0 ? g_streamFlush.ms / g_streamFlush.frames : 0.0, g_streamFlush.most,
        g_streamFlush.frames > 0 ? g_streamFlush.waitMs / g_streamFlush.frames : 0.0, g_streamFlush.waitMost, g_streamFlush.slope, g_streamFlush.fixed,
        g_stream.drainCuts, g_stream.spawnCuts, g_stream.spawnCaps, g_stream.tileDefers, g_stream.buildFrames,
        g_stream.orderCount > g_stream.orderCursor ? g_stream.orderCount - g_stream.orderCursor : 0u,
        g_stream.layerDrains[0], g_stream.layerDrains[1], g_stream.layerDrains[2], g_stream.layerDrains[3], g_stream.layerDrains[4],
        g_stream.layerSpawns[0], g_stream.layerSpawns[1], g_stream.layerSpawns[2], g_stream.layerSpawns[3], g_stream.layerSpawns[4]);
    WorldStreamLayerCostNote();
    SlNote();
    StNote();
    g_stream.changes = g_stream.expiredMost = g_stream.staticFrames = g_stream.staticMoved = g_stream.staticQuiet = g_stream.staticTile = 0;
    g_stream.backlogs = g_stream.drainCuts = g_stream.spawnCuts = g_stream.changeRemoved = 0;
    g_stream.spawnCaps = g_stream.tileDefers = g_stream.spawnedIn = 0;
    g_stream.budgetSum = 0.0; g_stream.budgetFloors = g_stream.buildFrames = 0;
    g_stream.floorHeld = 0; g_stream.quietHiSum = 0.0; g_stream.streamedMoved = g_stream.streamedQuiet = 0;
    g_stream.floorDry = g_stream.floorStuck = 0; g_stream.rebuiltSum = 0.0;
    g_stream.floorDue = g_stream.floorSpawned = 0;
    g_streamFlush.frames = 0; g_streamFlush.ms = g_streamFlush.most = 0.0; g_streamFlush.waitMs = g_streamFlush.waitMost = 0.0;
    g_stream.drainMs = g_stream.spawnMs = g_stream.drainMost = g_stream.spawnMost = 0.0;
    memset(g_stream.layerDrains, 0, sizeof g_stream.layerDrains); memset(g_stream.layerSpawns, 0, sizeof g_stream.layerSpawns);
    WindowNoteLocked();
    DestroyNote();
    WorldStreamObjectivesNote();
}

/* The level's world is freed when the mission ends, while g_worldLevel only moves at the next level's init: on the
 * ship a removal's DestroyUnit ran in the gone world 16 s after a leave. So the streamer works only while the game is
 * in a mission (state 4: [[game + 0x3326340] + 0xAC21C], as NearShownLive in difficulty_world_terrain.c). */
static int WorldStreamInMission(void)
{
    uintptr_t gameObj = 0; uint32_t state = 0;
    return !SafeRead(g_stream.game + 0x3326340u, &gameObj, 8) && gameObj != 0 && !SafeRead(gameObj + 0xAC21Cu, &state, 4) && state == 4;
}

/* The level left play for good: the generator went to 4 (0xF70D10, the only way out of 3 but 5) and only a level init
 * follows its 0 (0xF7444F). State 4's update destroys the records' stamp units (0xF742F7..0xF7433D) and the unload every
 * other unit of the world once the state is 0 (0xAB3CD9..0xAB3DCB): every address kept is freed memory or another
 * unit's from here. So all of the level's state goes, its queued removals with it. */
static void WorldStreamLeftLocked(uint32_t state)
{
    if (g_stream.played)
    {
        uint32_t units = 0, flagged = 0;
        for (uint32_t i = 0; i < g_stream.count && i < kStreamMaxLocations; i++)
        {
            units += g_streamLive[i] && g_streamUnit[i] != 0;
            flagged += g_streamLive[i] && g_streamReplace[i] && !g_streamDrop[i];
        }
        Note("difficulty: extra large maps: content streaming: the level left play (generator state %d): streaming stopped; %u stamp units, %u queued removals "
            "and %u more flagged for new ground are forgotten, none is destroyed from here on; %u removed in play, %u given up (their unit was gone); the "
            "world's unit list searched %u times (%.2f ms in all, the longest %.3f ms, %u units at the last); %u looks did not read", (int)state, units,
            g_streamDropCount, flagged, g_stream.removed, g_stream.gone, g_stream.looks, g_stream.lookMs, g_stream.lookMost, g_stream.listed, g_stream.lookFails);
        g_streamLeft.frames = kStreamLeftFrames; g_streamLeft.destroys = g_streamDestroys; g_streamLeft.levels = g_stream.levels;
        TerrainLogState("at the leave");
    }
    WorldStreamClearLocked();
}

/* Some frames after a level left play: the destroys the streamer made since (a run's proof that it stopped). */
static void WorldStreamLeftNoteLocked(void)
{
    if (g_streamLeft.frames == 0 || --g_streamLeft.frames != 0) return;
    Note("difficulty: extra large maps: content streaming: %d frames after level %ld left play: %ld destroy call(s) by the streamer since",
        (int)kStreamLeftFrames, (long)g_streamLeft.levels, (long)(g_streamDestroys - g_streamLeft.destroys));
}

/* A tile's layer change still filling; polled here too (it finishes the change), so the spawns never wait on another
 * caller's poll. Outside the stream lock: the change's end writes notes and descriptors. */
static int WorldStreamLayerBusy(void) { return NearLayerBusy(); }

static uint32_t g_streamTicks; /* WorldStreamTick's calls: two layer reads compare only from one frame to the next */
/* The static bodies' broad phase layer and its leaf count. The mission world's broad phase as the streamed layers
 * read it (SlWorld: [W+0x4C0], vtable +0x38 0xD40550); its layers are an array of pointers [bp+0x10], count [bp+0x18]
 * (0xD3C6B0..0xD3C72A). The layer rule (0xDB9E60) puts static bodies in layer 4; an add (0xD3CFD0) appends a 16-byte
 * leaf, size [L+0x50], capacity [L+0x54]. */
static int WorldStreamStaticLeaves(uint32_t* leaves)
{
    uintptr_t w = 0, bp = 0, layers = 0, l4 = 0;
    int32_t count = 0; uint32_t capacity = 0;
    if (!SlWorld(ApiModuleBase("helldivers2.exe", NULL), &w, &bp)) return 0;
    if (SafeRead(bp + 0x18, &count, 4) || count < 5 || count > 64 || SafeRead(bp + 0x10, &layers, 8) || layers == 0 ||
        SafeRead(layers + 4 * 8, &l4, 8) || l4 == 0 || SafeRead(l4 + 0x50, leaves, 4) || SafeRead(l4 + 0x54, &capacity, 4)) return 0;
    return *leaves <= (capacity & 0x3FFFFFFFu);
}

/* Each tick, before the change step: whether the static layer's leaves moved since the last tick, and whether that
 * tick's change step made any change. Moves after a tick with none come from someone else, so the change frames do not
 * hold all the static layer's rebuilds. */
static void WorldStreamStaticLocked(void)
{
    uint32_t leaves = 0, sl[SL_LAYERS], sum = 0;
    if (WorldStreamStaticLeaves(&leaves))
    {
        if (g_stream.staticAt != 0 && g_stream.staticAt + 1 == g_streamTicks && leaves != g_stream.staticLeaves)
        {
            g_stream.staticMoved++;
            if (!g_stream.changedLast) g_stream.staticQuiet++;
            /* a tile's bodies go in during the engine's world update after the step: seen here in the step's frame + 1 */
            g_stream.staticTile += MoveBuildRecent() != 0;
        }
        g_stream.staticFrames++;
        g_stream.staticAt = g_streamTicks; g_stream.staticLeaves = leaves;
    }
    /* layers 6..9 moved after a frame the streamer changed nothing in: a rebuild out of its budget, as when a tile
     * destroys stamp units a change frame's flush made (they took the frame's layer) */
    if (!SlLeaves(sl)) return;
    for (uint32_t k = 1; k < SL_LAYERS; k++) sum += sl[k];
    if (g_stream.streamedAt != 0 && g_stream.streamedAt + 1 == g_streamTicks && sum != g_stream.streamedLeaves)
    {
        g_stream.streamedMoved++;
        if (!g_stream.changedLast) g_stream.streamedQuiet++;
    }
    g_stream.streamedAt = g_streamTicks; g_stream.streamedLeaves = sum;
}

/* The change frame's one layer (SlPick) from the queued removals and the leaves, and that layer's leaves. -1 and 0
 * when the broad phase has no streamed layers: everything static is in layer 4, whose leaves (the last read) the
 * frame's step rebuilds. */
static void WorldStreamLayersLocked(int due, int* drain, uint32_t* spawn, uint32_t* rebuilt)
{
    uint32_t leaves[SL_LAYERS] = {0}, queued[SL_LAYERS] = {0};
    *drain = -1; *spawn = 0; *rebuilt = g_stream.staticFrames > 0 ? g_stream.staticLeaves : 0;
    if (!SlLeaves(leaves)) return;
    for (uint32_t n = 0; n < g_streamDropCount; n++)
    {
        const uint32_t i = g_streamDropList[(g_streamDropHead + n) % kStreamDropQueue];
        const uint32_t layer = i < g_stream.count ? g_streamLayer[i] : 0;
        queued[layer >= 6 && layer < 6 + SL_LAYERS - 1 ? layer - 5 : 0]++;
    }
    SlPick(leaves, queued, due, &g_stream.pickTurn, drain, spawn);
    *rebuilt = leaves[*drain >= 6 ? *drain - 5 : 0];
}

/* The change frame's time for its removals, spawns and their making: the frame target less the longest of the recent
 * quiet frames, the rebuild of the frame's layer and this frame's tile step, within kStreamBudgetFloor..Most. The
 * quiet frames hold the game's own world flush, so only the streamer's own work is counted here. */
static double WorldStreamBudget(double quietHi, double rebuildMs, double moveMs)
{
    if (!(quietHi > 0.0)) return kStreamChangeMs; /* not measured yet in this play */
    const double t = CO_FC_TARGET_MS - quietHi - rebuildMs - moveMs;
    return t < kStreamBudgetFloor ? kStreamBudgetFloor : t > kStreamBudgetMost ? kStreamBudgetMost : t;
}

/* A change frame's cost (removal or spawn frame), learned once its after-change frame closed: the two frames' world
 * update over two quiet frames', a quarter of the estimate (the frame after one ran 2.5-4.8 ms over the change frames,
 * run 2c, and was taken for a quiet one). A pair a far-grass job was out in or kept apart says nothing of it, nor do
 * quiet frames not measured yet; one sample is held to 4 x the estimate (1 ms at least, 12 at most). */
static void WorldStreamCostLearn(void)
{
    const int kind = g_streamCost.kind;
    if (kind == 0) return;
    CoFcRecent r;
    co_framecost_recent_all(&r);
    if (r.lastBodies != 0) return; /* the change frame itself just closed: the next frame is its after-change frame */
    g_streamCost.kind = 0;
    if (!r.lastAfter || r.lastAfterKind != kind || !r.pairClean || !(r.quietHi > 0.0) || !(r.quietWorld > 0.0)) return;
    double* est = kind == 1 ? &g_streamCost.removeMs : &g_streamCost.spawnMs;
    double most = *est * 4.0;
    most = most < 1.0 ? 1.0 : most > 12.0 ? 12.0 : most;
    double ms = r.pairWorld - 2.0 * r.quietWorld;
    ms = ms < 0.0 ? 0.0 : ms;
    if (g_streamCost.layer < SL_LAYERS)
    {
        LayerCostSum* c = &g_streamLayerCost[kind == 1 ? 0 : 1][g_streamCost.layer];
        c->n++; c->ms += ms; c->leaves += g_streamCost.leaves; if (ms > c->most) c->most = ms;
    }
    ms = ms > most ? most : ms;
    *est = *est * 0.75 + ms * 0.25;
    if (kind == 1) { g_streamPace.removeSum += ms; g_streamPace.removeSamples++; }
    else { g_streamPace.spawnSum += ms; g_streamPace.spawnSamples++; }
}

/* The spawns' units wait on the world's async spawn ring: they are made in this frame, after g_streamLock is released
 * (WorldStreamChangeAfter), with the animation wait a destroy has too. Nothing pending when the world interface does
 * not read (the world's own flush makes them later). */
static void WorldStreamFlushLaterLocked(uint32_t locations)
{
    uint64_t world = 0;
    uintptr_t wi = 0, table = 0, wait = 0;
    if (SafeRead((uintptr_t)g_stream.context + 0x40, &world, 8) || world == 0 || SafeRead(g_stream.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 ||
        SafeRead(wi + DIF_STREAM_TABLE, &table, 8) || table == 0 || (wait = WorldStreamAnimWait(table)) == 0) { SlFrameEnd(0); return; }
    g_streamFlush.pending = 1; g_streamFlush.world = world; g_streamFlush.wait = wait; g_streamFlush.locations = locations;
}

/* A flush's ms against its locations, into a line fitted over the recent flushes (each older one weighs 7/8 of the
 * next): the slope is a location's making. While the locations hardly vary the mean ms a location stands in (over by
 * the fixed part); a line that falls or crosses 0 is a burst of the game's own entries, and the slope stays. */
static void WorldStreamFlushCost(double ms, uint32_t locations)
{
    g_streamFlush.frames++; g_streamFlush.ms += ms;
    if (ms > g_streamFlush.most) g_streamFlush.most = ms;
    if (locations == 0) return;
    const double n = (double)locations, k = 0.875;
    g_streamFlush.sw = g_streamFlush.sw * k + 1.0; g_streamFlush.sn = g_streamFlush.sn * k + n; g_streamFlush.sm = g_streamFlush.sm * k + ms;
    g_streamFlush.snn = g_streamFlush.snn * k + n * n; g_streamFlush.snm = g_streamFlush.snm * k + n * ms;
    const double mn = g_streamFlush.sn / g_streamFlush.sw, mm = g_streamFlush.sm / g_streamFlush.sw;
    const double var = g_streamFlush.snn / g_streamFlush.sw - mn * mn, cov = g_streamFlush.snm / g_streamFlush.sw - mn * mm;
    const double slope = var > 0.25 && cov > 0.0 ? cov / var : 0.0, fixed = mm - slope * mn;
    if (slope > 0.0 && fixed >= 0.0) /* a quarter of the mean a location at least: a slope from noise would let n run to the caps */
    {
        g_streamFlush.slope = slope > 0.25 * mm / mn ? slope : 0.25 * mm / mn;
        g_streamFlush.fixed = mm - g_streamFlush.slope * mn;
    }
    else if (!(var > 0.25) || g_streamFlush.slope <= 0.0) { g_streamFlush.slope = mm / mn; g_streamFlush.fixed = 0.0; }
}

/* After the tick released g_streamLock: the change frame's flush, timed. Its fitted slope is the next frames' estimate
 * of a location's making (WorldStreamFlushCost), so a frame's spawns and their making stay in its budget. */
static void WorldStreamChangeAfter(void)
{
    if (!g_streamFlush.pending) return;
    g_streamFlush.pending = 0;
    /* the game's own wait (game.dll 0xAB61AD) comes later in the frame: what this one waits is overlap lost */
    const double w0 = HitchNow();
    ((WorldStreamAnimWaitFn)g_streamFlush.wait)(g_streamFlush.world);
    const double t0 = HitchNow();
    g_streamFlush.waitMs += t0 - w0; if (t0 - w0 > g_streamFlush.waitMost) g_streamFlush.waitMost = t0 - w0;
    g_hitchCost.spawn += (float)(t0 - w0);
    SlFrameEnd(g_streamFlush.world);
    const double ms = HitchNow() - t0;
    g_hitchCost.spawn += (float)ms;
    WorldStreamFlushCost(ms, g_streamFlush.locations);
}

/* The window holds its design: the live stamps past their removal radius at the last scan are no more than one change
 * frame's removals. A change frame at its floor spawns only then, or with no removal queued. */
static int WorldStreamWindowHolds(void) { return g_stream.expired <= kStreamChangeDrops || g_streamDropCount == 0; }

/* A removal frame's drain time over its removals is a removal's own time (the look, DestroyUnit's own part, the slot's
 * leave), learned a quarter of the way; one sample is held to 2 ms. */
static void WorldStreamOwnLearn(double ms, uint32_t removed)
{
    if (removed == 0) return;
    double per = ms / (double)removed;
    per = per < 0.0 ? 0.0 : per > 2.0 ? 2.0 : per;
    g_streamCost.ownMs += (per - g_streamCost.ownMs) * 0.25;
    g_streamPace.ownSum += per; g_streamPace.ownSamples++;
}

/* A chosen removal frame's room under the target: the learned step cost and kStreamRemoveGather removals' own time. */
static double WorldStreamRemoveRoom(void) { return g_streamCost.removeMs + kStreamRemoveGather * g_streamCost.ownMs; }

/* A forced removal frame's most: kStreamRemoveForcedOwnMs of learned own time, in [kStreamRemoveGather,
 * kStreamRemoveForcedMost]; 24 at run 2c's 0.31 ms. Such a frame costs the frame before, the learned step cost and at
 * most max(kStreamRemoveForcedOwnMs, kStreamRemoveGather own times): about 10.4 ms over the frame before in run 2c. */
static uint32_t WorldStreamForcedMost(void)
{
    const double n = g_streamCost.ownMs > 0.0 ? kStreamRemoveForcedOwnMs / g_streamCost.ownMs : (double)kStreamRemoveForcedMost;
    return n < (double)kStreamRemoveGather ? kStreamRemoveGather : n > (double)kStreamRemoveForcedMost ? kStreamRemoveForcedMost : (uint32_t)n;
}

/* The removals due: those queued, or the live stamps past their removal radius when more (they wait for queue room). */
static uint32_t WorldStreamBacklog(void) { return g_streamDropCount > g_stream.expired ? g_streamDropCount : g_stream.expired; }

/* A forced removal frame's minimum: the backlog within [kStreamRemoveGather, WorldStreamForcedMost]. The exits' pace
 * comes in through the queue (each scan queues its new exits) and through the forced frames' wait. */
static uint32_t WorldStreamDrainLeast(void)
{
    const uint32_t most = WorldStreamForcedMost(), n = WorldStreamBacklog();
    return n < kStreamRemoveGather ? kStreamRemoveGather : n > most ? most : n;
}

/* The oldest removal's wait that forces a removal frame: kStreamRemoveWaitBusyMs while the backlog is past one forced
 * frame's most or the exits' pace past one such frame each kStreamRemoveWaitMs, else kStreamRemoveWaitMs. Forced frames
 * alone then keep up with WorldStreamForcedMost a kStreamRemoveWaitBusyMs (240 a second at fps 40 and up: a removal
 * frame takes 4 frames with the spawns' turn), so neither the queue nor the live stamps past their radius grow below
 * that pace. */
static ULONGLONG WorldStreamRemoveWait(void)
{
    const uint32_t most = WorldStreamForcedMost();
    const int busy = WorldStreamBacklog() > most || g_streamPace.exitRate * (double)kStreamRemoveWaitMs / 1000.0 > (double)most;
    return busy ? (ULONGLONG)kStreamRemoveWaitBusyMs : (ULONGLONG)kStreamRemoveWaitMs;
}

/* A due stamp not yet spawned within the squared reach (0: any) from the cursor: the order is nearest first. */
static int WorldStreamDueInReach(float reach2)
{
    for (uint32_t k = g_stream.orderCursor; k < g_stream.orderCount; k++)
    {
        const uint32_t i = g_streamOrder[k];
        if (g_streamSpawned[i]) continue;
        return !(reach2 > 0.0f) || g_streamDistance[i] < reach2;
    }
    return 0;
}

/* A spawn frame at its floor the quiet frames did not cause: with the starting spawn cost (kStreamSpawnMsStart)
 * instead of the learned one it would have had 1.5 ms. Many of them say the learned estimate holds them down. */
static int WorldStreamFloorStuck(double quietHi, uint32_t leaves, double moveMs)
{
    (void)leaves;
    return quietHi > 0.0 && CO_FC_TARGET_MS - quietHi - moveMs - kStreamSpawnMsStart >= 1.5;
}

/* Whether this change try is a removal frame: 0 no, 1 chosen (the frame before left WorldStreamRemoveRoom under the
 * target, and kStreamRemoveGather are queued or the oldest waited kStreamRemoveGatherMs), 2 forced (the oldest waited
 * WorldStreamRemoveWait, whatever the frames). After any removal frame, a stalled far-grass job's too, a spawn frame's
 * turn when spawns wait: a forced pick ahead of it starved the spawns for a stalled job's 20 s. `lastWhole` 0: not
 * measured yet. */
static int WorldStreamRemovePick(ULONGLONG now, double lastWhole, int spawnsWait)
{
    if (g_streamDropCount == 0) return 0;
    if (spawnsWait && g_streamPace.lastKind == 1) return 0;
    const ULONGLONG waited = g_streamPace.removeWaitAt != 0 && now > g_streamPace.removeWaitAt ? now - g_streamPace.removeWaitAt : 0;
    if (waited >= WorldStreamRemoveWait()) return 2;
    if (lastWhole >= CO_FC_TARGET_MS - WorldStreamRemoveRoom()) return 0; /* 0, not measured: room */
    if (g_streamDropCount < kStreamRemoveGather && waited < kStreamRemoveGatherMs) return 0;
    return 1;
}

/* The smallest streamed layer (6..9) for a spawn frame, 0 when the broad phase has none (the split off): every
 * spawn frame fills the emptiest, so the layers stay near a quarter each. */
static uint32_t WorldStreamSpawnLayer(void)
{
    uint32_t leaves[SL_LAYERS] = {0}, small = 1;
    if (!SlLeaves(leaves)) return 0;
    for (uint32_t k = 2; k < SL_LAYERS; k++) if (leaves[k] < leaves[small]) small = k;
    return 5u + small; /* index 1..4 is layer 6..9 */
}

/* At most one frame each kStreamChangeGap ms, or every kStreamChangeTicksBusy frames with a backlog of removals or
 * spawns, adds or removes stamp bodies; never while a far-grass bake job is out (kStreamGrassHoldMs at most). A
 * removal frame only removes, in the layer with the most queued, as many as fit its frame's room (the target less the
 * frame before and the learned removal cost), WorldStreamRemovePick's minimum at least; a spawn frame only spawns, in
 * the budget the quiet frames leave (WorldStreamBudget with the learned spawn cost), past the window's design only the
 * nearest few. A try that changed nothing leaves the next frame free to try. The changes made. */
static uint32_t WorldStreamChangeLocked(ULONGLONG now, int layerBusy)
{
    WorldStreamCostLearn();
    g_stream.buildFrames += MoveStepMsNow() > 0.0; /* a tile builds in this frame */
    if (g_streamDropCount == 0) g_streamPace.removeWaitAt = 0;
    else if (g_streamPace.removeWaitAt == 0) g_streamPace.removeWaitAt = now;
    if (now < g_stream.nextChange || g_streamTicks < g_stream.nextChangeTick) return 0;
    const uint32_t pending = g_stream.orderCount > g_stream.orderCursor ? g_stream.orderCount - g_stream.orderCursor : 0;
    /* a moving tile's heavy build step this frame or the last: the change frame waits a frame, so it never adds to a slice */
    if (MoveStepRecent()) { g_stream.tileDefers += pending > 0 || g_streamDropCount > 0; return 0; }
    /* the level update (0xAB519E) runs the far-grass tick before this one (0xAB612D): a job it put out is out in this
     * frame's world update (0xAB6235) */
    int grassForced = 0;
    if (FarGrassBusy())
    {
        if (g_streamPace.grassSince == 0) g_streamPace.grassSince = now;
        if (now - g_streamPace.grassSince < kStreamGrassHoldMs) { g_streamPace.grassHolds += pending > 0 || g_streamDropCount > 0; return 0; }
        grassForced = 1;
    }
    else g_streamPace.grassSince = 0;
    const int backlog = pending > kStreamBacklogDue || g_streamDropCount > kStreamChangeDrops || g_stream.expired > 2u * kStreamChangeDrops;
    CoFcRecent fc;
    co_framecost_recent_all(&fc);
    const double quietHi = fc.quietHi;
    const int removal = WorldStreamRemovePick(now, fc.lastWhole, pending > 0 && !layerBusy);
    g_stream.drainCut = g_stream.spawnCut = g_stream.spawnCapped = 0;
    int drain = -1; uint32_t spawn = 0, rebuilt = 0, made = 0, changed = 0;
    double ms;
    int room = removal != 2, held = 0, floorDue = 0; /* a forced removal frame is one at its floor */
    const uint32_t removed = g_stream.removed, spawned = g_stream.spawnedIn;
    const double t0 = HitchNow();
    if (removal)
    {
        /* the layer with the most queued (no turn; layer 4 when the split is off) */
        WorldStreamLayersLocked(0, &drain, &spawn, &rebuilt); /* its spawn layer is not used: no spawns here */
        ms = fc.lastWhole > 0.0 ? CO_FC_TARGET_MS - fc.lastWhole - g_streamCost.removeMs : kStreamChangeMs;
        ms = ms < 0.0 ? 0.0 : ms > kStreamBudgetMost ? kStreamBudgetMost : ms;
        const uint32_t least = removal == 2 ? WorldStreamDrainLeast() : kStreamRemoveGather;
        g_stream.drainLeast = least;
        const double d0 = HitchNow();
        changed = WorldStreamDrainLocked(backlog ? kStreamChangeDropsBusy : kStreamChangeDrops, ms, drain, least);
        WorldStreamOwnLearn(HitchNow() - d0, changed);
    }
    else
    {
        if (pending == 0 || layerBusy) return 0;
        g_streamPace.lastKind = 2; /* the spawns' turn is taken, made or not: the next try may be a removal frame */
        rebuilt = g_stream.staticFrames > 0 ? g_stream.staticLeaves : 0;
        spawn = WorldStreamSpawnLayer();
        if (spawn != 0)
        {
            uint32_t leaves[SL_LAYERS] = {0};
            if (SlLeaves(leaves)) rebuilt = leaves[spawn - 5];
        }
        ms = WorldStreamBudget(quietHi, g_streamCost.spawnMs, MoveStepMsNow());
        room = ms > kStreamBudgetFloor;
        /* past the window's design only the nearest due inside kStreamInRadius, kStreamSpawnFloor of them */
        held = !room && !WorldStreamWindowHolds();
        const uint32_t most = held ? kStreamSpawnFloor : backlog ? kStreamChangeSpawnsBusy : kStreamChangeSpawns;
        const float reach = held ? kStreamInRadius * kStreamInRadius : 0.0f;
        floorDue = !room && WorldStreamDueInReach(reach);
        g_stream.spawnLayer = spawn;
        /* the makers' sampler counts bodies the spawn call itself makes (its forest) as this change frame's */
        const uint32_t tickWas = g_slChangeTick;
        g_slChangeTick = g_streamTicks;
        SlFrameBegin(spawn);
        g_stream.spawnWithin = reach;
        made = WorldStreamSpawnDueLocked(ms, most);
        if (!made) g_slChangeTick = tickWas;
        if (made && spawn) WorldStreamFlushLaterLocked(g_stream.spawnedIn - spawned); else SlFrameEnd(0);
        g_stream.spawnLayer = 0;
        changed = made;
    }
    g_stream.spawnTried = removal ? 0u : spawn + 1u;
    const double t1 = HitchNow();
    if (removal) { g_hitchCost.stream += (float)(t1 - t0); g_stream.drainMs += t1 - t0; if (t1 - t0 > g_stream.drainMost) g_stream.drainMost = t1 - t0; }
    else { g_hitchCost.spawn += (float)(t1 - t0); g_stream.spawnMs += t1 - t0; if (t1 - t0 > g_stream.spawnMost) g_stream.spawnMost = t1 - t0; }
    g_hitchCost.removed += (uint16_t)(g_stream.removed - removed);
    if (changed == 0) return 0;
    /* spawns held by a layer fill come back soon too, so tile builds do not cost spawn frames; so do queued removals
     * under the busy wait, which a kStreamChangeGap between change frames would hold to 4 forced frames a second */
    const int soon = backlog || (layerBusy && pending > 0) ||
        (g_streamDropCount > 0 && WorldStreamRemoveWait() == (ULONGLONG)kStreamRemoveWaitBusyMs);
    g_stream.nextChange = soon ? now : now + kStreamChangeGap;
    g_stream.nextChangeTick = soon ? g_streamTicks + kStreamChangeTicksBusy : 0;
    g_streamRebuild.leaves = rebuilt;
    g_streamCost.kind = removal ? 1 : 2;
    g_streamCost.layer = removal ? (drain >= 6 ? (uint32_t)drain - 5u : 0u) : (spawn >= 6 ? spawn - 5u : 0u);
    g_streamCost.leaves = rebuilt;
    g_streamPace.lastKind = removal ? 1 : 2;
    if (removal) { g_streamPace.removeFrames++; g_streamPace.removeForced += removal == 2; g_streamPace.removeWaitAt = g_streamDropCount ? now : 0; g_stream.changesSinceScan++; }
    else g_streamPace.spawnFrames++;
    if (grassForced) { g_streamPace.grassForced++; g_streamPace.grassSince = now; } /* one change each kStreamGrassHoldMs while a job stalls */
    g_stream.changes++;
    g_stream.budgetSum += ms; g_stream.budgetFloors += !room; g_stream.floorHeld += held; g_stream.quietHiSum += quietHi;
    g_streamPace.quietHiGrassSum += fc.quietHiGrass;
    g_stream.floorDue += floorDue; g_stream.floorDry += floorDue && made == 0;
    g_stream.floorSpawned += floorDue ? g_stream.spawnedIn - spawned : 0u;
    g_stream.floorStuck += !removal && !room && WorldStreamFloorStuck(quietHi, rebuilt, MoveStepMsNow());
    g_stream.rebuiltSum += rebuilt;
    g_stream.backlogs += backlog != 0; g_stream.drainCuts += g_stream.drainCut != 0; g_stream.spawnCuts += g_stream.spawnCut != 0;
    g_stream.changeRemoved += g_stream.removed - removed;
    if (g_stream.removed != removed) g_stream.layerDrains[drain >= 6 ? drain - 5 : 0]++;
    if (made) g_stream.layerSpawns[spawn >= 6 ? spawn - 5 : 0]++;
    co_framecost_bodies(room ? CO_FC_BODIES_ROOM : CO_FC_BODIES_FLOOR); /* a removal frame: room when chosen, the floor when forced */
    co_framecost_change(g_stream.removed != removed, made != 0);
    g_slChangeTick = g_streamTicks;
    return changed;
}

/* Each tick in play: the frame just closed into the governor's second when it was a quiet frame (no change, not right
 * after one, no far-grass job, not kept apart); each kGovWindowMs with kGovMinFrames such frames, its mean turns the
 * governor on over kGovOnMs and off under kGovOffMs, kGovHoldMs after its last turn at least. Its state stays between
 * the two. It acts only when asked (WorldStreamGovernActs); else its turns are counted, the would-be ones. */
static void WorldStreamGovernLocked(ULONGLONG now)
{
    CoFcRecent fc;
    co_framecost_recent_all(&fc);
    if (fc.lastWhole > 0.0 && fc.lastBodies == 0 && !fc.lastAfter && !fc.lastGrass && !fc.lastApart) { g_streamPace.govSum += fc.lastWorld; g_streamPace.govN++; }
    if (g_streamPace.govAt == 0) { g_streamPace.govAt = now; return; }
    if (now - g_streamPace.govAt < kGovWindowMs) return;
    g_streamPace.govAt = now;
    g_streamPace.govSeconds += g_streamPace.govOn != 0;
    if (g_streamPace.govN >= kGovMinFrames)
    {
        const double mean = g_streamPace.govSum / g_streamPace.govN;
        g_streamPace.govLast = mean;
        const int may = g_streamPace.govTurnAt == 0 || now - g_streamPace.govTurnAt >= kGovHoldMs;
        if (may && !g_streamPace.govOn && mean > kGovOnMs) { g_streamPace.govOn = 1; g_streamPace.govOnTurns++; g_streamPace.govTurnAt = now; }
        else if (may && g_streamPace.govOn && mean < kGovOffMs) { g_streamPace.govOn = 0; g_streamPace.govOffTurns++; g_streamPace.govTurnAt = now; }
    }
    g_streamPace.govSum = 0.0; g_streamPace.govN = 0;
}

/* The governor on and asked to act (g_streamGovActs): the far ring held, its far respawns held, its farthest evicted. */
static int WorldStreamGovernActs(void) { return g_streamPace.govOn && g_streamGovActs; }

/* A far window slot the governor may take out: live with its own unit, not queued, not a mission's, and farther than
 * kStreamOutRadius from every player (the near stamps' own keep radius: those are never touched). */
static int WorldStreamGovernMay(uint32_t s)
{
    if (g_streamOut2[s] <= 0.0f || !g_streamLive[s] || !g_streamPoseGood[s] || g_streamNeedsUnit[s] || g_streamUnit[s] == 0 || g_streamDrop[s] || g_streamMission[s])
        return 0;
    if (WorldStreamWithinOut(s)) { g_streamPace.govNearKept++; return 0; }
    return 1;
}

/* Called from TickDetour (packages.c) every frame: nothing until a level is captured and the generator's op is done,
 * nothing once g_worldLevel is another level (all state is dropped), nothing while the world is back to vanilla. */
/* A moving tile's 0x63 job drains in this frame's render callback, after this tick (A: difficulty_world_terrain.c sets
 * stampLeave at the push and clears it at the next poll once drained). Its long frame (about 185 ms) is kept out of
 * the quiet frames and the change frames; the change frame is not deferred for it. 1 when kept apart. */
static int WorldStreamRenderApart(void)
{
    if (!g_near.stampLeave || g_near.step < 0 || g_near.step >= NEAR_STEPS || g_near.stepOp[g_near.step] != 0x63) return 0;
    co_framecost_apart();
    return 1;
}

static void WorldStreamTick(float dt)
{
    if (g_streamInstallState != 1) return;
    WorldStreamRenderApart();
    g_streamTicks++;
    g_layerGameThread = GetCurrentThreadId();
    DestroyTick(); /* destroys that waited for their unit (difficulty_world_destroy.c) */
    if (dt > 0 && dt < 10.0f) { g_streamFrames++; g_streamFrameSum += dt; if (dt > g_streamFrameMax) g_streamFrameMax = dt; }
    if (g_streamInside) return; /* a generator call is inside: it owns the pass */
    const int layerBusy = WorldStreamLayerBusy();
    AcquireSRWLockExclusive(&g_streamLock);
    WorldStreamLeftNoteLocked();
    if (!g_stream.captured)
    {
        ReleaseSRWLockExclusive(&g_streamLock);
        return;
    }
    const uint32_t state = WorldStreamGenerator();
    if (g_worldLevelLive != g_stream.level || g_stream.levels != g_world.liveLevels) /* the mission in play, not a preview */
    {
        WorldStreamLeftLocked(state);
        ReleaseSRWLockExclusive(&g_streamLock);
        return;
    }
    if (state != 3) /* no scan, removal, window work or spawn; nothing is destroyed */
    {
        /* 5 is a second pipeline in play, back to 3 after it (0xF7451E); before play the level is still generating; a
         * state that does not read says nothing about the level, so the tick only waits */
        if (state != 5 && state != kStreamStateUnread && g_stream.played) WorldStreamLeftLocked(state);
        ReleaseSRWLockExclusive(&g_streamLock);
        return;
    }
    g_stream.played = 1;
    if (g_stream.deferred == 0 || g_streamInside || !g_stream.generatorDone || !(g_world.live > 1.0f) || !WorldStreamInMission())
    {
        ReleaseSRWLockExclusive(&g_streamLock);
        return;
    }
    ULONGLONG now = GetTickCount64();
    const uint32_t entered = WindowEntered();
    const double t0 = HitchNow();
    WorldStreamScanLocked(now);
    const double t1 = HitchNow();
    WorldStreamGovernLocked(now);
    {
        CoFcRecent fc;
        co_framecost_recent_all(&fc);
        StFrame(fc.lastBodies ? fc.lastKind : 0);
    }
    WindowScanLocked(now); /* the planned stamps near the players into the window (difficulty_stream_window.c) */
    const double t2 = HitchNow();
    WorldStreamStaticLocked();
    g_stream.changedLast = WorldStreamChangeLocked(now, layerBusy) != 0;
    g_hitchCost.stream += (float)(t1 - t0); g_hitchCost.window += (float)(t2 - t1);
    g_hitchCost.entered += (uint16_t)(WindowEntered() - entered);
    WorldStreamNoteLocked(now);
    ReleaseSRWLockExclusive(&g_streamLock);
    WorldStreamChangeAfter();
    /* Removal and respawn run in WorldStreamChangeLocked, called above on this thread under this lock and only when no
     * generator call is inside; only a location this streamer queued is removed. */
}

/* The rule and body hooks with the streamer, and the line that says whether the main world has the layers. */
static void WorldStreamLayersInstall(void)
{
    const char* layers = SlInstall(ApiModuleBase("helldivers2.exe", NULL));
    if (layers != NULL)
    {
        Note("difficulty: extra large maps: streamed broad phase layers off: %s; every static body stays in layer 4", layers);
#if defined(CO_RESEARCH) || defined(CO_TEST)
        const char* sampler = SlSamplerInstall(ApiModuleBase("helldivers2.exe", NULL));
        if (sampler != NULL) Note("difficulty: extra large maps: the bodies' makers are not sampled: %s", sampler);
#endif
    }
    else Note("difficulty: extra large maps: streamed broad phase layers on: the main world's broad phase has %d layers, 6..9 as layer 4, and each change frame's stamp bodies go to one of them", SlMainLayers());
}

/* NULL when the hook is in (or the level is not extra large, so there is nothing to stream), else why it is not. */
static const char* InstallWorldStream(uintptr_t game)
{
    if (g_streamInstallState == 1) return NULL;
    if (g_streamInstallState < 0) return "the extra large maps' content streamer is not installed";
    if (WorldScaleFor(g_difficulty.mapScale) <= 1u) return NULL; /* a vanilla world: no location to defer */
    if (game == 0) return "game.dll is not loaded (content streaming needs it)";
    if (!ApiCodeMatches(game + DIF_STREAM_SPAWN, kStreamSpawnPin))
    {
        g_streamInstallState = -1;
        Note("difficulty: extra large maps: content streaming off: SpawnLevelLayers (0x%X) is not the reviewed one", (unsigned)DIF_STREAM_SPAWN);
        return "SpawnLevelLayers is not the reviewed one (content streaming needs it)";
    }
    if (LoaderHook("extra large maps' content streamer", game + DIF_STREAM_SPAWN, (void*)WorldStreamSpawn, (void**)&g_streamSpawn))
    {
        g_streamInstallState = -1;
        Note("difficulty: extra large maps: content streaming off: SpawnLevelLayers could not be hooked");
        return "SpawnLevelLayers could not be hooked for content streaming";
    }
    LARGE_INTEGER frequency;
    frequency.QuadPart = 0;
    QueryPerformanceFrequency(&frequency);
    g_streamFrequency = frequency.QuadPart;
    g_stream.game = game;
    g_streamInstallState = 1;
    InstallDestroyQueue();
    MatResInstall();
    MatWaitInstall();
    TerrainSplitInstall();
    WorldStreamLayersInstall();
#if defined(CO_RESEARCH) || defined(CO_TEST)
    const char* timers = StInstall(ApiModuleBase("helldivers2.exe", NULL));
    Note("difficulty: extra large maps: step removal timers %s%s", timers ? "off: " : "on (removeBodies, broad phase remove, renumber; activations counted)", timers ? timers : "");
#endif
#ifdef CO_RESEARCH
    {
        char t[4] = "";
        g_streamGovActs = GetEnvironmentVariableA("HELLFORGE_XL_GOVERNOR", t, sizeof t) > 0 && t[0] == '1';
    }
#endif
    Note("difficulty: extra large maps: streaming governor %s", g_streamGovActs ? "acts (HELLFORGE_XL_GOVERNOR=1): it holds and evicts the far ring while on"
        : "observes only: it counts the turns it would make (a research build with HELLFORGE_XL_GOVERNOR=1 makes it act)");
    Note("difficulty: extra large maps: content streams: %d locations at most, %d per call, kept within %.0f m of the start, streamed within %.0f m of a player, removed beyond %.0f m after %d s",
        (int)kStreamMaxLocations, (int)kStreamBatch, (double)kStreamStartRadius, (double)kStreamInRadius, (double)kStreamOutRadius,
        (int)(kStreamOutTimeout / 1000));
    return NULL;
}

/* The install step (difficulty_install.c's kInstallSteps): InstallWorldStream takes the game module. It is declared
 * there before the steps and runs after InstallWorld; NULL when installed or nothing streams, else why. */
static const char* InstallWorldStreamStep(DifficultyInstall* d)
{
    return InstallWorldStream(d->game);
}

/* Research export (Lua): make the streamer treat world-space (x, y) as one more player position, or take it away.
 * The state is set and cleared under g_streamLock; it survives level changes until turned off. 0 when the streamer is
 * not installed. */
static int hellforge_research_stream_observer(int on, float x, float y)
{
    AcquireSRWLockExclusive(&g_streamLock);
    if (g_streamInstallState != 1)
    {
        ReleaseSRWLockExclusive(&g_streamLock);
        return 0;
    }
    if (on != 0)
    {
        int changed = !g_streamObserverOn || x != g_streamObserverAt[0] || y != g_streamObserverAt[1];
        g_streamObserverOn = 1;
        g_streamObserverAt[0] = x;
        g_streamObserverAt[1] = y;
        if (changed) Note("research: stream observer at %.1f, %.1f", (double)x, (double)y);
    }
    else
    {
        int changed = g_streamObserverOn;
        g_streamObserverOn = 0;
        g_streamObserverAt[0] = 0.0f;
        g_streamObserverAt[1] = 0.0f;
        if (changed) Note("research: stream observer off");
    }
    ReleaseSRWLockExclusive(&g_streamLock);
    return 1;
}

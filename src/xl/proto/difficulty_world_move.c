/* Extra large maps: the detailed terrain follows the players. The drop tile (difficulty_world_terrain.c) is built while
 * the level generates; this file builds more of them while the mission runs, on a grid of cells 512 coarse samples
 * (2048.4 m) wide anchored at the drop tile's square (the ring of every cell is dipped at load, NearRingGrid), and
 * releases them when no player comes near.
 *
 * Where: after the level update (game 0xF73F10, rcx = level, called once a frame from 0xAB519E), in state 3
 * ([level+0x1E4]), on the game thread: the place the level's own main-thread generation records run from in state 1
 * (0xF73F10 -> 0xF26140 -> the dispatcher 0xC14950). The level's records and its context stay valid all mission
 * ([level+0x2A8], freed only by the level's destructor); the job queue [ctx+0x50] = game+0xAC570 is drained every frame
 * by the render callback (0xAB7680 -> 0xAB0590), and the merge's request ring level+0xB20 by the level update (0xFCE9A0).
 *
 * One tile at a time:
 *  1. the merge package 0x5157EF2020B48DCD: [api+0x10]+0x310 (resident) and, when not, +0x2F0 (acquire), polled until
 *     resident (as op 0x43 does, 0xF61E8B..0xF61EA9; released by the level's 0x67 at 0xC17CF7);
 *  2. a copy of the context (0xE8 bytes) whose settings (+8) point at a copy of the level's settings (0xD0 bytes, level
 *     +0x2D20 up to the context's next field level+0x2DF0): the replay's composite tiles rewrite the world size and
 *     scatter radii there (TerrainBuild), which gameplay reads through the level's own context;
 *  3a. the stamp terrains of the locations within reach of the tile: op 0x2F (0xF44380) on a staging copy of their
 *     records (pass+8 the records, [pass+0x28] the count, +0x48 the resume index; it waits for its own spawns through
 *     [api+8]+0x48 before it moves on), so 0x63 draws them; 0x65 (0xF53D20) takes them away after the replay on the
 *     same staging (mode 0: the records' +0xA8 units; mode 1: the unit list pass+0x10, [pass+0x38], packed: its loop
 *     does not test for NULL), units the content streamer removed meanwhile left out;
 *  3. op 0x37 (0xFCEB80(ring, R): the merge's textures, generators and gui re-created, the flags R+0xE8.. having been
 *     reset by the level's 0x66/0x67), then 0x38 (0xF48DA0: cameras, shading environment slot 5; phase 1 waits for the
 *     ring to drain), each through the dispatcher on a copy of the level's record, the generation arena (ctx+0)
 *     reset after each (0x173BAB0, as 0xF26140 does after a finished op);
 *  4. the drop tile's replay (TerrainNearReplay from NEAR_START, its plan made again for the cell) with craters held
 *     (the deformation renders into whatever is published under the two heightmap names, which hold the tile's
 *     textures while it is replayed, and pushes its readback into the coarse collision): the deformation's update
 *     (0x1384BE0) and render (0x1385C30) are skipped while held; their records wait in their queue;
 *  5. the merge torn down as the level's 0x66 / 0x67 do it (0xC17B34.., 0xC17CC2..): the cameras ([api+8]+0x60 on R+8,
 *     R+0x18), R+0x108 ([api+8]+0x478), 0x94's lookup, 0xFCF2F0(R, world), 0x1009890(ctx+0x98, 5), the four textures
 *     and names of 0x67, the package ([api+0x10]+0x300) when it was acquired here;
 *  6. the spawn (InitSpawnTerrain on the level's kept 0x69 record), the finalize, the coarse collision lowered and the
 *     coarse drawing hidden over the union of the live tiles (NearHideUnion).
 * A tile goes when no player has been within MOVE_KEEP of its square for MOVE_KEEP_MS, a build or not: removed from the ground
 * queries, the coarse collision raised back from the level's CPU copy (lg+0xD8), the hidden nodes shown again, the
 * terrain destroyed ([api+8]+0x1E0 -> the thunk 0x336AD0 -> exe 0x1ADEA0) and its textures released a few frames later. The drop tile stays.
 *
 * Players: every Helldiver (as the content streamer reads them), the streamer's research observer, and the test eye
 * (HELLFORGE_XL_EYE) while it is on and within 30 m of the ground. HELLFORGE_XL_MOVE=0 turns this off. */

#define MOVE_LEVEL_UPDATE 0xF73F10u
#define MOVE_CRATER_UPDATE 0x1384BE0u
#define MOVE_CRATER_RENDER 0x1385C30u
static const char kMoveUpdatePin[] = "40 55 53 56 57 41 54 48 8D AC 24 40 FC FF FF 48 81 EC C0 04 00 00";
static const char kMoveCraterUpdatePin[] = "4C 8B DC 55 41 55 49 8D AB 78 F6 FF FF 48 81 EC 78 0A 00 00";
static const char kMoveCraterRenderPin[] = "40 55 56 41 56 41 57 48 8D AC 24 38 FF FF FF 48 81 EC C8 01 00 00";
#define MOVE_PACKAGE 0x5157EF2020B48DCDull
#define MOVE_WANT 600.0f      /* a cell is wanted when a player is this close to its square */
#define MOVE_KEEP 1300.0f     /* a tile is kept while a player is this close to its square ... */
#define MOVE_KEEP_MS 30000u   /* ... or was, this long ago */
#define MOVE_CAP 6            /* tiles at most (the drop tile counted); ~128 MiB GPU and ~150 MiB CPU each */
#define MOVE_HOLD_MS 60000u   /* craters are never held longer than this */
#define MOVE_SKIP_MS 120000u  /* a cell whose build failed waits this long */

enum { MV_IDLE, MV_PACKAGE, MV_STAMPS, MV_KICK, MV_SETUP, MV_LOOKUP, MV_REPLAY, MV_TEARDOWN, MV_UNSTAMP, MV_SPAWN, MV_READY, MV_FAILING };
#define MOVE_STAMPS_MAX 32768u /* the plan's stamps in reach too (difficulty_stream_window.c): a tile at vanilla density */
#define MOVE_STAMP_MARGIN 512.0f /* locations this far past the tile's texels: a stamp's terrain reaches ~256 m from it */
#define MOVE_STAMP_REACH 256.0f /* a stamp's terrain reaches about this far from it: these go in first */
#define MOVE_UNSTAMP_MS 2.0 /* game-thread time a frame for the stamp units' destroys */
#define MOVE_SELECT_MS 2.0  /* game-thread time a frame for the stamp units' spawns (MoveStampsSelect) */
/* MoveStampsSelect's place between frames: the table index, then the plan's pass, bucket row/column and entry. */
typedef struct { int on, plan; LONG level; uint32_t i; int pass, j, ii; uint32_t e, planned, past, pastInner, failed, frames; double ms, longest; } MoveSel;

typedef void (*MoveUpdateFn)(uint64_t level);
typedef void (*MoveCraterUpdateFn)(void* system, float dt, uint64_t r8, uint64_t r9);
typedef void (*MoveCraterRenderFn)(void* system, uint64_t rdx, uint64_t r8, uint64_t r9);
static MoveUpdateFn g_moveUpdate;
static MoveCraterUpdateFn g_moveCraterUpdate;
static MoveCraterRenderFn g_moveCraterRender;
static volatile LONG g_moveInstalled;
static volatile LONG64 g_moveHoldSince; /* craters wait while non-zero and younger than MOVE_HOLD_MS */
static volatile LONG64 g_moveHeldUpdates, g_moveHeldRenders;

static struct
{
    LONG level; int on, state, slot, cell[2], acquired, merge;
    uintptr_t levelPtr;
    float origin[2], centre[2];
    uint32_t at2F, at37, at38, at4C, at66, frames, waits, calls, quiet;
    int pkgPending; uint32_t pkgFrames;
    uint32_t nodeOld; /* [R+0x18] when the merge began: a camera node destroyed before, never this build's */
    int cutNoted;     /* MoveBuildLeft has ended or noted this build */
    ULONGLONG waitSince; /* MV_READY's current wait on another thread began (0: none): bounded from here */
    int shipSeen;     /* the ship was reached with this build left: its stamp units are never destroyed after that */
    int r108Out;      /* MoveTeardown's R+0x108: 0 none, 1 destroyed, 2 not in the world's list any more */
    uint32_t ringWaits; /* frames the stamp units' destroy waited for the async spawn ring */
    int failState; /* the state a build failed in (MoveFailRest destroys the new tile's terrain from MV_SPAWN on) */ /* the merge package's release, MOVE_PKG_FRAMES after the teardown that gave it up */
    /* the stamp terrains of the tile's locations (0x2F spawns them for 0x63 to draw, 0x65 takes them away): staging
     * copies of their records, the real indices, their units, the counts the two ops read through pointers */
    uint8_t* stLoc; uint32_t* stIndex; uint64_t* stUnit; uint32_t stCount, stUnits, stTemp; int stamped;
    uint32_t stGone, stFrames; double stMs, stLongest; /* the stamp units' destroys so far, spread over frames */
    MoveSel sel; /* the spawns so far, spread over frames */
    ULONGLONG since, started;
    uint8_t ctx[0xE8];
    uint8_t settings[0xD0];
    uint8_t one[0x19B0];
    uint8_t phase;
    /* the drop tile's maps and handle while a tile builds (g_near / g_matband hold the new tile's) */
    int stashed; float dropCentre[2]; uint64_t dropHandle, dropTex[2], dropBand; uint32_t* dropBandPixels; int dropBandServed, dropBandFramed; uint32_t dropBandFrame;
    uint32_t pixelsFreed; /* band pixels given back once their textures were uploaded (MoveBandPixelsTick) */
    int built, failed, released;
    uint32_t drainWaits; /* frames a due release has waited for the render queue to drain, the build's step with it */
    LONGLONG destroyTicks; /* the last release's destroy alone (QPC ticks), -1 none */
    struct { int a, b; ULONGLONG until; } skip[16];
    struct { int used; uint64_t tex[2], band; uint32_t* pixels; uint32_t frames; } freeing[NEAR_TILES_MAX * 2];
    float player[EVT_MAX_PLAYERS + 2][2];
    int players;
} g_move = {.level = -1};
/* A tile change's stamp flags, waiting for its grids (MoveReplaceLater). */
static struct { int on, slot, hasGone; NearTile gone; float cx, cy; const char* why; } g_moveReplace;

static int FarGrassBusy(void); /* difficulty_world_fargrass.c */
static void FarGrassTick(uint64_t level);
static void FarGrassLevelLeft(void);
static void FarGrassInstall(uintptr_t game);
static void FgGroundTick(uint64_t level); /* difficulty_world_fargrass_ground.c */
static void FgGroundLevelLeft(void);

static void MoveCraterUpdate(void* system, float dt, uint64_t r8, uint64_t r9)
{
    LONG64 since = g_moveHoldSince;
    if (since != 0 && GetTickCount64() - (ULONGLONG)since < MOVE_HOLD_MS) { InterlockedIncrement64(&g_moveHeldUpdates); return; }
    g_moveCraterUpdate(system, dt, r8, r9);
}

static void MoveCraterRender(void* system, uint64_t rdx, uint64_t r8, uint64_t r9)
{
    LONG64 since = g_moveHoldSince;
    if (since != 0 && GetTickCount64() - (ULONGLONG)since < MOVE_HOLD_MS) { InterlockedIncrement64(&g_moveHeldRenders); return; }
    g_moveCraterRender(system, rdx, r8, r9);
}

static void MoveHold(int on)
{
    InterlockedExchange64(&g_moveHoldSince, on ? (LONG64)GetTickCount64() : 0);
}

/* The textures of a tile that went, released once the frames that may still draw it are past. */
static void MoveFreeLater(uint64_t tex0, uint64_t tex1, uint64_t band, uint32_t* pixels)
{
    for (int k = 0; k < NEAR_TILES_MAX * 2; k++)
        if (!g_move.freeing[k].used)
        {
            g_move.freeing[k].used = 1; g_move.freeing[k].tex[0] = tex0; g_move.freeing[k].tex[1] = tex1; g_move.freeing[k].band = band;
            g_move.freeing[k].pixels = pixels; g_move.freeing[k].frames = 0;
            return;
        }
    Note("difficulty: moving tiles: no room to release a tile's textures; they stay allocated");
}

static void MoveFreeNow(uint64_t tex0, uint64_t tex1, uint64_t band, uint32_t* pixels)
{
    if (pixels != NULL && (const uint8_t*)pixels == g_matCopy.from) NearMatCopyFinish(); /* the spawn's readback reads them */
    NearReleaseFn release = (NearReleaseFn)NearApi(0x10, 0x198);
    typedef void (*DestroyFn)(uint64_t tex);
    DestroyFn destroy = (DestroyFn)NearApi(0x10, 0x6B0);
    if (release != NULL) { if (tex0 != 0) release(tex0); if (tex1 != 0) release(tex1); }
    if (band != 0 && destroy != NULL) destroy(band);
    if (pixels != NULL && (band == 0 || destroy != NULL)) VirtualFree(pixels, 0, MEM_RELEASE);
}

/* The merge package goes some frames after the teardown, never in the same call (run s84b: exe+0x504F42 on the render
 * thread: a kind-7 render object's destroy, still in the world's command list, freed the constant blocks of a shader
 * program that the package's material bring-out had already freed (0x51B4B0 via 0x282EC0, queued by 0x168470);
 * the level's own 0x66 destroys and 0x67 releases are separate ops; static rendercrash504). A build that starts while
 * the release is pending takes the package back instead of asking for it again. */
#define MOVE_PKG_FRAMES 8u
static void MovePackageTick(int now)
{
    if (!g_move.pkgPending || (!now && ++g_move.pkgFrames < MOVE_PKG_FRAMES)) return;
    typedef void (*PkgFn)(uint64_t hash);
    PkgFn pkgRelease = (PkgFn)NearApi(0x10, 0x300);
    if (pkgRelease != NULL) pkgRelease(MOVE_PACKAGE);
    g_move.pkgPending = 0;
}
static void MovePackageLater(void)
{
    if (!g_move.acquired) return;
    g_move.acquired = 0;
    if (g_move.pkgPending) { MovePackageTick(1); } /* one pending at most: the older goes now (its frames are mostly past) */
    g_move.pkgPending = 1; g_move.pkgFrames = 0;
}

static void MoveFreeTick(int all)
{
    MovePackageTick(all);
    if (!all) MoveBandPixelsTick(); /* the live tiles' band pixels wait on render frames too */
    for (int k = 0; k < NEAR_TILES_MAX * 2; k++)
        if (g_move.freeing[k].used && (all || ++g_move.freeing[k].frames >= 4))
        {
            MoveFreeNow(g_move.freeing[k].tex[0], g_move.freeing[k].tex[1], g_move.freeing[k].band, g_move.freeing[k].pixels);
            g_move.freeing[k].used = 0;
        }
}

/* The drop tile's maps back in g_near / g_matband, the build's flags off. */
static void MoveRestoreDrop(void)
{
    if (g_move.stashed)
    {
        g_near.tex[0] = g_move.dropTex[0]; g_near.tex[1] = g_move.dropTex[1]; g_near.handle = g_move.dropHandle;
        g_near.centre[0] = g_move.dropCentre[0]; g_near.centre[1] = g_move.dropCentre[1];
        MoveBandUnstash();
        g_move.stashed = 0;
    }
    g_near.state = NEAR_IN;
    InterlockedExchange(&g_moveBuilding, 0);
    MoveHold(0);
}

/* A new level starts (TerrainNearReplay's first call): the last level's tile terrains went at its end (NearTilesDestroy),
 * so their maps go now; one still in the world keeps them (NearTilesLeft). A build that end cut short was ended there
 * too (MoveBuildLeft); what is reset here is the mod's own state. */
static void MoveFrameLeft(void);
static void MoveLevelGone(void)
{
    NearLayerSettle();
    NearTilesLeft();
    if (g_move.state != MV_IDLE && g_move.stashed)
        MoveFreeNow(g_move.dropTex[0] == g_near.tex[0] ? 0 : g_move.dropTex[0], g_move.dropTex[1] == g_near.tex[1] ? 0 : g_move.dropTex[1],
            g_move.dropBand == g_matband.tex ? 0 : g_move.dropBand, g_move.dropBand == g_matband.tex ? NULL : g_move.dropBandPixels);
    g_move.stashed = 0;
    MoveFreeTick(1);
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].tex[0] != 0 || g_tiles[k].tex[1] != 0 || g_tiles[k].band != 0)
        {
            MoveFreeNow(g_tiles[k].tex[0], g_tiles[k].tex[1], g_tiles[k].band, g_tiles[k].bandPixels);
            g_tiles[k].tex[0] = g_tiles[k].tex[1] = g_tiles[k].band = 0; g_tiles[k].bandPixels = NULL;
        }
    g_move.state = MV_IDLE; g_move.level = -1; g_move.on = 0;
    g_move.stTemp = g_move.stGone = 0; g_move.stamped = 0;
    g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f; /* else this level's own 0x63 would look 20 km off */
    g_moveReplace.on = 0;
    SplitClear();
    InterlockedExchange(&g_moveBuilding, 0);
    MoveHold(0);
    MoveFrameLeft();
    NearAutoForget();
}

static int MoveOn(void)
{
    static int on = -1;
    if (on < 0) { char t[4] = ""; on = !(GetEnvironmentVariableA("HELLFORGE_XL_MOVE", t, sizeof t) > 0 && t[0] == '0'); }
    return on;
}

/* A player passing in a hellpod builds no tile on the way: the place held before counts until the pod has landed. */
static CoSettle g_moveSettle;
static void MovePlayers(void)
{
    g_move.players = 0;
    EvpPlayer players[EVT_MAX_PLAYERS];
    int count = EvpPlayers(g_world.game, players);
    const ULONGLONG now = GetTickCount64();
    co_settle_level(&g_moveSettle, g_world.levels);
    for (int i = 0; count > 0 && i < count && g_move.players < EVT_MAX_PLAYERS; i++)
    {
        if (players[i].avatar == 0) continue;
        ActHealth h;
        float at[3] = {0.0f, 0.0f, 0.0f};
        if (!ActHealthOf(g_world.game, players[i].avatar, &h) || h.unit == 0 || !EvpUnitPosition(h.unit, at) || !isfinite(at[0]) || !isfinite(at[1])) continue;
        co_settle_at(&g_moveSettle, (uint32_t)i, at, now, g_move.player[g_move.players]);
        g_move.players++;
    }
    if (g_streamObserverOn) { g_move.player[g_move.players][0] = g_streamObserverAt[0]; g_move.player[g_move.players][1] = g_streamObserverAt[1]; g_move.players++; }
    if (g_eye.on) /* the test eye walks like a player: not when it looks from high above (elevated views) */
    {
        float ground = NearEyeGround(g_eye.pos[0], g_eye.pos[1]);
        if (ground < -9000.0f || g_eye.pos[2] - ground < 30.0f)
        { g_move.player[g_move.players][0] = g_eye.pos[0]; g_move.player[g_move.players][1] = g_eye.pos[1]; g_move.players++; }
    }
}

static void MoveCellCentre(int a, int b, float* c)
{
    c[0] = g_move.origin[0] + (float)a * NEAR_HIDE; c[1] = g_move.origin[1] + (float)b * NEAR_HIDE;
}

/* Metres from (x, y) to the square of cell (a, b) (0 inside). */
static float MoveCellDistance(int a, int b, float x, float y)
{
    float c[2];
    MoveCellCentre(a, b, c);
    float dx = fmaxf(0.0f, fabsf(x - c[0]) - NEAR_HIDE / 2), dy = fmaxf(0.0f, fabsf(y - c[1]) - NEAR_HIDE / 2);
    return sqrtf(dx * dx + dy * dy);
}

static int MoveCellValid(int a, int b) { return a >= g_ring.a0 && a <= g_ring.a1 && b >= g_ring.b0 && b <= g_ring.b1; }

static int MoveCellLive(int a, int b)
{
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].cell[0] == a && g_tiles[k].cell[1] == b) return 1;
    return 0;
}

static int MoveCellSkipped(int a, int b, ULONGLONG now)
{
    for (int k = 0; k < 16; k++) if (g_move.skip[k].until > now && g_move.skip[k].a == a && g_move.skip[k].b == b) return 1;
    return 0;
}

static void MoveCellSkip(int a, int b, ULONGLONG now)
{
    int at = 0;
    for (int k = 1; k < 16; k++) if (g_move.skip[k].until < g_move.skip[at].until) at = k;
    g_move.skip[at].a = a; g_move.skip[at].b = b; g_move.skip[at].until = now + MOVE_SKIP_MS;
}

/* The tile's stamp terrains are drawn 20 km north of their places, where no terrain stands: mid-mission the merge's 0x63
 * render (a data-driven request, 0xABA7FC8A, set up by 0x37 at 0xFCF1AE) draws the live terrains into the heights as
 * well (run s72k: its delta over the tile was the level's surface, corr 0.978, slope 0.99). Each staged location gets a
 * stamp unit of its own (SpawnUnit, the world interface's +0x380, as the streamer respawns one) from the resource and
 * pose the streamer saved at capture ([[loc+0x90]+8], unit+0x20), its translation (row 3) + MOVE_STAMP_OFFSET in y; 0x2F
 * spawns its stamp terrains there, 0x63 sees them through the cameras moved by the same offset (an orthographic view: the
 * heights are the same), 0x65 takes them away, and the units are destroyed (+0x388). The real stamp units, their
 * buildings and the streamer's state are not touched. */
#define MOVE_STAMP_OFFSET 20000.0f
static uint64_t MoveWorld(void);

/* The world's async spawn ring is empty: with r9 = [world+0x20], *[r9+8] == *[r9+0x10] (the test 0x338960 makes at
 * 0x338974..0x33898F before it drains). A level destroyed while its units still wait there skips them (0xD9970 at
 * 0xD9B8A / 0xD9D6A: the handle resolves to no unit yet) and the drain makes them later as orphans with bodies no level
 * removes (KB xl-world-streaming, subagent deadbody). 1 empty, 0 not, -1 unreadable. */
static int MoveSpawnRingEmpty(uint64_t world)
{
    uint64_t ring = 0, at8 = 0, at10 = 0, a = 0, b = 0;
    if (world == 0 || SafeRead(world + 0x20, &ring, 8) || ring == 0 || SafeRead(ring + 8, &at8, 8) || SafeRead(ring + 0x10, &at10, 8) ||
        at8 == 0 || at10 == 0 || SafeRead(at8, &a, 8) || SafeRead(at10, &b, 8)) return -1;
    return a == b;
}

/* MV_UNSTAMP / MV_FAILING: the stamp units go once the spawn ring is empty (at most 600 frames' wait). */
static int MoveStampUnitsReady(void)
{
    if (g_move.stTemp == 0 || MoveSpawnRingEmpty(MoveWorld()) != 0) return 1;
    if (++g_move.ringWaits <= 600) return 0;
    Note("difficulty: moving tiles: the async spawn ring did not empty in 600 frames; the tile's stamp units go now");
    return 1;
}

/* 1 once every stamp unit is gone. Each destroy scans the world's unit list and its second list (exe 0x3389C3,
 * 0x1B47E0), so a tile's 30000 in one frame held it for about 240 ms; a frame gives them `budget` ms. */
static int MoveStampUnitsDestroy(double budget)
{
    if (g_move.stGone >= g_move.stTemp) { g_move.stTemp = g_move.stGone = 0; g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f; return 1; }
    uint64_t world = MoveWorld();
    uintptr_t wi = 0, table = 0, destroy = 0;
    if (world == 0 || SafeRead(g_world.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 || SafeRead(wi + DIF_STREAM_TABLE, &table, 8) || table == 0 ||
        SafeRead(table + DIF_STREAM_DESTROY_ENTRY, &destroy, 8) || destroy == 0) { g_move.stTemp = g_move.stGone = 0; return 1; }
    /* the animation wait first, as every game destroy (difficulty_stream.c, DIF_STREAM_ANIM_WAIT_ENTRY); this runs before
     * the async kick today (the level-update hook, game 0xAB519E), where the wait returns at once */
    uintptr_t wait = WorldStreamAnimWait(table);
    if (wait == 0) { Note("difficulty: near terrain: the tile's stamp units kept: the animation wait is not the reviewed one"); g_move.stTemp = g_move.stGone = 0; return 1; }
    ((WorldStreamAnimWaitFn)wait)(world);
    LARGE_INTEGER hz, t0, t;
    QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    const LONGLONG limit = budget >= 1e6 ? INT64_MAX : (LONGLONG)(budget * (double)hz.QuadPart / 1000.0); /* 1e30: no limit */
    uint32_t k = g_move.stGone;
    do
    {
        if (g_move.stUnit[k] != 0) ((WorldStreamDestroyUnitFn)destroy)(world, g_move.stUnit[k], 0, 0);
        QueryPerformanceCounter(&t);
    } while (++k < g_move.stTemp && t.QuadPart - t0.QuadPart < limit);
    const double ms = (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    g_move.stGone = k; g_move.stFrames++; g_move.stMs += ms;
    if (ms > g_move.stLongest) g_move.stLongest = ms;
    if (k < g_move.stTemp) return 0;
    Note("difficulty: moving tiles: the tile's %u stamp units went over %u frames (%.0f ms in all, the longest frame %.1f ms)", g_move.stTemp,
        g_move.stFrames, g_move.stMs, g_move.stLongest);
    g_move.stTemp = g_move.stGone = g_move.stFrames = 0; g_move.stMs = g_move.stLongest = 0.0;
    g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f;
    return 1;
}

/* The locations whose stamps reach the cell's tile (their record xy +0 in generator metres, world + half), each with a
 * stamp unit of its own at the offset, copied for 0x2F. A tile's ~32768 spawns took 83-120 ms in one frame, so a frame
 * makes them within `budget` ms (at least one) and the next goes on from the same place: the same units in the same
 * order. 1 done (g_move.stCount final), 0 more next frame, -1 the level's stamp data went in between. */
static int MoveStampsSelect(uint64_t locs, uint32_t count, float half, double budget)
{
    MoveSel* s = &g_move.sel;
    if (!s->on)
    {
        g_move.stCount = 0; g_move.stUnits = 0; g_move.stTemp = 0; g_move.stGone = 0;
        if (g_move.stLoc == NULL)
        {
            g_move.stLoc = (uint8_t*)VirtualAlloc(NULL, (size_t)MOVE_STAMPS_MAX * 0xE0, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            g_move.stIndex = (uint32_t*)VirtualAlloc(NULL, (size_t)MOVE_STAMPS_MAX * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            g_move.stUnit = (uint64_t*)VirtualAlloc(NULL, (size_t)MOVE_STAMPS_MAX * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (g_move.stLoc == NULL || g_move.stIndex == NULL || g_move.stUnit == NULL) return 1;
        }
        if (!g_stream.captured || g_stream.level != g_worldLevel || g_stream.levels != g_world.levels)
        { Note("difficulty: moving tiles: the streamer has no stamp poses for this level; the tile gets no stamp terrains"); return 1; }
        memset(s, 0, sizeof *s);
        s->on = 1; s->level = g_world.levels; s->plan = g_plan.level == g_world.levels && g_plan.count != 0; s->j = -1;
    }
    else if (g_world.levels != s->level || !g_stream.captured || g_stream.level != g_worldLevel || g_stream.levels != s->level ||
        (s->plan && g_plan.level != s->level))
    { s->on = 0; return -1; }
    uint64_t world = MoveWorld();
    uintptr_t wi = 0, table = 0, spawn = 0;
    if (world == 0 || SafeRead(g_world.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 || SafeRead(wi + DIF_STREAM_TABLE, &table, 8) || table == 0 ||
        SafeRead(table + DIF_STREAM_SPAWN_ENTRY, &spawn, 8) || spawn == 0)
    { const int some = g_move.stTemp != 0; s->on = 0; return some ? -1 : 1; }
    LARGE_INTEGER hz, t0, t;
    QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    const LONGLONG limit = budget >= 1e6 ? INT64_MAX : (LONGLONG)(budget * (double)hz.QuadPart / 1000.0);
    uint32_t made = 0;
#define MOVE_SEL_YIELD() do { if (made != 0) { QueryPerformanceCounter(&t); if (t.QuadPart - t0.QuadPart >= limit) goto frame; } } while (0)
    const float reach = NEAR_SIDE / 2 + MOVE_STAMP_MARGIN;
    uint8_t rec[0xE0];
    const uint32_t tableEnd = g_win.on && g_win.level == g_stream.level ? g_win.base : count; /* window slots come from the plan below */
    for (; s->i < count && s->i < tableEnd && s->i < kStreamMaxLocations && g_move.stCount < MOVE_STAMPS_MAX; s->i++)
    {
        const uint32_t i = s->i;
        if (!g_streamPoseGood[i] || g_streamResource[i] == 0 || SafeRead(locs + (uint64_t)i * 0xE0, rec, sizeof rec)) continue;
        float at[2];
        memcpy(at, rec, 8);
        if (!isfinite(at[0]) || !isfinite(at[1])) continue;
        if (fabsf(at[0] - half - g_move.centre[0]) > reach || fabsf(at[1] - half - g_move.centre[1]) > reach) continue;
        MOVE_SEL_YIELD();
        float pose[16];
        memcpy(pose, g_streamPose[i], sizeof pose);
        pose[13] += MOVE_STAMP_OFFSET;
        uint64_t unit = ((WorldStreamSpawnUnitFn)spawn)(world, g_streamResource[i], pose, 0, 0);
        made++;
        if (unit == 0) { s->failed++; continue; }
        memcpy(rec + 0xA8, &unit, 8);
        memcpy(g_move.stLoc + (size_t)g_move.stCount * 0xE0, rec, sizeof rec);
        g_move.stIndex[g_move.stCount] = i; g_move.stUnit[g_move.stUnits++] = unit;
        g_move.stCount++; g_move.stTemp = g_move.stUnits;
    }
    s->i = kStreamMaxLocations; /* the table is done (also when the room filled) */
    /* the plan's stamps in reach (g_plan: buckets of the map in generator metres), with op 0x31's pose (WindowPose); the
     * cursor (pass, j, ii, e) resumes the loops where the last frame stopped, -1 / UINT32_MAX = enter that loop afresh */
    if (s->plan)
    {
        const float cell = g_plan.cell, gx = g_move.centre[0] + half, gy = g_move.centre[1] + half;
        int i0 = (int)floorf((gx - reach) / cell), i1 = (int)floorf((gx + reach) / cell), j0 = (int)floorf((gy - reach) / cell), j1 = (int)floorf((gy + reach) / cell);
        /* pass 0: the stamps whose terrain reaches the tile (MOVE_STAMP_REACH past its texels); pass 1: the rest of the
         * margin. A full room then leaves out only the outer margin, never a stamp that shapes the tile. */
        const float inner = NEAR_SIDE / 2 + MOVE_STAMP_REACH;
        for (; s->pass < 2; s->pass++, s->j = -1)
        {
            if (s->j < 0) { s->j = j0 < 0 ? 0 : j0; s->ii = -1; }
            for (; s->j <= j1 && s->j < STAMP_PLAN_GRID; s->j++, s->ii = -1)
            {
                if (s->ii < 0) { s->ii = i0 < 0 ? 0 : i0; s->e = UINT32_MAX; }
                for (; s->ii <= i1 && s->ii < STAMP_PLAN_GRID; s->ii++, s->e = UINT32_MAX)
                {
                    const int b = s->j * STAMP_PLAN_GRID + s->ii;
                    if (s->e == UINT32_MAX) s->e = g_plan.start[b];
                    for (; s->e < g_plan.start[b + 1]; s->e++)
                    {
                        const uint8_t* loc = g_stamps.cand[s->e].loc;
                        float at[2]; memcpy(at, loc, 8);
                        const float dx = fabsf(at[0] - half - g_move.centre[0]), dy = fabsf(at[1] - half - g_move.centre[1]);
                        if (dx > reach || dy > reach || (s->pass == 0) != (dx <= inner && dy <= inner)) continue;
                        if (g_move.stCount >= MOVE_STAMPS_MAX) { s->past++; s->pastInner += s->pass == 0; continue; }
                        uint64_t holder = 0, resource = 0;
                        memcpy(&holder, loc + DIF_STREAM_LOC_RESOURCE, 8);
                        if (holder == 0 || SafeRead((uintptr_t)holder + 8, &resource, 8) || resource == 0) continue;
                        MOVE_SEL_YIELD();
                        float pose[16];
                        WindowPose(loc, 2.0f * half, pose);
                        pose[13] += MOVE_STAMP_OFFSET;
                        uint64_t unit = ((WorldStreamSpawnUnitFn)spawn)(world, resource, pose, 0, 0);
                        made++;
                        if (unit == 0) { s->failed++; continue; }
                        memcpy(rec, loc, sizeof rec);
                        memcpy(rec + 0xA8, &unit, 8);
                        memcpy(g_move.stLoc + (size_t)g_move.stCount * 0xE0, rec, sizeof rec);
                        g_move.stIndex[g_move.stCount] = 0xFFFFFFFFu; g_move.stUnit[g_move.stUnits++] = unit;
                        g_move.stCount++; g_move.stTemp = g_move.stUnits; s->planned++;
                    }
                }
            }
        }
    }
#undef MOVE_SEL_YIELD
    QueryPerformanceCounter(&t);
    s->on = 0;
    {
        const double ms = (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
        s->frames++; s->ms += ms; if (ms > s->longest) s->longest = ms;
    }
    Note("difficulty: moving tiles: %u stamp terrains for the tile (%u of them the plan's%s); their units made over %u frames (%.0f ms in all, the longest frame %.1f ms)",
        g_move.stCount, s->planned, s->past ? "; more in reach than the tile's room" : "", s->frames, s->ms, s->longest);
    if (s->past) Note("difficulty: moving tiles: %u of the plan's stamps in reach left out (room %u), %u of them within %.0f m of the tile", s->past, MOVE_STAMPS_MAX, s->pastInner, MOVE_STAMP_REACH);
    if (s->failed) Note("difficulty: moving tiles: %u stamp units could not be made at the offset", s->failed);
    if (g_move.stCount) { g_nearStampOffset[0] = 0.0f; g_nearStampOffset[1] = MOVE_STAMP_OFFSET; }
    return 1;
frame:
    {
        const double ms = (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
        s->frames++; s->ms += ms; if (ms > s->longest) s->longest = ms;
    }
    return 0;
}

/* The level's 0x2F (op = 0x2F) or 0x65 record on the staging lists: locations pass+8 / count [pass+0x28] for both, units
 * pass+0x10 / count [pass+0x38] for 0x65's unit mode, the resume index (0x2F +0x48, 0x65 +0x4C) from 0. */
static int MoveStampsRecord(uint32_t at, int op)
{
    if (SafeRead(g_near.records + (uint64_t)at * 0x19B0u, g_move.one, sizeof g_move.one)) return 0;
    uint8_t* pass = g_move.one + 8;
    uint64_t locs = (uint64_t)(uintptr_t)g_move.stLoc, cnt = (uint64_t)(uintptr_t)&g_move.stCount;
    memcpy(pass + 8, &locs, 8); memcpy(pass + 0x28, &cnt, 8);
    uint32_t zero = 0;
    if (op == 0x2F) memcpy(pass + 0x48, &zero, 4);
    else
    {
        uint64_t units = (uint64_t)(uintptr_t)g_move.stUnit, ucnt = (uint64_t)(uintptr_t)&g_move.stUnits;
        memcpy(pass + 0x10, &units, 8); memcpy(pass + 0x38, &ucnt, 8); memcpy(pass + 0x4C, &zero, 4);
    }
    g_move.phase = 0;
    return 1;
}

/* One record of the level's through the dispatcher on the context copy, a call; 1 when it moved on, 0 not yet, -1 on a
 * fault. The generation arena is reset after a finished op (0xF26140 at 0xF261FE). */
static int MoveRunRecord(void)
{
    volatile uint32_t index = 0;
    volatile int calls = 0;
    LONG fault = WorldTileGuarded(g_move.one, (uint64_t)(uintptr_t)g_move.ctx, &index, &g_move.phase, 0, 0, 1, &calls);
    if (fault)
    {
        Note("difficulty: moving tiles: op 0x%02X faulted (%08lX at game.dll+0x%llX)", g_move.one[0], (unsigned long)fault, (unsigned long long)(TileGuardSelf()->at - g_world.game));
        return -1;
    }
    if (index == 0) return 0;
    uint64_t arena = 0;
    memcpy(&arena, g_move.ctx, 8);
    typedef void (*ArenaFn)(uint64_t arena);
    if (arena != 0) ((ArenaFn)(g_world.game + 0x173BAB0u))(arena);
    return 1;
}

/* Op 0x2F (0xF44380) in budgeted slices. Phase A spawns the stamp terrain of up to 1023 locations a call (the bound
 * 0x3FF at 0xF443FC), in location order, each its own table entry; once every location is done (0xF44709) phase B walks
 * all of them in the same call: for 32768 locations 196608 engine calls and more on one frame. Here a call's batch is
 * sized to MOVE_2F_MS, phase B is skipped by jumping to its "not done" exit (0xF4470C) and run below as it is in the
 * code, a slice a frame; the batches change only where the calls split, not what is spawned or in which order. */
#define MOVE_2F_MS 3.0
static const struct { uint32_t at; const char* bytes; } kMove2FPins[] = {
    {0xF443F9u, "41 8D 8B FF 03 00 00"},
    {0xF44408u, "44 3B F1 4C 89 A4 24 98 00 00 00 41 0F 42 CE 4C 89 AC 24 90 00 00 00 4C 89 BC 24 88 00 00 00 89"},
    {0xF44428u, "44 24 24 44 3B D9 0F 83 C9 02 00 00"}, /* the batch's clamp to the count and its loop test */
    {0xF44705u, "44 89 58 48 45 3B DE 0F 85 3A 01 00 00 48 8B 05 EF 1B 3E 02 48 8B 48 08 48 8B 41 48 48 8B 4B 40"},
    {0xF44725u, "FF D0 85 C0 0F 84 1D 01 00 00 45 33 ED 45 85 F6 0F 84 0D 01 00 00 8B 44 24 20 4C 8D 77 08 4C 8D"},
    {0xF44745u, "A6 A8 00 00 00 66 0F 1F 44 00 00 41 83 7E F8 FF 0F 84 D6 00 00 00 41 8B 76 FC 33 FF 49 8B 2C 24"},
    {0xF44765u, "45 8B 3E 85 F6 0F 84 C1 00 00 00 48 8B 05 91 1B 3E 02 42 8D 14 3F 48 8B 48 50 48 8B 41 30 48 8B"},
    {0xF44785u, "CD FF D0 8B D8 85 C0 74 1A 48 8B 05 73 1B 3E 02 48 8B 48 18 48 8B 81 20 07 00 00 8B CB FF D0 84"},
    {0xF447A5u, "C0 75 08 FF C7 3B FE 72 C2 EB 7D 48 8B 05 51 1B 3E 02 8B CB 48 8B 50 18 FF 92 50 04 00 00 48 8B"},
    {0xF447C5u, "05 3E 1B 3E 02 33 D2 48 8B 48 18 48 8B 81 60 04 00 00 8B CB FF D0 48 8B 0D 26 1B 3E 02 41 8D 55"},
    {0xF447E5u, "01 48 8B D8 4C 8B 81 10 01 00 00 48 8B C8 41 FF 50 18 48 8B 0D 0A 1B 3E 02 48 8B 91 10 01 00 00"},
    {0xF44805u, "48 8B CB 4C 8B 42 10 BA 03 05 76 F2 41 FF D0 48 8B 0D ED 1A 3E 02 0F 57 D2 BA FA 94 D9 E0 4C 8B"},
    {0xF44825u, "41 28 48 8B C8 41 FF 10 8B 44 24 20 41 FF C5 49 81 C4 E0 00 00 00 49 83 C6 0C 44 3B E8 0F 82 08"},
    {0xF44845u, "FF FF FF 32 C0 EB 02 B0 01"},
};
static struct { int pinned, phaseB, done; uint32_t batch, at, calls, slices; double msA, msB; } g_move2F;
/* Every stamp proxy phase B touches, for the render flags op 0x63's split would hide by (0x2BD140 reads them). They are
 * read after the slice, outside the guard: a SafeRead fault inside it would be taken for phase B's own. */
static uint64_t g_move2FObj[MOVE_STAMPS_MAX];
static uint32_t g_move2FObjN;
static struct { uint32_t n, noop, bit7, hideable, unread, values, other; uint32_t value[8], times[8]; } g_move2FFlags;

/* ---- Op 0x63's stamp render in ranges ---------------------------------------------------------------------------------
 * The render of a tile's ~29000 stamp terrains costs the render thread 147-180 ms, which the game thread waits for in
 * the next frame. Its cull reads rw+0x268 only (0x1EA3E0; 0x107400 zeroes property 0x81C3BDDD), which a hide takes a
 * terrain out of (0x3486C0 -> 0x2BD140, applied by the flush at 0x58E6A0), and its draws sort by depth i+1 of the
 * location, highest first. So the stamps go in ranges from the highest location down, one a frame: the job's own render
 * with range 0 shown, then its saved copy again per range; the same adds in the same order (scratch/u63g, u63h). */
#define SPLIT_STAMP_US 5.5    /* render side a stamp: the whole render's 147-180 ms over its 28600-29277 stamps */
#define SPLIT_RANGE_MS 3.0    /* render side a range */
#define SPLIT_TOGGLE_MS 1.0   /* game-thread time a frame for the shows and hides */
#define SPLIT_PER_MIN 64u
#define SPLIT_RANGES_MAX 256u
#define SPLIT_STALL 600       /* frames a range's render may wait for the job consumer */
#define SPLIT_FRESH 0xFFFFFFFFu
enum { SP_NONE, SP_WHOLE, SP_HIDING, SP_READY, SP_QUEUED, SP_RANGES, SP_SHOWING, SP_DONE, SP_RESTORE };
static struct
{
    int state, over, idsOk, pending;
    LONG level;
    uint32_t m, per, K, k, cursor, hiddenN, stall, qBefore;
    uint32_t l2Locs, l2Multi, l2Extra, l2Most;
    uint32_t shows, hides, dead, frames;
    double toggleMs, toggleMax, renderMs, renderMax, renderFirst, frameMs, frameMax, frameLeast;
    LARGE_INTEGER frameAt;
    __attribute__((aligned(16))) uint8_t job[0x1D0]; /* the 0x63 job as queued: the consumer's copy of it (0xAB0600) */
} g_split;
static uint32_t g_splitId[MOVE_STAMPS_MAX]; /* phase B's live terrain id of each drawn location, in location order */
static uint8_t g_splitHidden[MOVE_STAMPS_MAX];

static int Move2FPinned(void)
{
    if (g_move2F.pinned == 0)
    {
        g_move2F.pinned = 1;
        for (size_t k = 0; k < sizeof kMove2FPins / sizeof kMove2FPins[0]; k++)
            if (!ApiCodeMatches(g_world.game + kMove2FPins[k].at, kMove2FPins[k].bytes)) g_move2F.pinned = -1;
        if (g_move2F.pinned < 0) Note("difficulty: moving tiles: op 0x2F is not the reviewed code; it runs whole, a call a frame");
    }
    return g_move2F.pinned > 0;
}

/* The batch bound and phase B's gate: `batch` locations a call and the gate a jump to the exit, or the game's bytes. */
static void Move2FPatch(uint32_t batch)
{
    uint8_t* at = (uint8_t*)(uintptr_t)(g_world.game + 0xF443F9u);
    DWORD old = 0;
    if (!VirtualProtect(at, 0x320, PAGE_EXECUTE_READWRITE, &old)) return;
    const uint32_t bound = batch != 0 ? batch : 0x3FFu;
    memcpy(at + 3, &bound, 4);
    static const uint8_t jump[6] = {0xE9, 0x3B, 0x01, 0x00, 0x00, 0x90}, gate[6] = {0x0F, 0x85, 0x3A, 0x01, 0x00, 0x00};
    memcpy((uint8_t*)(uintptr_t)(g_world.game + 0xF4470Cu), batch != 0 ? jump : gate, 6);
    VirtualProtect(at, 0x320, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 0x320);
}

/* Phase B as 0xF44712..0xF4484A has it, from location g_move2F.at until MOVE_2F_MS are spent (checked each 16). */
static void Move2FPhaseB(uint64_t pass, uint64_t record, uint64_t context)
{
    (void)record;
    typedef uint64_t (*WorldFn)(uint64_t world);
    typedef uint32_t (*IdFn)(uint64_t unit, uint32_t index);
    typedef char (*LiveFn)(uint32_t id);
    typedef void (*IdOnlyFn)(uint32_t id);
    typedef uint64_t (*ObjFn)(uint32_t id, uint32_t zero);
    typedef void (*IndexFn)(uint64_t obj, uint32_t index);
    typedef uint64_t (*MatFn)(uint64_t obj, uint32_t hash);
    typedef void (*ScalarFn)(uint64_t mat, uint32_t hash, float value);
    const uint64_t api = *(volatile uint64_t*)(uintptr_t)(g_world.game + 0x3326308u);
    const uint64_t a8 = *(uint64_t*)(uintptr_t)(api + 0x08), a18 = *(uint64_t*)(uintptr_t)(api + 0x18), a28 = *(uint64_t*)(uintptr_t)(api + 0x28);
    const uint64_t a50 = *(uint64_t*)(uintptr_t)(api + 0x50), a110 = *(uint64_t*)(uintptr_t)(api + 0x110);
    const uint32_t count = **(uint32_t**)(uintptr_t)(pass + 0x28);
    const uint8_t* locs = *(const uint8_t**)(uintptr_t)(pass + 0x08);
    const uint32_t* table = *(const uint32_t**)(uintptr_t)(pass + 0x50);
    if (g_move2F.at == 0 && ((WorldFn)*(uint64_t*)(uintptr_t)(a8 + 0x48))(*(uint64_t*)(uintptr_t)(context + 0x40)) == 0) return; /* again next call */
    LARGE_INTEGER hz, t0, t1; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    uint32_t i = g_move2F.at;
    for (; i < count; i++)
    {
        if (i != g_move2F.at && (i & 15) == 0)
        {
            QueryPerformanceCounter(&t1);
            if ((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart >= MOVE_2F_MS) break;
        }
        const uint32_t* e = table + (size_t)i * 3;
        if (e[0] == 0xFFFFFFFFu || e[1] == 0) continue;
        const uint64_t unit = *(const uint64_t*)(locs + (size_t)i * 0xE0 + 0xA8);
        uint32_t id = 0, j = 0;
        for (; j < e[1]; j++)
        {
            id = ((IdFn)*(uint64_t*)(uintptr_t)(a50 + 0x30))(unit, j + e[2]);
            if (id != 0 && ((LiveFn)*(uint64_t*)(uintptr_t)(a18 + 0x720))(id)) break;
            id = 0;
        }
        /* the later live ids keep the default depth and would draw in every range of 0x63's split: counted (reads only,
         * 0x32B8E0 and 0x9D8C0), and any one turns the split off */
        g_split.l2Locs++;
        if (e[1] > 1) g_split.l2Multi++;
        if (id != 0 && e[1] > 1)
        {
            uint32_t extra = 0;
            for (uint32_t k = j + 1; k < e[1]; k++)
            {
                const uint32_t other = ((IdFn)*(uint64_t*)(uintptr_t)(a50 + 0x30))(unit, k + e[2]);
                if (other != 0 && ((LiveFn)*(uint64_t*)(uintptr_t)(a18 + 0x720))(other)) extra++;
            }
            g_split.l2Extra += extra;
            if (extra > g_split.l2Most) g_split.l2Most = extra;
        }
        if (id == 0) continue;
        if (g_split.m < MOVE_STAMPS_MAX) g_splitId[g_split.m++] = id; else g_split.over = 1;
        ((IdOnlyFn)*(uint64_t*)(uintptr_t)(a18 + 0x450))(id);
        const uint64_t obj = ((ObjFn)*(uint64_t*)(uintptr_t)(a18 + 0x460))(id, 0);
        if (g_move2FObjN < MOVE_STAMPS_MAX) g_move2FObj[g_move2FObjN++] = obj;
        ((IndexFn)*(uint64_t*)(uintptr_t)(a110 + 0x18))(obj, i + 1);
        const uint64_t mat = ((MatFn)*(uint64_t*)(uintptr_t)(a110 + 0x10))(obj, 0xF2760503u);
        ((ScalarFn)*(uint64_t*)(uintptr_t)a28)(mat, 0xE0D994FAu, 0.0f);
    }
    QueryPerformanceCounter(&t1);
    g_move2F.msB += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    g_move2F.slices++;
    g_move2F.at = i;
    g_move2F.done = i >= count;
    if (g_move2F.done) g_split.idsOk = 1;
}

/* The slice's proxies' flags as 0x2BD140 reads them: [[obj+0x30]+0x2C], else [[obj+0x38]+0x20]. Its hide does nothing
 * without a bit of 0x83; bits 0 and 1 name the culling sets it moves, bit 7 one it leaves. */
static void Move2FFlagsTally(void)
{
    for (uint32_t k = 0; k < g_move2FObjN; k++)
    {
        uint64_t r30 = 0, r38 = 0; uint32_t f = 0;
        g_move2FFlags.n++;
        const int read = !SafeRead(g_move2FObj[k] + 0x30, &r30, 8) && (r30 != 0 ? !SafeRead(r30 + 0x2C, &f, 4) :
            !SafeRead(g_move2FObj[k] + 0x38, &r38, 8) && r38 != 0 && !SafeRead(r38 + 0x20, &f, 4));
        if (!read) { g_move2FFlags.unread++; continue; }
        if ((f & 0x83) == 0) g_move2FFlags.noop++;
        else if (f & 0x80) g_move2FFlags.bit7++;
        else g_move2FFlags.hideable++;
        uint32_t v = 0;
        while (v < g_move2FFlags.values && g_move2FFlags.value[v] != f) v++;
        if (v == g_move2FFlags.values && v < 8) { g_move2FFlags.value[v] = f; g_move2FFlags.values++; }
        if (v < g_move2FFlags.values) g_move2FFlags.times[v]++; else g_move2FFlags.other++;
    }
    g_move2FObjN = 0;
}

static int MoveRun2FDone(uint32_t count)
{
    Note("difficulty: moving tiles: op 0x2F in %u calls (%.1f ms) and %u slices of its last part (%.1f ms) for %u locations", g_move2F.calls, g_move2F.msA,
        g_move2F.slices, g_move2F.msB, count);
    if (g_move2FFlags.n != 0)
    {
        char values[256]; size_t n = 0; values[0] = 0;
        for (uint32_t v = 0; v < g_move2FFlags.values && n + 1 < sizeof values; v++)
        {
            int w = snprintf(values + n, sizeof values - n, " 0x%X x%u;", g_move2FFlags.value[v], g_move2FFlags.times[v]);
            if (w < 0) break;
            n += (size_t)w;
        }
        Note("difficulty: moving tiles: the stamp terrains' render flags over %u: hide a no-op (no bit of 0x83) %u, bit 7 %u, hideable (bit 0 or 1, no bit 7) %u, "
            "unread %u; values:%s %u of others", g_move2FFlags.n, g_move2FFlags.noop, g_move2FFlags.bit7, g_move2FFlags.hideable, g_move2FFlags.unread, values,
            g_move2FFlags.other);
    }
    else Note("difficulty: moving tiles: the stamp terrains' render flags were not tallied (no stamp in the mod's last part)");
    if (g_split.idsOk)
        Note("difficulty: moving tiles: 0x63 terrains: %u locations, %u with e[1] > 1, %u extra live ids (most at one location %u)", g_split.l2Locs,
            g_split.l2Multi, g_split.l2Extra, g_split.l2Most);
    else Note("difficulty: moving tiles: 0x63 terrains: not counted (op 0x2F's last part ran in the game's call)");
    uint64_t arena = 0;
    memcpy(&arena, g_move.ctx, 8);
    typedef void (*ArenaFn)(uint64_t arena);
    if (arena != 0) ((ArenaFn)(g_world.game + 0x173BAB0u))(arena);
    return 1;
}

/* MV_STAMPS' call: 1 done (the arena reset as MoveRunRecord does), 0 more next frame, -1 a fault. */
static int MoveRun2F(void)
{
    if (!Move2FPinned()) return MoveRunRecord();
    uint64_t pass = (uint64_t)(uintptr_t)g_move.one + 8, countAt = 0;
    uint32_t count = 0, before = 0, after = 0;
    if (SafeRead(pass + 0x28, &countAt, 8) || countAt == 0 || SafeRead(countAt, &count, 4)) return MoveRunRecord();
    LARGE_INTEGER hz, t0, t1; QueryPerformanceFrequency(&hz);
    if (!g_move2F.phaseB)
    {
        if (g_move2F.batch == 0) g_move2F.batch = 16; /* the first build's guess; later builds start at the last one's */
        SafeRead(pass + 0x48, &before, 4);
        volatile uint32_t index = 0;
        volatile int calls = 0;
        Move2FPatch(g_move2F.batch);
        QueryPerformanceCounter(&t0);
        LONG fault = WorldTileGuarded(g_move.one, (uint64_t)(uintptr_t)g_move.ctx, &index, &g_move.phase, 0, 0, 1, &calls);
        QueryPerformanceCounter(&t1);
        Move2FPatch(0);
        if (fault) { Note("difficulty: moving tiles: op 0x2F faulted (%08lX at game.dll+0x%llX)", (unsigned long)fault, (unsigned long long)(TileGuardSelf()->at - g_world.game)); return -1; }
        SafeRead(pass + 0x48, &after, 4);
        const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
        g_move2F.msA += ms; g_move2F.calls++;
        if (index != 0) return MoveRun2FDone(count); /* the game's call finished it whole */
        if (after > before) g_move.waits = 0; /* moving on */
        /* the next batch from this one's time a location, within 1..1023 */
        const double per = after > before ? ms / (double)(after - before) : 0;
        double next = per > 0 ? MOVE_2F_MS / per : (double)g_move2F.batch * 2;
        if (next > (double)g_move2F.batch * 2) next = (double)g_move2F.batch * 2;
        g_move2F.batch = next < 1 ? 1u : next > 1023 ? 1023u : (uint32_t)next;
        if (after < count) return 0;
        g_move2F.phaseB = 1; g_move2F.at = 0;
        return 0;
    }
    const uint32_t at = g_move2F.at;
    LONG fault = WorldTileGuardedCall((uintptr_t)Move2FPhaseB, g_move.one, (uint64_t)(uintptr_t)g_move.ctx);
    if (fault)
    {
        const uintptr_t where = TileGuardSelf()->at, exe = (uintptr_t)GetModuleHandleW(NULL);
        Note("difficulty: moving tiles: op 0x2F's last part faulted (%08lX at %p: game.dll+0x%llX, exe+0x%llX)", (unsigned long)fault, (void*)where,
            (unsigned long long)(where - g_world.game), (unsigned long long)(where - exe));
        return -1;
    }
    Move2FFlagsTally();
    if (g_move2F.at > at) g_move.waits = 0;
    if (!g_move2F.done) return 0;
    return MoveRun2FDone(count);
}
static void SplitClear(void)
{
    memset(&g_split, 0, sizeof g_split);
    g_split.level = g_world.levels; g_split.cursor = SPLIT_FRESH;
}
static void Move2FReset(void)
{
    const int pinned = g_move2F.pinned; const uint32_t batch = g_move2F.batch;
    memset(&g_move2F, 0, sizeof g_move2F); memset(&g_move2FFlags, 0, sizeof g_move2FFlags);
    g_move2F.pinned = pinned; g_move2F.batch = batch; g_move2FObjN = 0;
    SplitClear();
}

/* ---- The split's steps (game thread) ---- */
typedef char (*SplitLiveFn)(uint32_t id);
typedef uint64_t (*SplitObjFn)(uint32_t id, uint32_t zero);
typedef void (*SplitShowFn)(uint64_t obj, uint32_t show);
static struct { uint32_t from, to, showBelow; double budget; int done; SplitLiveFn live; SplitObjFn obj; SplitShowFn show; } g_splitWalk;

/* Range k: stamps [lo, hi) of g_splitId, from the top down; the last one reaches 0. */
static uint32_t SplitHi(uint32_t k) { return g_split.m - k * g_split.per; }
static uint32_t SplitLo(uint32_t k)
{
    const uint32_t top = (k + 1) * g_split.per;
    return k + 1 >= g_split.K || top >= g_split.m ? 0 : g_split.m - top;
}

/* Under the tile guard: stamps [cursor, to) shown below showBelow, hidden from it, each looked up by its id first (a
 * stamp can be gone; its object is never kept, 0x203E20 has no null test). Only a change is posted. */
static void SplitWalkIn(uint64_t unused0, uint64_t unused1, uint64_t unused2)
{
    (void)unused0; (void)unused1; (void)unused2;
    LARGE_INTEGER hz, t0, t1; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    uint32_t x = g_split.cursor;
    for (uint32_t n = 0; x < g_splitWalk.to; x++, n++)
    {
        if (n != 0 && (n & 15) == 0)
        {
            QueryPerformanceCounter(&t1);
            if ((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart >= g_splitWalk.budget) break;
        }
        const uint8_t want = x < g_splitWalk.showBelow ? 0 : 1; /* 1: hidden */
        if (g_splitHidden[x] == want) continue;
        const uint32_t id = g_splitId[x];
        if (id == 0 || !g_splitWalk.live(id))
        {
            if (id != 0) { g_split.dead++; g_splitId[x] = 0; } /* never looked up again */
            if (g_splitHidden[x]) { g_splitHidden[x] = 0; g_split.hiddenN--; } /* nothing left to show */
            continue;
        }
        g_splitWalk.show(g_splitWalk.obj(id, 0), want ? 0u : 1u);
        g_splitHidden[x] = want;
        if (want) { g_split.hides++; g_split.hiddenN++; } else { g_split.shows++; g_split.hiddenN--; }
    }
    g_split.cursor = x;
    g_splitWalk.done = x >= g_splitWalk.to;
}

/* One walk's slice within `budget` ms: 1 done, 0 more next frame, -1 a fault (the walk's state stays as far as it got). */
static int SplitWalk(uint32_t from, uint32_t to, uint32_t showBelow, double budget)
{
    g_splitWalk.live = (SplitLiveFn)NearApi(0x18, 0x720); g_splitWalk.obj = (SplitObjFn)NearApi(0x18, 0x460);
    g_splitWalk.show = (SplitShowFn)NearApi(0x110, 0x20);
    if (g_splitWalk.live == NULL || g_splitWalk.obj == NULL || g_splitWalk.show == NULL) return -1;
    if (g_split.cursor == SPLIT_FRESH || g_split.cursor < from) g_split.cursor = from;
    g_splitWalk.from = from; g_splitWalk.to = to; g_splitWalk.showBelow = showBelow; g_splitWalk.budget = budget; g_splitWalk.done = 0;
    LARGE_INTEGER hz, t0, t1; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    const LONG fault = WorldTileGuardedCall((uintptr_t)SplitWalkIn, NULL, 0);
    QueryPerformanceCounter(&t1);
    const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    g_split.toggleMs += ms; if (ms > g_split.toggleMax) g_split.toggleMax = ms;
    if (fault)
    {
        const uintptr_t where = TileGuardSelf()->at, exe = (uintptr_t)GetModuleHandleW(NULL);
        Note("difficulty: moving tiles: 0x63 split: a show or hide faulted (%08lX at exe+0x%llX)", (unsigned long)fault, (unsigned long long)(where - exe));
        return -1;
    }
    if (!g_splitWalk.done) return 0;
    g_split.cursor = SPLIT_FRESH;
    return 1;
}

static void SplitFrame(void)
{
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (g_split.frameAt.QuadPart != 0)
    {
        LARGE_INTEGER hz; QueryPerformanceFrequency(&hz);
        const double ms = (double)(now.QuadPart - g_split.frameAt.QuadPart) * 1000.0 / (double)hz.QuadPart;
        g_split.frameMs += ms; g_split.frames++;
        if (ms > g_split.frameMax) g_split.frameMax = ms;
        if (g_split.frameLeast == 0 || ms < g_split.frameLeast) g_split.frameLeast = ms;
    }
    g_split.frameAt = now;
}

/* Whole or in ranges, decided once a build before any hide: the ids are phase B's, none extra, the calls there. */
static void SplitDecide(void)
{
    char why[160] = "";
#ifdef CO_RESEARCH
    { char t[4] = ""; if (GetEnvironmentVariableA("HELLFORGE_XL_MOVE_SPLIT63", t, sizeof t) > 0 && t[0] == '0') snprintf(why, sizeof why, "turned off (HELLFORGE_XL_MOVE_SPLIT63=0, research)"); }
#endif
    if (why[0] != 0) {}
    else if (!g_split.idsOk || g_split.level != g_world.levels) snprintf(why, sizeof why, "op 0x2F's last part did not run in the mod, so its terrain ids were not kept");
    else if (g_split.over) snprintf(why, sizeof why, "more than %u stamp terrains", MOVE_STAMPS_MAX);
    else if (g_split.l2Extra != 0) snprintf(why, sizeof why, "%u extra live terrain ids at its locations would draw in every range", g_split.l2Extra);
    else if (NearApi(0x18, 0x720) == 0 || NearApi(0x18, 0x460) == 0 || NearApi(0x110, 0x20) == 0 || NearApi(0x10, 0x30) == 0)
        snprintf(why, sizeof why, "the live, object, show or flush call did not resolve");
    else if (g_nearRunOriginal == NULL || g_nearConsume == NULL) snprintf(why, sizeof why, "the render wrapper or the job consumer's hook is not in");
    else
    {
        uint32_t per = (uint32_t)(SPLIT_RANGE_MS * 1000.0 / SPLIT_STAMP_US);
        if (per < SPLIT_PER_MIN) per = SPLIT_PER_MIN;
        uint32_t K = (g_split.m + per - 1) / per;
        if (K > SPLIT_RANGES_MAX) { K = SPLIT_RANGES_MAX; per = (g_split.m + K - 1) / K; }
        g_split.per = per; g_split.K = K;
        if (K < 2) snprintf(why, sizeof why, "its %u stamps fit one range", g_split.m);
    }
    if (why[0] != 0)
    {
        g_split.state = SP_WHOLE;
        Note("difficulty: moving tiles: 0x63 split: off: %s; the stamps render whole in one frame", why);
        return;
    }
    g_split.state = SP_HIDING; g_split.cursor = SPLIT_FRESH;
    Note("difficulty: moving tiles: 0x63 split: on: %u stamps in %u ranges of %u (%.1f us a stamp, %.1f ms a range)", g_split.m, g_split.K, g_split.per,
        SPLIT_STAMP_US, SPLIT_RANGE_MS);
}

/* Before 0x63's op (its first call): every stamp below range 0 hidden, a slice a frame. 1 the op may run, 0 more next
 * frame, -1 a fault. `queued`: the job queue's count now, against which the op's one job is found. */
static int Split63Prepare(uint32_t queued)
{
    if (g_split.state == SP_NONE) SplitDecide();
    if (g_split.state == SP_HIDING)
    {
        const int r = SplitWalk(0, SplitLo(0), 0, SPLIT_TOGGLE_MS);
        if (r <= 0) return r;
        g_split.state = SP_READY;
    }
    g_split.qBefore = queued;
    return 1;
}

/* After 0x63's op: its one job is the one the split renders again, checked as the producer writes it (0xC178B1,
 * 0xC14DCE) and copied. Any mismatch shows every stamp again in this frame, before the consumer renders the job (same
 * thread, later in the frame), so it renders whole. -1: the shows faulted and the build must fail. */
static int Split63Queued(uint64_t context)
{
    if (g_split.state != SP_READY) return 0;
    char why[160] = "";
    uint64_t queue = 0, world = 0, w0 = 0, cam = 0;
    uint32_t q = 0, kind = 0;
    if (SafeRead(context + 0x50, &queue, 8) || queue == 0 || SafeRead(queue + 0x10, &q, 4) || SafeRead(context + 0x40, &world, 8))
        snprintf(why, sizeof why, "the job queue did not read");
    else if (q != g_split.qBefore + 1) snprintf(why, sizeof why, "the op queued %d jobs, not one", (int)(q - g_split.qBefore));
    else if (SafeRead(queue + 0x18 + (uint64_t)(q - 1) * 0x1D0, g_split.job, sizeof g_split.job)) snprintf(why, sizeof why, "its job did not read");
    else
    {
        memcpy(&w0, g_split.job, 8); memcpy(&cam, g_split.job + 8, 8); memcpy(&kind, g_split.job + 0x68, 4);
        if (kind != 0xFFFFFFFFu || g_split.job[0x24] != 0 || g_split.job[0x38] != 0 || g_split.job[0x39] != 0)
            snprintf(why, sizeof why, "its job is not a plain render (+0x68 %08X, +0x24 %u, +0x38 %u, +0x39 %u)", kind, g_split.job[0x24], g_split.job[0x38], g_split.job[0x39]);
        else if (w0 == 0 || w0 != world) snprintf(why, sizeof why, "its job's world is not the build's");
        else if (cam == 0 || cam != g_near.cam[0]) snprintf(why, sizeof why, "its job's camera is not the merge camera");
    }
    if (why[0] == 0)
    {
        g_split.state = SP_QUEUED; g_split.frameAt.QuadPart = 0;
        SplitFrame();
        return 0;
    }
    const uint32_t hidden = g_split.hiddenN;
    const double before = g_split.toggleMs;
    const int r = SplitWalk(0, g_split.m, g_split.m, 1e30);
    g_split.state = SP_WHOLE;
    Note("difficulty: moving tiles: 0x63 split: off after the hides: %s; %u stamps shown again in this frame (%.1f ms%s), the stamps render whole in one frame",
        why, hidden, g_split.toggleMs - before, r < 0 ? ", the shows FAULTED: the build fails" : "");
    return r < 0 ? -1 : 0;
}

/* The job's copy again for the range whose shows and hides are posted, from the job consumer's hook after the game's
 * consumer (0xAB07A0 / 0xAB07E1 as it calls them for this job, through the wrapper so the gui's offset and scale
 * apply), under the tile's names. Only while this build's replay waits in 0x63. */
static void Split63Replay(void)
{
    if (!g_split.pending || g_split.state != SP_RANGES || !g_nearActive || !g_moveBuilding || !g_near.stampLeave || g_split.level != g_world.levels) return;
    typedef void (*FlushFn)(uint64_t world);
    FlushFn flush = (FlushFn)NearApi(0x10, 0x30);
    if (flush == NULL) return; /* the range stalls and the build fails */
    uint64_t a = 0, camera = 0, c = 0, e = 0; uint32_t flags = 0;
    memcpy(&a, g_split.job, 8); memcpy(&camera, g_split.job + 8, 8); memcpy(&c, g_split.job + 0x10, 8); memcpy(&e, g_split.job + 0x18, 8);
    memcpy(&flags, g_split.job + 0x20, 4);
    const int opened = NearNamesOpen();
    LARGE_INTEGER hz, t0, t1; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    flush(a);
    NearRunAt(1, a, camera, c, (uint64_t)(uintptr_t)(g_split.job + 0x58), e, 0, (uint64_t)(flags | 0x10000u), 0);
    QueryPerformanceCounter(&t1);
    NearNamesClose(opened);
    const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    g_split.renderMs += ms; if (ms > g_split.renderMax) g_split.renderMax = ms;
    g_split.pending = 0;
}

/* The game's consumer rendering the queued job (range 0): its time. `job` is the consumer's copy. */
static void Split63Seen(const uint8_t* job, double ms)
{
    if (g_split.state != SP_QUEUED || job == NULL || memcmp(job, g_split.job, 0x70) != 0) return;
    g_split.renderFirst = ms; g_split.renderMs += ms; if (ms > g_split.renderMax) g_split.renderMax = ms;
}

/* 0x63's leave, once its job has drained (the consumer rendered it with range 0 shown: every queued render-path job is
 * rendered, 0xAB07E1): per frame, range k hidden and range k+1 shown, then its render in the consumer; after the last
 * range every stamp shown again. 1 done (or whole), 0 more next frame, -1 the build fails. */
static int Split63Leave(void)
{
    if (g_split.state == SP_NONE || g_split.state == SP_WHOLE || g_split.state == SP_DONE) return 1;
    if (g_split.state != SP_QUEUED && g_split.state != SP_RANGES && g_split.state != SP_SHOWING) return -1;
    SplitFrame();
    if (g_split.state == SP_QUEUED) { g_split.state = SP_RANGES; g_split.k = 0; g_split.pending = 0; g_split.cursor = SPLIT_FRESH; }
    if (g_split.state == SP_RANGES)
    {
        if (g_split.pending)
        {
            if (++g_split.stall < SPLIT_STALL) return 0;
            Note("difficulty: moving tiles: 0x63 split: range %u of %u was not rendered in %u frames; the build fails", g_split.k + 1, g_split.K, SPLIT_STALL);
            return -1;
        }
        if (g_split.k + 1 < g_split.K)
        {
            /* ranges k+1 and k are one span: shown below range k's start, hidden from it */
            const int r = SplitWalk(SplitLo(g_split.k + 1), SplitHi(g_split.k), SplitLo(g_split.k), SPLIT_TOGGLE_MS);
            if (r <= 0) return r;
            g_split.k++; g_split.pending = 1; g_split.stall = 0;
            return 0;
        }
        g_split.state = SP_SHOWING; g_split.cursor = SPLIT_FRESH;
    }
    const int r = SplitWalk(0, g_split.m, g_split.m, SPLIT_TOGGLE_MS);
    if (r <= 0) return r;
    g_split.state = SP_DONE;
    Note("difficulty: moving tiles: 0x63 split: %u ranges of %u stamps, toggles %.1f ms (frame max %.2f ms, %u shows, %u hides, %u skipped dead), "
        "renders %.1f ms (range max %.2f ms, first %.2f ms), frames %.1f ms over %u (max %.2f ms, least %.2f ms)", g_split.K, g_split.per, g_split.toggleMs,
        g_split.toggleMax, g_split.shows, g_split.hides, g_split.dead, g_split.renderMs, g_split.renderMax, g_split.renderFirst, g_split.frameMs, g_split.frames,
        g_split.frameMax, g_split.frameLeast);
    return 1;
}

/* A build that fails while stamps are hidden: no range renders again, and MV_FAILING shows them before 0x65. */
static void Split63Fail(const char* why)
{
    if (g_split.state == SP_NONE || g_split.state == SP_WHOLE || g_split.state == SP_DONE || g_split.state == SP_RESTORE) return;
    g_split.pending = 0;
    Note("difficulty: moving tiles: 0x63 split: cut short in range %u of %u (%s): %u stamps hidden, shown again before 0x65", g_split.k + 1, g_split.K, why,
        g_split.hiddenN);
    g_split.state = g_split.hiddenN != 0 ? SP_RESTORE : SP_DONE; g_split.cursor = SPLIT_FRESH;
}

/* MV_FAILING's first work: 1 once nothing is hidden. Shows that fault leave the rest hidden for the destroy, which
 * takes a hidden terrain from the set's parked list as well (0x58E240, 0x58E2BF). */
static int Split63Restore(void)
{
    if (g_split.state != SP_RESTORE) return 1;
    const int r = SplitWalk(0, g_split.m, g_split.m, SPLIT_TOGGLE_MS);
    if (r == 0) return 0;
    if (r < 0) Note("difficulty: moving tiles: 0x63 split: %u stamps stay hidden for their destroy", g_split.hiddenN);
    g_split.state = SP_DONE;
    return 1;
}

/* The ship or a level init with a build under way: no frame of it follows, so nothing is shown. Hidden stamps go with
 * the build's units through the teardown: a hidden terrain's destroy (0x1E81B4) takes it from the parked list
 * (0x58E240), the inverse of the hide's own bookkeeping (0x58E6A0). */
static void Split63Left(const char* why)
{
    if (g_split.hiddenN != 0 && g_split.state != SP_NONE && g_split.state != SP_WHOLE)
        Note("difficulty: moving tiles: 0x63 split: %s in range %u of %u: %u stamps stay hidden; they go with the build's units", why, g_split.k + 1, g_split.K,
            g_split.hiddenN);
    SplitClear();
}

static uint64_t MoveWorld(void)
{
    uint64_t world = 0;
    memcpy(&world, g_move.ctx + 0x40, 8);
    return world;
}

/* From MV_KICK on the merge's resources may exist, and its world-size views stay out of the scatter update. */
static uintptr_t MoveMergeObject(void);
static void MoveMergeBegin(void)
{
    if (!g_move.merge)
    {
        /* 0x66 and MoveTeardown destroy the second camera's node and leave its id at R+0x18 (0xC17C5B..0xC17C63); a
         * teardown before 0x38 has made this build's must not queue that id again (exe 0x12D2D0) */
        const uintptr_t R = MoveMergeObject();
        g_move.nodeOld = 0;
        if (R != 0) SafeRead(R + 0x18, &g_move.nodeOld, 4);
    }
    g_move.merge = 1;
    TdScatterMergeOpen();
}

/* The merge object R is part of the level object (level+0x2B30: level init stores that at [G], 0xF7150F..0xF71516,
 * and the level's destructor passes it to 0xFCF2F0, 0xF6F88E), so it is there after the level's teardown has cleared G. */
static uintptr_t MoveMergeObject(void)
{
    const uintptr_t level = g_move.levelPtr != 0 ? g_move.levelPtr : g_worldLevel;
    uintptr_t gen = 0, R = 0;
    if (level == 0 || SafeRead(level + 0x2C8, &gen, 8) || gen == 0 || SafeRead(gen, &R, 8) || (R != 0 && R != level + 0x2B30u)) return 0;
    return level + 0x2B30u;
}

/* `object` is one of the pointers of a world list. R+0x108's destroy ([api+8]+0x478, exe 0x1B3F90) looks for it in
 * [world+0x230910] (count +0x230908) and reads the object all the same when it is not there (0x1B3FDE). */
static int MoveListed(uint64_t countAt, uint64_t listAt, uint64_t object)
{
    uint32_t count = 0;
    uint64_t list = 0, entry = 0;
    if (SafeRead(countAt, &count, 4) || SafeRead(listAt, &list, 8) || count > 4096 || list == 0) return 0;
    for (uint32_t i = 0; i < count; i++) if (!SafeRead(list + 8ull * i, &entry, 8) && entry == object) return 1;
    return 0;
}

/* A camera of the merge may be asked to go. After the level its handle may be gone already (the unload destroys every
 * unit, 0xAB3D20..0xAB3DCB), and one that resolves to no unit ends the game in the destroy queue's drain when the
 * queue's own filter is not in (exe 0x12E70E..0x12E71B). So it goes only while it resolves; with no unit table known
 * it is left to the world. */
static int MoveNodeGoes(uint32_t node, int left)
{
    return node != 0 && (!left || (g_destroyTableAt != 0 && DestroyHandleState(node) == 0));
}

/* The merge as the level's 0x66 and 0x67 leave it (the parts this build re-created); 1 when torn down. `left`: the
 * level has ended and its own teardown (0xF6FF60) has run: that released the 0x94 lookup and ended the environment's
 * slot 5 (0xF708AB), the records may be the next generation's, and the saved environment pointer may be gone. */
static int MoveTeardown(int left)
{
    if (!g_move.merge) return 0;
    g_move.merge = 0;
    TdScatterMergeClose(); /* its camera units go below; the scatter cut ends once their queued renders ran */
    uintptr_t R = MoveMergeObject(), api = 0, t8 = 0, t10 = 0;
    uint64_t world = MoveWorld(), env = 0;
    memcpy(&env, g_move.ctx + 0x98, 8);
    if (R == 0 || SafeRead(g_world.game + 0x3326308u, &api, 8) || api == 0 ||
        SafeRead(api + 8, &t8, 8) || t8 == 0 || SafeRead(api + 0x10, &t10, 8) || t10 == 0 || world == 0 || (left && world != NearMainWorld()))
    { Note("difficulty: moving tiles: the merge could not be torn down (no merge object or engine tables)"); return 0; }
    typedef void (*NodeFn)(uint64_t world, uint32_t node);
    typedef void (*UnitFn)(uint64_t world, uint64_t unit);
    typedef void (*PtrFn)(uint64_t p);
    typedef void (*NameFn)(uint32_t name);
    typedef void (*PkgFn)(uint64_t hash);
    NodeFn destroyNode = NULL; UnitFn destroyUnit = NULL; PtrFn releaseRead = NULL, release = NULL; NameFn unpublish = NULL; PkgFn pkgRelease = NULL;
    SafeRead(t8 + 0x60, &destroyNode, 8); SafeRead(t8 + 0x478, &destroyUnit, 8); SafeRead(t10 + 0x1E0, &releaseRead, 8); SafeRead(t10 + 0x190, &release, 8);
    SafeRead(t10 + 0x218, &unpublish, 8); SafeRead(t10 + 0x300, &pkgRelease, 8);
    if (destroyNode == NULL || destroyUnit == NULL || releaseRead == NULL || release == NULL || unpublish == NULL || pkgRelease == NULL) return 0;
    /* 0x66: the cameras, R+0x108 */
    uint32_t n0 = 0, n1 = 0; uint64_t r108 = 0;
    SafeRead(R + 8, &n0, 4); SafeRead(R + 0x18, &n1, 4); SafeRead(R + 0x108, &r108, 8);
    if (MoveNodeGoes(n0, left)) destroyNode(world, n0);
    if (n1 != g_move.nodeOld && MoveNodeGoes(n1, left)) destroyNode(world, n1);
    g_move.nodeOld = n1;
    /* after the level only one the world still lists is its to destroy; 0xFCF2F0 below skips a zero */
    const int listed = r108 != 0 && (!left || MoveListed(world + 0x230908u, world + 0x230910u, r108));
    if (listed) destroyUnit(world, r108);
    g_move.r108Out = r108 == 0 ? 0 : listed ? 1 : 2; /* for the level end's line */
    *(volatile uint32_t*)(R + 8) = 0; *(volatile uint64_t*)(R + 0x108) = 0;
    /* 0x66: 0x94's lookup ([record+0x18]: its readback +0x10, its texture +0x18 under 0xDBCD240D) */
    uint64_t lookup = 0, rd = 0, tex = 0;
    if (!left && !SafeRead(g_near.records + (uint64_t)g_move.at66 * 0x19B0u + 0x18, &lookup, 8) && lookup != 0)
    {
        if (!SafeRead(lookup + 0x10, &rd, 8) && rd != 0) releaseRead(lookup);
        if (!SafeRead(lookup + 0x18, &tex, 8) && tex != 0) { unpublish(0xDBCD240Du); release(tex); *(volatile uint64_t*)(lookup + 0x18) = 0; }
    }
    typedef void (*MergeFn)(uint64_t R, uint64_t world);
    typedef void (*EnvFn)(uint64_t manager, uint32_t slot);
    ((MergeFn)(g_world.game + 0xFCF2F0u))(R, world);
    if (env != 0 && !left) ((EnvFn)(g_world.game + 0x1009890u))(env, 5);
    /* 0x67: the four textures and their names */
    static const struct { uint32_t name, at, flag; } kFree[4] = {{0xA2003184u, 0x98, 0xE8}, {0x1BB41B90u, 0xA0, 0xE9}, {0x2FFE957Du, 0xB8, 0xEC}, {0xEBD2B190u, 0xD8, 0xF0}};
    for (int k = 0; k < 4; k++)
    {
        uint64_t t = 0;
        SafeRead(R + kFree[k].at, &t, 8);
        if (t != 0) { unpublish(kFree[k].name); release(t); }
        *(volatile uint64_t*)(R + kFree[k].at) = 0; *(volatile uint8_t*)(R + kFree[k].flag) = 0;
    }
    MovePackageLater(); /* not now: the destroys above are still in the world's command list */
    uint64_t arena = 0;
    memcpy(&arena, g_move.ctx, 8);
    typedef void (*ArenaFn)(uint64_t arena);
    if (arena != 0) ((ArenaFn)(g_world.game + 0x173BAB0u))(arena);
    return 1;
}

/* 0x65's cleanup queues one destroy per live layer unit (exe 0xE1200 -> 0x12D2D0) and up to 0x1000 stamp levels a call;
 * the world's list holds 8192 and the frame's drain empties it. A call runs only while the list is below the engine's
 * own "many destroys" mark (0x800, world interface +0x78), one a frame. */
#define MOVE_DESTROY_ROOM 0x800

/* The rest of a failed build, once its stamp terrains are away. */
static void MoveFailRest(void)
{
    NearShapeJoin("a failed build"); /* the terrain below may be destroyed: its shape job first */
    NearMatCopyFinish(); /* and its readback's rest, into the terrain's memory */
    MoveStampUnitsDestroy(1e30);
    MoveTeardown(0);
    if (g_move.failState >= MV_SPAWN && g_near.handle != 0 && g_near.handle != g_move.dropHandle)
    {
        typedef void (*DestroyFn)(uint64_t world, uint64_t handle);
        DestroyFn destroy = (DestroyFn)NearApi(0x08, 0x1E0);
        /* a shape job stuck in this terrain's steps still writes its object: the terrain is left, not freed under it */
        if (g_shape.stuck && g_shape.handle == g_near.handle) Note("difficulty: moving tiles: the failed tile's terrain is left in the world: its shape job is stuck in it");
        else if (destroy != NULL) { destroy(MoveWorld(), g_near.handle); g_near.handle = 0; }
    }
    if (g_move.stashed)
    {
        MoveFreeLater(g_near.tex[0] != g_move.dropTex[0] ? g_near.tex[0] : 0, g_near.tex[1] != g_move.dropTex[1] ? g_near.tex[1] : 0,
            g_matband.tex != g_move.dropBand ? g_matband.tex : 0, g_matband.pixels != g_move.dropBandPixels ? g_matband.pixels : NULL);
    }
    MovePackageLater();
    MoveCellSkip(g_move.cell[0], g_move.cell[1], GetTickCount64());
    MoveRestoreDrop();
    g_move.failed++;
    g_move.state = MV_IDLE;
}

/* A build that did not finish: the merge torn down, the new tile's maps and terrain gone, the drop tile's back. With
 * stamp terrains spawned, 0x65 takes them away first, one call a frame (MV_FAILING), as MV_UNSTAMP does. */
static void MoveFail(const char* why)
{
    Note("difficulty: moving tiles: the tile of cell (%d, %d) is not built: %s", g_move.cell[0], g_move.cell[1], why);
    Split63Fail(why);
    if (g_nearActive) NearReplayEnd(0);
    if (g_move.state != MV_FAILING) g_move.failState = g_move.state;
    if (g_move.stamped && g_move.state != MV_FAILING && MoveStampsRecord(g_near.at65, 0x65))
    {
        g_move.waits = 0; g_move.ringWaits = 0; g_move.state = MV_FAILING;
        return;
    }
    if (g_move.stamped) Note("difficulty: moving tiles: the stamp terrains were not all taken away");
    g_move.stamped = 0;
    if (g_move.stTemp != 0 && g_move.state != MV_FAILING) { g_move.ringWaits = 0; g_move.state = MV_FAILING; return; } /* the stamp units wait for the ring */
    MoveFailRest();
}

/* A streamed stamp the streamer spawned and could take out (as its own removal tests), standing within `reach` of
 * (cx, cy) (max norm, world metres). */
static int MoveStampMovable(uint32_t i, float cx, float cy, float reach)
{
    return g_streamDeferred[i] && g_streamLive[i] && g_streamPoseGood[i] && !g_streamNeedsUnit[i] && g_streamUnit[i] != 0 && !g_streamMission[i] &&
        fabsf(g_streamAt[i][0] - cx) <= reach && fabsf(g_streamAt[i][1] - cy) <= reach;
}

/* A stamp is put back only when the ground under it moved more than this. Its units already stand up to 0.5 m off the
 * drawn ground: they read the nearest 1 m grid sample, up to 0.71 m away, which is 0.5 m of height on a 35 degree slope. */
#define MOVE_STAMP_MOVED 0.5f
#define MOVE_STAMP_STEP 4.0f   /* m between the footprint's samples: the coarse terrain's spacing, under which a tile's
                                * own relief is what differs */
#define MOVE_STAMP_SIDE 17     /* samples a side at most (a 64 m footprint at MOVE_STAMP_STEP) */

/* Whether the ground under stream slot i moved by more than MOVE_STAMP_MOVED when the tile in `slot` came (gone NULL)
 * or went (gone: its copy). Before and after are what its units read (the grid chosen by its level unit, the stamp's
 * pose, else the level's heightfield) over its footprint (record +0xB0, map metres). *side: the footprint's side. */
/* Stream slot i's footprint in world metres (record +0xB0, map metres), else foot_band_min around its pose:
 * f = x0, x1, y0, y1. Returns its longer side. */
static float MoveStampFootprint(uint32_t i, float f[4])
{
    const float x = g_streamAt[i][0], y = g_streamAt[i][1];
    float box[4];
    if (SafeRead((uintptr_t)g_stream.array + (size_t)i * kStreamRecord + 0xB0, box, sizeof box) || !isfinite(box[0]) || !isfinite(box[1]) ||
        !isfinite(box[2]) || !isfinite(box[3]))
        box[0] = box[2] = x + g_stream.half, box[1] = box[3] = y + g_stream.half;
    f[0] = fminf(box[0], box[2]) - g_stream.half; f[1] = fmaxf(box[0], box[2]) - g_stream.half;
    f[2] = fminf(box[1], box[3]) - g_stream.half; f[3] = fmaxf(box[1], box[3]) - g_stream.half;
    if (f[1] - f[0] < 1.0f) { f[0] = x - 16.0f; f[1] = x + 16.0f; }
    if (f[3] - f[2] < 1.0f) { f[2] = y - 16.0f; f[3] = y + 16.0f; }
    return fmaxf(f[1] - f[0], f[3] - f[2]);
}

/* The replace pick's key for a stamp `side` m across: the distance its meshes are drawn to, squared. */
static float MoveStampSeen2(float side)
{
    float seen = WindowVisible(side * 1.41421356f);
    return seen * seen > 1.0f ? seen * seen : 1.0f;
}

static int MoveStampGroundMoved(uint32_t i, int slot, const NearTile* gone, float cx, float cy, float* side)
{
    const float x = g_streamAt[i][0], y = g_streamAt[i][1];
    float f[4];
    *side = MoveStampFootprint(i, f);
    const float x0 = f[0], x1 = f[1], y0 = f[2], y1 = f[3];
    const int skip = gone != NULL ? -1 : slot;
    const int before = NearLayerGridWith(x, y, skip, gone != NULL ? gone->centre : NULL), after = NearLayerGridWith(x, y, -1, NULL);
    if (!before && !after) return 0; /* the level's heightfield both times */
    const float span = NEAR_SPAN / 2;
    if (before == after && (x1 < cx - span || x0 > cx + span || y1 < cy - span || y0 > cy + span)) return 0; /* the drawn ground moves only there */
    int nx = (int)ceilf((x1 - x0) / MOVE_STAMP_STEP) + 1, ny = (int)ceilf((y1 - y0) / MOVE_STAMP_STEP) + 1;
    nx = nx > MOVE_STAMP_SIDE ? MOVE_STAMP_SIDE : nx; ny = ny > MOVE_STAMP_SIDE ? MOVE_STAMP_SIDE : ny;
    for (int r = 0; r < ny; r++)
        for (int c = 0; c < nx; c++)
        {
            float px = x0 + (x1 - x0) * (float)c / (float)(nx - 1), py = y0 + (y1 - y0) * (float)r / (float)(ny - 1);
            float was = before ? NearLayerGroundWith(px, py, skip, gone) : NearLevelFieldAt(px, py);
            float now = after ? NearLayerGround(px, py) : NearLevelFieldAt(px, py);
            if (fabsf(now - was) > MOVE_STAMP_MOVED) return 1;
        }
    return 0;
}

/* The streamed stamps whose ground a tile's coming or going moved. Their layer units took their heights from the
 * ground as drawn when they spawned, so they float or sink by the difference. Each one the streamer spawned is
 * flagged; its scan takes it out (the least seen first, a few a pass) and it comes back on the ground as drawn now: a
 * window stamp enters its slot again from the plan, a level location is due again. A stamp whose ground stayed within
 * MOVE_STAMP_MOVED stays (taking every one in reach out made 1568 stamps pop over 12 s for one tile). The mission's
 * stamps and the start area's content are never streamed. Game thread. Returns how many were flagged. */
static int MoveLayerWait(void);
static uint32_t MoveStampsReplace(int slot, const NearTile* gone, float cx, float cy, const char* why)
{
    if (g_streamInstallState != 1 || g_streamInside) return 0;
    const float reach = NEAR_SPAN / 2 + NEAR_LAYER_REACH;
    uint32_t flagged = 0, inReach = 0;
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    AcquireSRWLockExclusive(&g_streamLock);
    if (g_stream.captured && g_stream.generatorDone && g_stream.level == g_worldLevel && g_stream.levels == g_world.levels)
        for (uint32_t i = 0; i < g_stream.count && i < kStreamMaxLocations; i++)
        {
            if (!MoveStampMovable(i, cx, cy, reach)) continue;
            inReach++;
            float side = 0.0f;
            if (!MoveStampGroundMoved(i, slot, gone, cx, cy, &side)) continue;
            g_streamSeen2[i] = MoveStampSeen2(side);
            g_streamReplace[i] = 1;
            flagged++;
        }
    ReleaseSRWLockExclusive(&g_streamLock);
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    if (inReach)
        Note("difficulty: moving tiles: %s at (%.0f, %.0f): %u of %u streamed stamps in reach stand on ground that moved over %.1f m; they go out and come back on it (%u a scan, the least seen first; %.1f ms)",
            why, (double)cx, (double)cy, flagged, inReach, (double)MOVE_STAMP_MOVED, (unsigned)kStreamReplaceBatch,
            (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
    return flagged;
}

/* The stamps on moved ground are flagged once the grids hold the new ground (NearLayerTileChangeLater's fills): a
 * stamp put back before would read the old. `gone`'s copy is kept: its ground is the tile's slot buffer, which no
 * build reuses while a change is pending (MoveLayerWait). */
static void MoveReplaceLater(int slot, const NearTile* gone, float cx, float cy, const char* why)
{
    g_moveReplace.slot = slot; g_moveReplace.hasGone = gone != NULL; if (gone) g_moveReplace.gone = *gone;
    g_moveReplace.cx = cx; g_moveReplace.cy = cy; g_moveReplace.why = why; g_moveReplace.on = 1;
    MoveLayerWait();
}

/* 1 while a tile change's grids fill; the stamps' flags follow when they are done. Game thread. */
static int MoveLayerWait(void)
{
    if (NearLayerBusy()) return 1;
    if (g_moveReplace.on)
    {
        g_moveReplace.on = 0;
        MoveStampsReplace(g_moveReplace.slot, g_moveReplace.hasGone ? &g_moveReplace.gone : NULL, g_moveReplace.cx, g_moveReplace.cy, g_moveReplace.why);
    }
    return 0;
}

static float MoveCoarseHere(uintptr_t data, float wx, float wy);
static float MoveCoarseGround(float x, float y);
/* After a raise: the ground query (0x13CA380, from z + 500 m down 1000 m) at a 4 x 4 grid inside the square against the
 * level's coarse rows: how many hit within 0.5 m (audit 1g, 9: lw87a / lw92a found no collision in a released square). */
static void MoveRaiseCheck(float cx, float cy, int cellX, int cellY)
{
    uintptr_t game = (uintptr_t)GetModuleHandleW(L"game.dll"), world = 0, gen = 0, coarse = 0; uint32_t dims[3] = {0};
    if (!game || !EvpPtr(game + 0x346D508u, &world) || SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0 || SafeRead(gen + 0x8758A8u + 0xD8, dims, 12) ||
        dims[0] != 4096 || SafeRead(gen + 0x8758A8u + 0xD8 + 0x10, &coarse, 8) || coarse == 0) return;
    typedef uint8_t (*GroundFn)(uintptr_t world, const float* in, float* outZ, uint8_t reject, float* normal, uint8_t mode);
    int hit = 0, miss = 0, off = 0; float worst = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
        {
            float x = cx + ((float)i - 1.5f) * NEAR_HIDE / 4.5f, y = cy + ((float)j - 1.5f) * NEAR_HIDE / 4.5f, in[3] = {x, y, 0}, z = NAN;
            ((GroundFn)(game + 0x13CA380u))(world, in, &z, 0, NULL, 0);
            float want = MoveCoarseHere(coarse, x, y);
            if (!isfinite(z)) miss++;
            else if (fabsf(z - want) <= 0.5f) hit++;
            else { off++; if (fabsf(z - want) > worst) worst = fabsf(z - want); }
        }
    Note("difficulty: moving tiles: the released square of cell (%d, %d): the ground query at 16 points: %d on the coarse ground, %d elsewhere (worst %.1f m: props or over it), %d with no collision",
        cellX, cellY, hit, off, (double)worst, miss);
}

/* A release's parts on the game thread (QPC ticks): the raise, the hide union, the layer change and replacement queued,
 * the destroy, its own fence wait inside it (0x1ADFB3, timed by xl_free.c's 0x106030 hook; -1 untimed), the frees
 * queued, the ground check: which of them holds the 18-20 ms release frame. */
enum { MR_RAISE, MR_UNION, MR_LATER, MR_DESTROY, MR_FREE, MR_CHECK, MR_PARTS };
static struct { int64_t part[MR_PARTS]; volatile LONG64 fence; int fenceTimed; } g_moveRelease;

static void MoveRelease(int slot)
{
    NearTile t = g_tiles[slot];
    uint64_t world = MoveWorld();
    LARGE_INTEGER at[MR_PARTS + 1];
    memset(g_moveRelease.part, 0, sizeof g_moveRelease.part);
    InterlockedExchange64(&g_moveRelease.fence, 0);
    NearTileRemove(slot);
    g_tiles[slot].tex[0] = g_tiles[slot].tex[1] = g_tiles[slot].band = 0; g_tiles[slot].bandPixels = NULL;
    QueryPerformanceCounter(&at[0]);
    int raised = NearRaiseCoarse(world, &g_tileHidden[slot]);
    g_tileHidden[slot].lowered = 0;
    QueryPerformanceCounter(&at[1]);
    NearHideUnion(world, "a tile released");
    QueryPerformanceCounter(&at[2]);
    NearLayerTileChangeLater(slot, 0, t.centre[0], t.centre[1]);
    MoveReplaceLater(slot, &t, t.centre[0], t.centre[1], "a tile went");
    QueryPerformanceCounter(&at[3]);
    typedef void (*DestroyFn)(uint64_t world, uint64_t handle);
    DestroyFn destroy = (DestroyFn)NearApi(0x08, 0x1E0);
    g_move.destroyTicks = -1;
    if (destroy != NULL && t.handle != 0)
    {
        LARGE_INTEGER d0, d1; QueryPerformanceCounter(&d0);
        destroy(world, t.handle);
        QueryPerformanceCounter(&d1);
        g_move.destroyTicks = d1.QuadPart - d0.QuadPart;
        g_tiles[slot].handle = 0; /* its object is freed: a later terrain may get the address */
    }
    QueryPerformanceCounter(&at[4]);
    MoveFreeLater(t.tex[0], t.tex[1], t.band, t.bandPixels);
    g_move.released++;
    QueryPerformanceCounter(&at[5]);
    if (raised) MoveRaiseCheck(t.centre[0], t.centre[1], t.cell[0], t.cell[1]);
    QueryPerformanceCounter(&at[6]);
    for (int k = 0; k < MR_PARTS; k++) g_moveRelease.part[k] = at[k + 1].QuadPart - at[k].QuadPart;
    Note("difficulty: moving tiles: the tile of cell (%d, %d) released (no player within %.0f m for %u s); the coarse collision %s; %d tiles live",
        t.cell[0], t.cell[1], (double)MOVE_KEEP, MOVE_KEEP_MS / 1000, raised ? "raised back" : "NOT raised back (the level's copy did not read)", NearTilesLive());
    if (g_allocOn > 0 || MemProbeOn()) WorldMemoryNote("a tile released", 0xFFFFFFFFu, 0, 1); /* research (HELLFORGE_XL_ALLOCS=1 or MEMPROBE=1): what a release gives back */
    MoveTrimEvent("a tile released");
}

/* The context's arena ([ctx+0]): the generation thread's arena is destroyed when the level is done (0x173BB80: its
 * chunks freed, its head zeroed; the allocator 0x173B8D0 then reads [0] at 0x173B936, run s72d's 0x2F fault). The
 * build gets its own, laid out as 0x173B700 makes one: arena {head chunk +0, lock +8 (0), owner thread index +0xC (-1:
 * the allocator skips the index call), growth size +0x10 (0x100000)}; chunk {used +0, data +8, capacity +0x10, next
 * +0x18}. The reset (0x173BAB0) frees the chunks the allocator grew (engine memory, put in front of ours) and sets the
 * last one's used to 0: ours is the last and is never freed. */
#define MOVE_ARENA_BYTES (64u << 20)
static struct { uint64_t head; LONG lock; int32_t owner; uint64_t growth; } g_moveArena;
static struct { uint64_t used, data, capacity, next; } g_moveChunk;

static int MoveArena(void)
{
    if (g_moveChunk.data == 0)
    {
        void* p = VirtualAlloc(NULL, MOVE_ARENA_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p == NULL) { Note("difficulty: moving tiles: no memory for the build's arena"); return 0; }
        g_moveChunk.data = (uint64_t)(uintptr_t)p; g_moveChunk.capacity = MOVE_ARENA_BYTES;
    }
    g_moveChunk.used = 0; g_moveChunk.next = 0;
    g_moveArena.head = (uint64_t)(uintptr_t)&g_moveChunk; g_moveArena.lock = 0; g_moveArena.owner = -1; g_moveArena.growth = 0x100000;
    return 1;
}

static void MoveGroundStart(int slot);
static int MoveStart(void)
{
    uint64_t block = 0;
    if (SafeRead(g_worldLevel + 0x2A8, &block, 8) || block == 0 || block != g_near.records) return 0;
    memcpy(g_move.ctx, (void*)(uintptr_t)(block + 0x19B008u), sizeof g_move.ctx);
    uint64_t settings = 0;
    memcpy(&settings, g_move.ctx + 8, 8);
    if (settings == 0 || SafeRead(settings, g_move.settings, sizeof g_move.settings)) return 0;
    uint64_t mine = (uint64_t)(uintptr_t)g_move.settings;
    memcpy(g_move.ctx + 8, &mine, 8);
    g_move.ctx[0xE1] = 0; g_move.ctx[0xE2] = 0; /* abort, restart */
    if (!MoveArena()) return 0;
    { uint64_t arena = (uint64_t)(uintptr_t)&g_moveArena; memcpy(g_move.ctx, &arena, 8); }
    /* the drop tile's maps aside: the replay creates the new tile's in g_near / g_matband */
    g_move.dropTex[0] = g_near.tex[0]; g_move.dropTex[1] = g_near.tex[1]; g_move.dropHandle = g_near.handle;
    g_move.dropCentre[0] = g_near.centre[0]; g_move.dropCentre[1] = g_near.centre[1];
    MoveBandStash();
    g_move.stashed = 1;
    g_near.tex[0] = g_near.tex[1] = 0; g_matband.tex = 0; g_matband.pixels = NULL;
    InterlockedExchange(&g_moveBuilding, 1);
    MoveGroundStart(g_move.slot);
    g_move.sel.on = 0;
    g_move.acquired = 0; g_move.merge = 0; g_move.cutNoted = 0; g_move.shipSeen = 0; g_move.waits = 0; g_move.frames = 0; g_move.started = GetTickCount64();
    g_move.state = MV_PACKAGE;
    Note("difficulty: moving tiles: building cell (%d, %d) at (%.0f, %.0f) for a player %.0f m from it; %d tiles live", g_move.cell[0], g_move.cell[1],
        (double)g_move.centre[0], (double)g_move.centre[1], (double)MoveCellDistance(g_move.cell[0], g_move.cell[1], g_move.player[0][0], g_move.player[0][1]),
        NearTilesLive());
    return 1;
}

/* The replay's merge state for the cell (as TerrainNearReplay's first call sets it up at load). */
static int MoveReplayBegin(void)
{
    uintptr_t gen = 0, lg = 0;
    uint64_t settings = 0;
    uint32_t size = 0, tiles = 0;
    memcpy(&settings, g_move.ctx + 8, 8);
    if (SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0) return 0;
    lg = gen + 0x8758A8u;
    g_near.lg = lg; g_near.settings = settings;
    if (SafeRead(gen, &g_near.R, 8) || g_near.R == 0 || SafeRead(g_near.R + 0x110, &g_near.gui, 8) || SafeRead(g_near.R, &g_near.cam[0], 8) ||
        SafeRead(g_near.R + 8, &g_near.node[0], 4) || SafeRead(g_near.R + 0x10, &g_near.cam[1], 8) || SafeRead(g_near.R + 0x18, &g_near.node[1], 4) ||
        SafeRead(settings + 0x2C, &size, 4) || SafeRead(lg + 0x120, &g_near.samples, 4) || SafeRead(lg + 0x124, &tiles, 4) || g_near.cam[0] == 0 ||
        g_near.cam[1] == 0 || g_near.gui == 0)
    { Note("difficulty: moving tiles: the merge was not re-created (R 0x%llX)", (unsigned long long)g_near.R); return 0; }
    g_near.world = MoveWorld();
    if (size != 16384u || tiles != 1 || !(g_near.samples >= 1024.0f && g_near.samples <= 8192.0f) || g_near.world == 0)
    { Note("difficulty: moving tiles: the merge is not the reviewed one (size %u, %u tiles, %.0f samples)", size, tiles, (double)g_near.samples); return 0; }
    g_near.centre[0] = g_move.centre[0]; g_near.centre[1] = g_move.centre[1];
    g_near.world_size = (float)size;
    g_near.scale = g_near.world_size / NEAR_SIDE;
    for (int k = 0; k < 2; k++) g_near.offset[k] = -(g_near.centre[k] + g_near.world_size / 2 - NEAR_SIDE / 2) * g_near.samples / NEAR_SIDE;
    if (!NearPlan(g_near.records, g_near.at65)) { Note("difficulty: moving tiles: the level's merge records are not all there"); return 0; }
    g_near.step = 0; g_near.calls = 0; g_near.building = 0; g_near.tilesDrawn = 0; g_near.tilesFailed = 0; g_near.waits = 0; g_near.frames = 0;
    g_near.matLeave = 0; g_near.handle = 0;
    g_near.started = GetTickCount64();
    g_stepDump.phase = 0;
    g_near.state = NEAR_START; g_near.loggedStep = 0; g_near.stampLeave = 0;
    MoveHold(1);
    return 1;
}

/* After the finalize: the coarse terrain under the tile, its ground, the tile in the list. */
/* The "ready" frame's parts (ms), for the build's cost line: the game's ready call and finalize, the coarse square
 * hidden, the tile's heights copied, its band taken, the hide union, the layer change started, the stamps flagged. */
static double g_moveReadyMs[7];
static double MoveMsSince(LARGE_INTEGER* t)
{
    LARGE_INTEGER now, hz; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&hz);
    double ms = (double)(now.QuadPart - t->QuadPart) * 1000.0 / (double)hz.QuadPart;
    *t = now;
    return ms;
}

/* A slot's first heights copy (NearGroundOf) went into a fresh 32 MiB buffer, every page faulted in on the ready frame
 * (14.8 ms in game against 1.2 ms into touched memory). The buffer is made and touched on a thread of its own while
 * the tile builds; the ready state waits for it and the first slot without a buffer takes it. */
static struct { HANDLE thread; volatile LONG done; uint16_t* buf; } g_groundPre;
static DWORD WINAPI MoveGroundFault(LPVOID unused)
{
    (void)unused;
    const size_t bytes = (size_t)4096 * 4096 * 2;
    volatile uint8_t* b = (volatile uint8_t*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (size_t i = 0; b != NULL && i < bytes; i += 4096) b[i] = 0;
    g_groundPre.buf = (uint16_t*)b;
    MemoryBarrier();
    InterlockedExchange(&g_groundPre.done, 1);
    return 0;
}
static void MoveGroundStart(int slot)
{
    if (slot < 0 || slot >= NEAR_TILES_MAX || g_groundBuf[slot] != NULL || g_groundPre.thread != NULL || g_groundPre.buf != NULL) return;
    InterlockedExchange(&g_groundPre.done, 0);
    g_groundPre.thread = CreateThread(NULL, 0, MoveGroundFault, NULL, 0, NULL); /* none: NearGroundOf makes its own */
}
/* 1 while the buffer is being touched (a frame at a time); then `slot` gets it if it has none. */
static int MoveGroundWait(int slot);
/* MV_READY waits for the last layer change's fill (a thread, about 1.3 s of work on 4 threads below normal priority,
 * plus at most 8 x 200 ms for spawns reading its grids) and the ground prefault (8192 page touches, ~14 ms). 60 s is
 * many times both: past it the thread is hung, and the build fails rather than wait for the level's end. Counted from
 * when the current wait began: a release in the state may start a healthy change late. */
#define MOVE_READY_WAIT_MS 60000u
/* MV_READY's waits: NULL when both are done, "" to wait a frame more, else why the build fails. */
static const char* MoveReadyWait(ULONGLONG now)
{
    const int layer = MoveLayerWait();
    if (!layer && !MoveGroundWait(g_move.slot)) { g_move.waitSince = 0; return NULL; }
    if (g_move.waitSince == 0) g_move.waitSince = now;
    if (now - g_move.waitSince <= MOVE_READY_WAIT_MS) return "";
    return layer ? "the layer change did not finish" : "the ground buffer did not finish";
}

static int MoveGroundWait(int slot)
{
    if (g_groundPre.thread != NULL)
    {
        if (!g_groundPre.done) return 1;
        WaitForSingleObject(g_groundPre.thread, INFINITE); /* past its last store */
        CloseHandle(g_groundPre.thread); g_groundPre.thread = NULL;
    }
    if (slot >= 0 && slot < NEAR_TILES_MAX && g_groundBuf[slot] == NULL && g_groundPre.buf != NULL) { g_groundBuf[slot] = g_groundPre.buf; g_groundPre.buf = NULL; }
    return 0;
}

static void MoveTileIn(uint64_t world)
{
    int slot = g_move.slot, x0 = 0, y0 = 0;
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    NearCellSquare(g_move.cell[0], g_move.cell[1], g_move.centre[0], g_move.centre[1], &x0, &y0);
    NearHideCoarse(world, x0, y0, &g_tileHidden[slot]);
    g_moveReadyMs[1] = MoveMsSince(&t);
    NearGround ground;
    NearGroundOf(world, g_near.handle, NEAR_SPAN, g_move.centre[0], g_move.centre[1], &ground, &g_groundBuf[slot]);
    NearTileAdd(slot, g_move.centre[0], g_move.centre[1], g_near.handle, &ground);
    g_moveReadyMs[2] = MoveMsSince(&t);
    g_tiles[slot].cell[0] = g_move.cell[0]; g_tiles[slot].cell[1] = g_move.cell[1];
    g_tiles[slot].tex[0] = g_near.tex[0]; g_tiles[slot].tex[1] = g_near.tex[1];
    MoveTileTakeBand(slot);
    g_moveReadyMs[3] = MoveMsSince(&t);
    NearHideUnion(world, "a tile built in mission");
    g_moveReadyMs[4] = MoveMsSince(&t);
    NearLayerTileChangeLater(slot, 1, g_move.centre[0], g_move.centre[1]);
    g_moveReadyMs[5] = MoveMsSince(&t);
    MoveReplaceLater(slot, NULL, g_move.centre[0], g_move.centre[1], "a tile is in");
    g_moveReadyMs[6] = MoveMsSince(&t);
    g_move.built++;
    Note("difficulty: moving tiles: the tile of cell (%d, %d) is in after %.1f s (%u frames): ground %s, centre %.2f m (coarse %.2f m); %d tiles live; craters held %lld updates",
        g_move.cell[0], g_move.cell[1], (double)(GetTickCount64() - g_move.started) / 1000.0, g_move.frames, ground.v != NULL ? "read" : "NOT read",
        (double)NearGroundAt(&ground, g_move.centre[0], g_move.centre[1]), (double)NearGroundAt(&g_nearCoarseGround, g_move.centre[0], g_move.centre[1]),
        NearTilesLive(), (long long)g_moveHeldUpdates);
    if (g_allocOn > 0 || MemProbeOn()) WorldMemoryNote("a tile in", 0xFFFFFFFFu, 0, 1); /* research (HELLFORGE_XL_ALLOCS=1 or MEMPROBE=1): what a build keeps */
    MoveRestoreDrop(); /* g_near.tex / g_matband: the drop tile's again (the new tile's are in its entry) */
    MoveTrimEvent("a tile in");
}

/* ---- Units left under the ground (living world audit 1f criterion 7) ----------------------------------------------
 * KB enemies/living-world-tile-switch-falls (subagent; checked by me in game.dll): the motion loop 0x59F440 gives the
 * records below [mgr+0x4890] (mgr [game+0x3326558]) the step 0x59FED0; the mode is +0x1C of the record's 0x2C entry at
 * [mgr+0x48D8] (0x5A03B2; jump table 0x5A1884: modes 4 and 7 -> 0x5A0B70), its active byte +0x20 (0x59FF3D). Those
 * modes look the entity up in the ground system [game+0x3326440] (hash header +0x28, records [+0x48] of 0x3C bytes;
 * 0x5A0B70..0x5A0BF8, 0x5A0D22) and read its ray's hit flag +0x38: hit -> z := the hit's z, miss -> gravity. The ray
 * starts `up` (+0x2C) over the unit, straight down. The step's output position is the 0xA4 record +0..+8 at
 * [mgr+0x48D0] (0x5A16D5 / 0x5A16DE); the authority bit is [descriptor+0x14] bit 0 (0x5A2D2C). A heightfield has one
 * surface, so a unit left under it never meets it again and falls to the motion bound: the coarse collision lowered
 * under a new tile's square while it stood on the coarse ground below the tile (lw89a: 7 of 7 falls began 0.2-0.35 s
 * after NearHideCoarse), the coarse raised back over the tile at release, or a step on a tile into a rise taller than
 * `up`. Each frame, before the ray is cast (MoveTick runs in the level update at 0xAB519E, before the cast at 0xAB5597
 * and the world update at 0xAB5EC9): a unit in mode 4 or 7, active, ours to move, whose last ray missed and that is
 * more than MOVE_KEEP_UNDER under the ground there (the live tile's heights inside its square, else the level's coarse
 * ones) is put back on it by the game's teleport 0x5A2BB0, as the test teleport does. A missed ray means gravity, so
 * these are exactly the units vanilla would let fall; nothing standing is touched. Vanilla units too: garrisons and
 * objective units standing on the coarse ground when a tile comes are hit the same way. */
#define MOVE_KEEP_UNDER 0.3f
#define MOVE_KEEP_RECORDS 8192
static uint8_t g_keepModes[MOVE_KEEP_RECORDS * 0x2C], g_keepPos[MOVE_KEEP_RECORDS * 0xA4];
static struct { int pins; LONG level; uint32_t lifted, fromTile, fromCoarse, held, refused, settled; uint64_t frame; } g_keep;
/* Each unit put back, by entity: its lift count and whether the collision under it was found (for the notes).
 * KB living-world-tile-switch-falls section 6 (subagent; checked by me in game.dll): the game's teleport 0x5A2BB0 writes
 * the position but never the velocity, the 0x84 record at [mgr+0x48C8] (+8 vz: the no-hit branch adds gravity to it at
 * 0x5A101F, only the grounded branch writes it, at 0x5A10AA). lw92a's squad 1913 kept its free-fall speed through 2850
 * lifts: each one was undone by the next frame's fall at that speed (the drop grew g dt per frame, vz = g (t - 11.8 s)).
 * So a lift also sets vz = 0, and is made onto the collision as it is: the ground query (0x13CA380, as the teleport's own
 * check 0x909B20 uses it) cast down from 1.5 m over the expected ground; when it hits within MOVE_KEEP_FIND of the
 * expected ground the unit goes 5 cm over the hit, else (no collision there: lw92a's released square, under study) it is
 * held 10 cm over the expected ground, vz 0, every frame it would fall. No caps: a unit under the ground with a missed
 * ray is always falling (P). The 3843ca65 rules (only while falling, three tries) are gone. */
#define MOVE_KEEP_FIND 3.0f
#define MOVE_KEEP_TRACKS 1024
static struct { uint32_t entity; uint32_t lifts; uint8_t noCollision; } g_keepTrack[MOVE_KEEP_TRACKS];
static int MoveKeepTrack(uint32_t e)
{
    uint32_t h = (e * 2654435761u) % MOVE_KEEP_TRACKS;
    for (uint32_t k = 0; k < MOVE_KEEP_TRACKS; k++, h = (h + 1) % MOVE_KEEP_TRACKS)
    {
        if (g_keepTrack[h].entity == e) return (int)h;
        if (g_keepTrack[h].entity == 0) { memset(&g_keepTrack[h], 0, sizeof g_keepTrack[h]); g_keepTrack[h].entity = e; return (int)h; }
    }
    return -1;
}

/* The level's coarse heights (lg+0xD8, the rows the coarse collision was made from and NearRaiseCoarse writes back) at a
 * point, on the triangles NearCoarseTri uses; -9999 outside or unreadable. */
static float MoveCoarseHere(uintptr_t data, float wx, float wy)
{
    const float step = 16383.5f / 4095.0f;
    float fx = (wx + 8191.75f) / step, fy = (8191.75f - wy) / step;
    if (!(fx >= 0 && fy >= 0 && fx < 4095.0f && fy < 4095.0f)) return -9999.0f;
    uint32_t c = (uint32_t)fx, r = (uint32_t)fy;
    float ax = fx - c, ay = fy - r;
    uint16_t no[2], so[2];
    if (SafeRead(data + ((uint64_t)r * 4096 + c) * 2, no, 4) || SafeRead(data + ((uint64_t)(r + 1) * 4096 + c) * 2, so, 4)) return -9999.0f;
    float nw = no[0], ne = no[1], sw = so[0], se = so[1];
    float z = ax <= ay ? sw + ax * (se - sw) + (1 - ay) * (nw - sw) : ne + (1 - ax) * (nw - ne) + ay * (se - ne);
    return z / 65535.0f * 1000.0f - 500.0f;
}

/* The ground as drawn at a point on an extra large level with moving tiles: the live tile's heights inside its hidden
 * square, else the level's coarse rows (lg+0xD8); *tile the slot or -1; -9999 when it does not read. */
static float MoveDrawnGround(float x, float y, int* tile)
{
    *tile = -1;
    if (!(g_moveInstalled && g_move.on && g_move.level == g_world.levels)) return -9999.0f; /* moving tiles only: the coarse rows read
        * below are the extra large level's (audit 1h, 15) */
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].ground.v != NULL &&
            fabsf(x - g_tiles[k].centre[0]) < NEAR_HIDE / 2 && fabsf(y - g_tiles[k].centre[1]) < NEAR_HIDE / 2)
        { *tile = k; return NearGroundAt(&g_tiles[k].ground, x, y); }
    return MoveCoarseGround(x, y);
}
/* The level's coarse rows (lg+0xD8) at a point, under a live tile too: the coarse collision's height where it is not
 * lowered; -9999 when it does not read. */
static float MoveCoarseGround(float x, float y)
{
    uintptr_t gen = 0, coarse = 0; uint32_t dims[3] = {0};
    if (g_worldLevel == 0 || SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0 || SafeRead(gen + 0x8758A8u + 0xD8, dims, 12) || dims[0] != 4096 ||
        dims[1] != 4096 || NearTexelBytes(dims[2]) != 2 || SafeRead(gen + 0x8758A8u + 0xD8 + 0x10, &coarse, 8) || coarse == 0) return -9999.0f;
    return MoveCoarseHere(coarse, x, y);
}
/* Moving tiles on for this level (an extra large level whose detailed tiles follow the players). */
static int MoveTilesOn(void) { return g_moveInstalled && g_move.on && g_move.level == g_world.levels; }
/* A point inside the square of the cell being built now (its tile comes in at the end, its coarse ground lowered then). */
static int MoveCellBuilding(float x, float y)
{
    return MoveTilesOn() && g_move.state != MV_IDLE && fabsf(x - g_move.centre[0]) < NEAR_HIDE / 2 && fabsf(y - g_move.centre[1]) < NEAR_HIDE / 2;
}

/* Members started over the ground (living_world.c LwPlace: an extra large level's navmesh faces sit metres off the
 * drawn ground, and the squad create starts each member at its face's height): the living world lists a new squad's
 * members for a few seconds, and a listed unit whose ray missed more than MOVE_SETTLE_OVER over the ground is put on it
 * as a buried one is, instead of falling there. Nothing else in the air is touched: jumps, drops and falls stay. */
#define MOVE_SETTLE_OVER 0.5f
#define MOVE_SETTLE_UNITS 64
static struct { uint32_t entity; ULONGLONG until; } g_moveDown[MOVE_SETTLE_UNITS];
static ULONGLONG g_moveDownUntil; /* the latest `until`: nothing is listed after it */
static void MoveSettleAsk(uint32_t e, ULONGLONG until)
{
    int at = -1;
    ULONGLONG now = GetTickCount64();
    if (e == 0) return;
    for (int k = 0; k < MOVE_SETTLE_UNITS; k++)
    {
        if (g_moveDown[k].entity == e) { at = k; break; }
        if (at < 0 && (g_moveDown[k].entity == 0 || g_moveDown[k].until <= now)) at = k;
    }
    if (at < 0) return;
    g_moveDown[at].entity = e; g_moveDown[at].until = until;
    if (until > g_moveDownUntil) g_moveDownUntil = until;
}
static int MoveSettleListed(uint32_t e, ULONGLONG now)
{
    for (int k = 0; k < MOVE_SETTLE_UNITS; k++) if (g_moveDown[k].entity == e) return g_moveDown[k].until > now;
    return 0;
}
/* What the keeper may do with a unit at z over `ground`: 1 put it back up (under the ground), 2 put it down (over it,
 * only while units are listed), 0 nothing. Whether its ray missed and, for 2, whether it is listed are tested after. */
static int MoveKeepWhich(float z, float ground, int listing)
{
    if (ground < -9000.0f) return 0;
    if (z < ground - MOVE_KEEP_UNDER) return 1;
    return listing && z > ground + MOVE_SETTLE_OVER ? 2 : 0;
}
/* The keeper is in: units left under the ground are put back on it. */
static int MoveKeepOn(void) { return g_keep.pins > 0; }

static void MoveGroundKeep(void)
{
    static uintptr_t game;
    if (game == 0) game = (uintptr_t)GetModuleHandleW(L"game.dll");
    if (g_keep.pins == 0)
    {
        g_keep.pins = game && ApiCodeMatches(game + 0x59F478u, "3B 9F 90 48 00 00") && ApiCodeMatches(game + 0x573F5Cu, "44 8B 80 8C 48 00 00") &&
            ApiCodeMatches(game + 0x5A03B2u, "8B 44 10 1C FF C8 83 F8 06") && ApiCodeMatches(game + 0x5A1884u, "D7 03 5A 00 0F 04 5A 00 58 04 5A 00 70 0B 5A 00 04 05 5A 00 B7 10 5A 00 70 0B 5A 00") &&
            ApiCodeMatches(game + 0x59FF3Du, "80 7C 08 20 00") && ApiCodeMatches(game + 0x5A0B79u, "4C 8B 35 C0 58 D8 02") &&
            ApiCodeMatches(game + 0x5A0BE6u, "48 6B F1 3C 49 03 76 48 0F B6 46 38") && ApiCodeMatches(game + 0x5A16D5u, "F2 45 0F 11 6D 00 8B 45 98 41 89 45 08") &&
            ApiCodeMatches(game + 0x5A2C62u, "49 69 F4 A4 00 00 00") && ApiCodeMatches(game + 0x5A2BB0u, "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 40") ? 1 : -1;
        Note(g_keep.pins > 0 ? "difficulty: moving tiles: units left under the ground are put back on it (motion modes 4 and 7)"
                             : "difficulty: moving tiles: the motion step is not the reviewed code; units left under the ground are not put back");
    }
    if (g_keep.pins < 0) return;
    if (g_keep.level != g_world.levels)
    { g_keep.level = g_world.levels; g_keep.lifted = g_keep.fromTile = g_keep.fromCoarse = g_keep.held = g_keep.refused = g_keep.settled = 0; memset(g_moveDown, 0, sizeof g_moveDown); g_moveDownUntil = 0; memset(g_keepTrack, 0, sizeof g_keepTrack); }
    g_keep.frame++;
    ULONGLONG now = GetTickCount64();
    uintptr_t mgr = 0, modes = 0, pos = 0, descs = 0, gsys = 0, grecs = 0, gen = 0, coarse = 0;
    uint32_t dims[3] = {0};
    if (!EvpPtr(game + 0x3326558u, &mgr) || !EvpPtr(mgr + 0x48D8, &modes) || !EvpPtr(mgr + 0x48D0, &pos) || !EvpPtr(mgr + 0x48B8, &descs) ||
        !EvpPtr(game + 0x3326440u, &gsys) || !EvpPtr(gsys + 0x48, &grecs)) return;
    if (!SafeRead(g_worldLevel + 0x2C8, &gen, 8) && gen != 0 && !SafeRead(gen + 0x8758A8u + 0xD8, dims, 12) && dims[0] == 4096 && dims[1] == 4096 &&
        NearTexelBytes(dims[2]) == 2) SafeRead(gen + 0x8758A8u + 0xD8 + 0x10, &coarse, 8);
    uint32_t n = EvpU32(mgr + 0x4890, 0), all = EvpU32(mgr + 0x488C, 0);
    if (n > all) n = all;
    if (n > MOVE_KEEP_RECORDS) n = MOVE_KEEP_RECORDS;
    if (n == 0 || SafeRead(modes, g_keepModes, (size_t)n * 0x2C) || SafeRead(pos, g_keepPos, (size_t)n * 0xA4)) return;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint8_t* m = g_keepModes + (size_t)i * 0x2C;
        uint32_t mode; memcpy(&mode, m + 0x1C, 4);
        if ((mode != 4 && mode != 7) || m[0x20] == 0) continue;
        float p[3]; memcpy(p, g_keepPos + (size_t)i * 0xA4, 12);
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) continue;
        int tile = -1; float ground = -9999.0f;
        for (int k = 0; k < NEAR_TILES_MAX; k++)
            if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].ground.v != NULL &&
                fabsf(p[0] - g_tiles[k].centre[0]) < NEAR_HIDE / 2 && fabsf(p[1] - g_tiles[k].centre[1]) < NEAR_HIDE / 2)
            { tile = k; ground = NearGroundAt(&g_tiles[k].ground, p[0], p[1]); break; }
        if (tile < 0 && coarse) ground = MoveCoarseHere(coarse, p[0], p[1]);
        int which = MoveKeepWhich(p[2], ground, now < g_moveDownUntil);
        if (which == 0) continue;
        uintptr_t d = 0; uint32_t e = 0, gi = 0; uint8_t flags = 0, hit = 1; float ray[2] = {0, 0};
        if (!EvpPtr(descs + 8u * i, &d) || d == 0 || SafeRead(d + 0x14, &flags, 1) || !(flags & 1) || (e = EvpU32(d + 8, 0)) == 0) continue;
        if (!EvpHash(gsys + 0x28, e, 8, 4, &gi) || gi >= 0x100000 || SafeRead(grecs + 0x3Cu * gi + 0x38, &hit, 1) || hit) continue;
        SafeRead(grecs + 0x3Cu * gi + 0x2C, ray, 8);
        if (((uint8_t (*)(void*, uint32_t))(game + 0x4A52B0u))(NULL, e)) continue; /* attached: the teleport stops half-way */
        if (which == 2 && !MoveSettleListed(e, now)) continue;
        int tk = MoveKeepTrack(e);
        float to[3] = {p[0], p[1], ground + 0.1f}, floor_ = NAN;
        uintptr_t world = 0;
        if (EvpPtr(game + 0x346D508u, &world))
        {
            typedef uint8_t (*GroundFn)(uintptr_t world, const float* in, float* outZ, uint8_t reject, float* normal, uint8_t mode);
            float in[3] = {p[0], p[1], ground + 1.5f - 500.0f}; /* the query casts from in z + 500 m down 1000 m */
            ((GroundFn)(game + 0x13CA380u))(world, in, &floor_, 0, NULL, 0);
        }
        int found = isfinite(floor_) && fabsf(floor_ - ground) <= MOVE_KEEP_FIND;
        if (found) to[2] = floor_ + 0.05f;
        ((void (*)(void*, uint32_t, const float*))(game + 0x5A2BB0u))(NULL, e, to);
        uintptr_t vel = 0; float vz = NAN, zero = 0.0f;
        if (EvpPtr(mgr + 0x48C8, &vel) && !SafeRead(vel + 0x84u * i + 8, &vz, 4)) *(volatile float*)(vel + 0x84u * i + 8) = zero;
        if (which == 2)
        {
            if (++g_keep.settled <= 100)
                Note("difficulty: moving tiles: new squad member %08X started %.2f m over the %s at (%.1f, %.1f) (z %.2f, vz %.2f, its ray missed); the collision %s: put down at %.2f, vz 0 (%u this level)",
                    e, (double)(p[2] - ground), tile >= 0 ? "tile's ground" : "coarse ground", (double)p[0], (double)p[1], (double)p[2], (double)vz, found ? "found" : "NOT found within 3 m", (double)to[2], g_keep.settled);
            continue;
        }
        g_keep.lifted++; if (tile >= 0) g_keep.fromTile++; else g_keep.fromCoarse++;
        if (!found) g_keep.held++;
        uint32_t lifts = 0;
        if (tk >= 0) { lifts = ++g_keepTrack[tk].lifts; if (!found && !g_keepTrack[tk].noCollision) { g_keepTrack[tk].noCollision = 1; g_keep.refused++; } }
        if (g_keep.lifted <= 200 || g_keep.lifted % 50 == 0 || lifts == 1 || (!found && lifts % 100 == 1))
            Note("difficulty: moving tiles: unit %08X (motion mode %u) under the %s at (%.1f, %.1f): z %.2f, ground %.2f (%.2f under), vz %.2f, its ray missed (up %.2f, below %.2f); the collision %s: put back at %.2f, vz 0 (its lift %u; %u this level, %u on tiles, %u on the coarse ground, %u without collision under them)",
                e, mode, tile >= 0 ? "tile's ground" : "coarse ground", (double)p[0], (double)p[1], (double)p[2], (double)ground, (double)(ground - p[2]), (double)vz,
                (double)ray[0], (double)ray[1], found ? "found" : "NOT found within 3 m", (double)to[2], lifts, g_keep.lifted, g_keep.fromTile, g_keep.fromCoarse, g_keep.refused);
    }
}

/* Nothing a build reads is a released tile's (TILES-TRIM-PROOFS.md), but while the merge exists its cameras may draw the
 * world: over the tile's own square (+-NEAR_SIDE / 2), over 0x41's vanilla-space square (+-MOVE_MAT_HALF at the
 * origin) and 20 km north for 0x63. A tile whose terrain (+-NEAR_SPAN / 2) is clear of both squares goes then; a
 * nearer one waits for the teardown, so no pass of the build sees the world change. */
#define MOVE_MAT_HALF 1024.0f
static int MoveReleaseClear(int k)
{
    if (g_move.state == MV_IDLE || !g_move.merge) return 1;
    /* 0x38 makes the merge cameras world-sized (+-8,192 m at the origin) and 0x4C's fill draws through cam0; they
     * narrow only when the replay leaves NEAR_START (NearCameras). Until then no tile goes. */
    if (g_move.state == MV_KICK || g_move.state == MV_SETUP || g_move.state == MV_LOOKUP || (g_move.state == MV_REPLAY && g_near.state == NEAR_START)) return 0;
    const float* c = g_tiles[k].centre;
    float build = fmaxf(fabsf(c[0] - g_move.centre[0]), fabsf(c[1] - g_move.centre[1])), origin = fmaxf(fabsf(c[0]), fabsf(c[1]));
    return build >= NEAR_SIDE / 2 + NEAR_SPAN / 2 && origin >= MOVE_MAT_HALF + NEAR_SPAN / 2;
}

static const char* MoveStateName(int state)
{
    static const char* const names[] = {"idle", "package", "stamps", "kick", "setup", "lookup", "replay", "teardown", "unstamp", "spawn", "ready", "failing"};
    return state >= 0 && state <= MV_FAILING ? names[state] : "?";
}

/* Tiles a player is near are kept, building or not. */
static void MoveKeepSeen(ULONGLONG now)
{
    for (int k = 0; k < NEAR_TILES_MAX; k++)
    {
        if (!g_tiles[k].live || g_tiles[k].level != g_world.levels) continue;
        for (int p = 0; p < g_move.players; p++)
            if (MoveCellDistance(g_tiles[k].cell[0], g_tiles[k].cell[1], g_move.player[p][0], g_move.player[p][1]) <= MOVE_KEEP) { g_tiles[k].seen = now; break; }
    }
}

static int MoveReleaseTick(ULONGLONG now); /* xl_free.c: an expired tile's release, in a frame where it waits for nothing */

static void MoveTick(uint64_t level)
{
    if (!g_moveInstalled || level == 0 || level != g_worldLevelLive || g_world.live != 8.0f || !TerrainOn() || !NearOn() || !MoveOn()) return;
    uint32_t state = 0;
    if (SafeRead(level + 0x1E4, &state, 4) || state != 3) return;
    if (g_near.level != g_world.levels || (g_move.state == MV_IDLE && g_near.state != NEAR_IN)) return;
    ULONGLONG now = GetTickCount64();
    if (g_move.level != g_world.levels)
    {
        g_move.level = g_world.levels; g_move.levelPtr = level; g_move.state = MV_IDLE; g_move.on = 0;
        g_move.built = g_move.failed = g_move.released = 0; g_move.pixelsFreed = 0; g_move.drainWaits = 0;
        memset(g_move.skip, 0, sizeof g_move.skip);
        if (!(g_ring.level == g_world.levels && g_ring.applied)) { Note("difficulty: moving tiles: off for this level (no grid ring at load)"); return; }
        if (!g_tiles[0].live || g_tiles[0].level != g_world.levels) { Note("difficulty: moving tiles: off for this level (no drop tile)"); return; }
        g_move.at2F = g_move.at37 = g_move.at38 = g_move.at66 = 0;
        for (uint32_t i = g_near.at65; i > 0; i--)
        {
            uint32_t op = 0;
            if (SafeRead(g_near.records + (uint64_t)(i - 1) * 0x19B0u, &op, 4)) break;
            if (op == 0x38 && g_move.at38 == 0) g_move.at38 = i - 1;
            if (op == 0x37 && g_move.at37 == 0) g_move.at37 = i - 1;
            if (op == 0x2F && g_move.at2F == 0) g_move.at2F = i - 1; /* after 0x38 in the list (s72c: 0x37 67, 0x38 78, 0x2F 102) */
            if (g_move.at2F != 0 && g_move.at37 != 0 && g_move.at38 != 0) break;
        }
        /* 0x4C between 0x38 and 0x65: it makes R+0xB8 (0x2FFE957D), which 0x67 releases and the replay's 0x64 binds */
        g_move.at4C = 0;
        for (uint32_t i = g_move.at38 + 1; i < g_near.at65; i++)
        {
            uint32_t op = 0;
            if (!SafeRead(g_near.records + (uint64_t)i * 0x19B0u, &op, 4) && op == 0x4C) { g_move.at4C = i; break; }
        }
        for (uint32_t i = g_near.at65 + 1; i < g_near.end; i++)
        {
            uint32_t op = 0;
            if (!SafeRead(g_near.records + (uint64_t)i * 0x19B0u, &op, 4) && op == 0x66) { g_move.at66 = i; break; }
        }
        if (g_move.at2F == 0 || g_move.at37 == 0 || g_move.at38 == 0 || g_move.at4C == 0 || g_move.at66 == 0)
        { Note("difficulty: moving tiles: off for this level (records 0x2F %u, 0x37 %u, 0x38 %u, 0x4C %u, 0x66 %u)", g_move.at2F, g_move.at37, g_move.at38, g_move.at4C, g_move.at66); return; }
        g_move.origin[0] = g_tiles[0].centre[0]; g_move.origin[1] = g_tiles[0].centre[1];
        g_move.on = 1;
        Note("difficulty: moving tiles: on: cells %d..%d east, %d..%d north of the drop tile's at (%.0f, %.0f), %.1f m each; wanted within %.0f m, kept within %.0f m "
            "for %u s, %d at most; records 0x2F %u, 0x37 %u, 0x38 %u, 0x65 %u, 0x66 %u", g_ring.a0, g_ring.a1, g_ring.b0, g_ring.b1, (double)g_move.origin[0],
            (double)g_move.origin[1], (double)NEAR_HIDE, (double)MOVE_WANT, (double)MOVE_KEEP, MOVE_KEEP_MS / 1000, MOVE_CAP, g_move.at2F, g_move.at37, g_move.at38,
            g_near.at65, g_move.at66);
    }
    if (!g_move.on) return;
    int64_t fc = co_framecost_now();
    MoveGroundKeep(); /* before the frame's ground rays are cast */
    co_framecost_nested(CO_FC_GROUNDKEEP, CO_FC_MOVE, fc);
    MoveFreeTick(0);
    NearBandTick();
    MovePlayers();
    MoveLayerWait();
    if (MoveReleaseTick(now)) return; /* a release, or a frame held for one: a build's step waits for the next */
    if (MoveTrimTick()) return;
    if (g_move.state == MV_IDLE)
    {
        if (g_move.players == 0 || FarGrassBusy()) return; /* a far-grass batch has the generator's queue and names */
        if (NearTilesLive() >= MOVE_CAP) return;
        float best = 1e9f; int ba = 0, bb = 0, bp = -1;
        for (int p = 0; p < g_move.players; p++)
        {
            int pa = (int)floorf((g_move.player[p][0] - g_move.origin[0]) / NEAR_HIDE + 0.5f), pb = (int)floorf((g_move.player[p][1] - g_move.origin[1]) / NEAR_HIDE + 0.5f);
            for (int a = pa - 1; a <= pa + 1; a++)
                for (int b = pb - 1; b <= pb + 1; b++)
                {
                    if (!MoveCellValid(a, b) || MoveCellLive(a, b) || MoveCellSkipped(a, b, now)) continue;
                    float d = MoveCellDistance(a, b, g_move.player[p][0], g_move.player[p][1]);
                    if (d <= MOVE_WANT && d < best) { best = d; ba = a; bb = b; bp = p; }
                }
        }
        if (bp < 0) return;
        g_move.slot = NearTileFree();
        if (g_move.slot < 0) return;
        g_move.cell[0] = ba; g_move.cell[1] = bb;
        MoveCellCentre(ba, bb, g_move.centre);
        if (bp != 0) { float t0 = g_move.player[0][0], t1 = g_move.player[0][1]; g_move.player[0][0] = g_move.player[bp][0]; g_move.player[0][1] = g_move.player[bp][1]; g_move.player[bp][0] = t0; g_move.player[bp][1] = t1; }
        if (!MoveStart()) { Note("difficulty: moving tiles: the level's context did not read; no tile"); MoveCellSkip(ba, bb, now); return; }
    }
    g_move.frames++;
    if (g_move.state == MV_PACKAGE)
    {
        typedef int (*HasFn)(uint64_t hash);
        typedef void (*PkgFn)(uint64_t hash);
        HasFn has = (HasFn)NearApi(0x10, 0x310);
        PkgFn acquire = (PkgFn)NearApi(0x10, 0x2F0);
        if (has == NULL || acquire == NULL) { MoveFail("the package interface is not there"); return; }
        /* The package calls queue requests (exe 0x603670 / 0x603A80, ring mgr+0x218, 512 x {hash, context, load flag}); no
         * count. An acquire turns this context's queued unload back into a load, is a no-op when the package is listed
         * and nothing is queued (0x60E800), else queues a load. The unload is done when the consumer (0x603290) reaches
         * it (0x60E410), so has() (0x321330) still answers 1 while a release waits in the queue: run s85a/s85b built on
         * that, the package went, and 0x38's gui material lookup (exe 0x264620 via [api+0xD0]+0x238) found no material
         * (set_scalar on 0, exe 0x4F33DA). So the build asks whenever it does not hold the package, whatever has() says. */
        if (g_move.pkgPending && !g_move.acquired) { g_move.pkgPending = 0; g_move.acquired = 1; } /* not released yet: taken back */
        if (!g_move.acquired) { acquire(MOVE_PACKAGE); g_move.acquired = 1; Note("difficulty: moving tiles: the merge package asked for"); }
        if (has(MOVE_PACKAGE) == 0)
        {
            if (++g_move.waits > 3600) MoveFail("the merge package did not load");
            return;
        }
        if (!g_move.sel.on) { g_move.waits = 0; g_move.stamped = 0; g_move.stCount = g_move.stUnits = 0; }
        uint64_t locs = 0, countAt = 0; uint32_t count = 0;
        uint64_t rec = g_near.records + (uint64_t)g_move.at2F * 0x19B0u;
        if (SafeRead(rec + 8 + 8, &locs, 8) || SafeRead(rec + 8 + 0x28, &countAt, 8) || locs == 0 || countAt == 0 || SafeRead(countAt, &count, 4))
        { MoveFail("the 0x2F record's locations did not read"); return; }
        static int stamps = -1; /* HELLFORGE_XL_MOVE_STAMPS=0: no stamp terrains for the tile (0x63 draws none) */
        if (stamps < 0) { char t[4] = ""; stamps = !(GetEnvironmentVariableA("HELLFORGE_XL_MOVE_STAMPS", t, sizeof t) > 0 && t[0] == '0'); }
        if (stamps)
        {
            const int r = MoveStampsSelect(locs, count, g_near.world_size > 0 ? g_near.world_size / 2 : 8192.0f, MOVE_SELECT_MS);
            if (r < 0) { MoveFail("the level's stamp data went while the stamp units were made"); return; }
            if (r == 0) return; /* the rest of the spawns next frame */
        }
        if (g_move.stCount != 0 && !MoveStampsRecord(g_move.at2F, 0x2F)) { MoveFail("record 0x2F did not read"); return; }
        Move2FReset();
        g_move.state = g_move.stCount != 0 ? MV_STAMPS : MV_KICK;
        if (g_move.state == MV_KICK && SafeRead(g_near.records + (uint64_t)g_move.at37 * 0x19B0u, g_move.one, sizeof g_move.one)) { MoveFail("record 0x37 did not read"); return; }
        g_move.phase = g_move.state == MV_KICK ? 0 : g_move.phase;
        return;
    }
    if (g_move.state == MV_STAMPS)
    {
        g_move.stamped = 1; /* from the first call some of the terrains may be spawned */
        int r = MoveRun2F();
        if (r < 0) { MoveFail("0x2F faulted"); return; }
        if (r == 0) { if (++g_move.waits > 1200) MoveFail("0x2F did not finish (the spawns did not come)"); return; }
        Note("difficulty: moving tiles: the stamp terrains of %u locations spawned for the tile (%.1f s)", g_move.stCount, (double)(GetTickCount64() - g_move.started) / 1000.0);
        if (SafeRead(g_near.records + (uint64_t)g_move.at37 * 0x19B0u, g_move.one, sizeof g_move.one)) { MoveFail("record 0x37 did not read"); return; }
        g_move.phase = 0; g_move.waits = 0; g_move.state = MV_KICK;
        return;
    }
    if (g_move.state == MV_KICK)
    {
        MoveMergeBegin();
        int r = MoveRunRecord();
        if (r < 0) { MoveFail("0x37 faulted"); return; }
        if (r == 0) { if (++g_move.waits > 60) MoveFail("0x37 did not finish"); return; }
        if (SafeRead(g_near.records + (uint64_t)g_move.at38 * 0x19B0u, g_move.one, sizeof g_move.one)) { MoveFail("record 0x38 did not read"); return; }
        /* the merge in one tile, as the level's own 0x38 ran it (WorldDispatch sets the record's T, pass+0xA8, to 1 for
         * the step: the records of an extra large level give 2); this copy goes to the dispatcher directly */
        { uint32_t t = 0; memcpy(&t, g_move.one + 8 + 0xA8, 4); if (t > 1 && t <= 16) { t = 1; memcpy(g_move.one + 8 + 0xA8, &t, 4); } }
        g_move.phase = 0; g_move.waits = 0; g_move.state = MV_SETUP;
        return;
    }
    if (g_move.state == MV_SETUP)
    {
        int r = MoveRunRecord();
        if (r < 0) { MoveFail("0x38 faulted"); return; }
        if (r == 0) { if (++g_move.waits > 1200) MoveFail("0x38 did not finish (the ring did not drain)"); return; }
        /* 0x4C (case 0xC16B85): phase 0 creates R+0xB8 (4096 x 1, format 0x2A, [api+0x10]+0x170) and publishes it as
         * 0x2FFE957D, phase 1 fills it (0xF4F4F0). 0x67 released the level's; 0x37 re-creates only the other three
         * (0xA2003184, 0x1BB41B90, 0xEBD2B190, 0xFCEB80), and the replay's 0x64 binds it (run s72h: without it 0x64's job
         * crashed a worker at exe+0x396CCD, an id with no resource) */
        if (SafeRead(g_near.records + (uint64_t)g_move.at4C * 0x19B0u, g_move.one, sizeof g_move.one)) { MoveFail("record 0x4C did not read"); return; }
        g_move.phase = 0; g_move.waits = 0; g_move.state = MV_LOOKUP;
        return;
    }
    if (g_move.state == MV_LOOKUP)
    {
        int r = MoveRunRecord();
        if (r < 0) { MoveFail("0x4C faulted"); return; }
        if (r == 0) { if (++g_move.waits > 1200) MoveFail("0x4C did not finish"); return; }
        if (!MoveReplayBegin()) { MoveFail("the replay could not start"); return; }
        Note("difficulty: moving tiles: the merge is re-created (%.1f s); replaying %d steps for the tile", (double)(GetTickCount64() - g_move.started) / 1000.0, g_near.steps);
        g_move.state = MV_REPLAY;
        return;
    }
    if (g_move.state == MV_REPLAY)
    {
        TerrainNearReplay(g_near.records, g_near.end, g_near.at65, (uint64_t)(uintptr_t)g_move.ctx, 0, 0);
        if (g_near.state == NEAR_FAILED) { MoveFail("the replay failed"); return; }
        if (g_near.state != NEAR_SPAWN) return;
        MoveHold(0); /* NearReplayEnd unpublished the tile's textures */
        g_move.waits = g_move.quiet = 0; g_move.state = MV_TEARDOWN;
        return;
    }
    if (g_move.state == MV_TEARDOWN)
    {
        const int q = NearQueueWait((uint64_t)(uintptr_t)g_move.ctx, &g_move.waits, &g_move.quiet, 600, 3, "a build's teardown");
        if (q < 0) MoveFail("the job queue did not empty");
        if (q != 0) return;
        if (g_move.stamped)
        {
            if (!MoveStampsRecord(g_near.at65, 0x65)) { MoveFail("record 0x65 did not read"); return; }
            g_move.waits = 0; g_move.state = MV_UNSTAMP;
            return;
        }
        MoveTeardown(0);
        memcpy(g_near.one69, g_near.keep69, sizeof g_near.one69); memcpy(g_near.one6B, g_near.keep6B, sizeof g_near.one6B);
        g_move.state = MV_SPAWN;
        return;
    }
    if (g_move.state == MV_FAILING)
    {
        if (!Split63Restore()) return; /* hidden stamps shown before 0x65 takes them */
        if (g_move.stamped && DestroyListCount(MoveWorld()) >= MOVE_DESTROY_ROOM) { if (++g_move.waits > 600) { g_move.stamped = 0; g_move.ringWaits = 0; Note("difficulty: moving tiles: the destroy list did not empty; the stamp terrains were not all taken away"); } return; }
        if (g_move.stamped)
        {
            int r = MoveRunRecord();
            if (r == 0 && ++g_move.waits <= 600) return;
            if (r != 1) Note("difficulty: moving tiles: the stamp terrains were not all taken away (0x65: %d)", r);
            g_move.stamped = 0; g_move.ringWaits = 0;
        }
        if (g_move.stGone == 0 && !MoveStampUnitsReady()) return;
        if (!MoveStampUnitsDestroy(MOVE_UNSTAMP_MS)) return;
        MoveFailRest();
        return;
    }
    if (g_move.state == MV_UNSTAMP)
    {
        if (g_move.stamped)
        {
            if (DestroyListCount(MoveWorld()) >= MOVE_DESTROY_ROOM) { if (++g_move.waits > 600) { g_move.stamped = 0; MoveFail("the destroy list did not empty"); } return; }
            int r = MoveRunRecord();
            if (r < 0) { g_move.stamped = 0; MoveFail("0x65 faulted"); return; }
            if (r == 0) { if (++g_move.waits > 600) { g_move.stamped = 0; MoveFail("0x65 did not finish"); } return; }
            g_move.stamped = 0; g_move.ringWaits = 0;
        }
        if (g_move.stGone == 0 && !MoveStampUnitsReady()) return;
        if (!MoveStampUnitsDestroy(MOVE_UNSTAMP_MS)) return;
        MoveTeardown(0);
        memcpy(g_near.one69, g_near.keep69, sizeof g_near.one69); memcpy(g_near.one6B, g_near.keep6B, sizeof g_near.one6B);
        g_move.state = MV_SPAWN;
        return;
    }
    if (g_move.state == MV_SPAWN)
    {
        if (g_matband.tex != 0 && (g_matband.holds < NEAR_MAT_HOLD || GetTickCount64() - g_matband.made < 100)) { g_matband.holds++; return; }
        if (!NearSpawnTile((uint64_t)(uintptr_t)g_move.ctx)) { MoveFail("the spawn failed"); return; }
        g_move.state = MV_READY; g_move.waitSince = 0;
        return;
    }
    if (g_move.state == MV_READY)
    {
        /* the last change's grids first: the tile's slot buffers and grids are its */
        const char* wait = MoveReadyWait(GetTickCount64());
        if (wait != NULL) { if (wait[0] != 0) MoveFail(wait); return; }
        uint64_t world = 0;
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        if (NearReadyTile((uint64_t)(uintptr_t)g_move.ctx, &world)) return;
        g_moveReadyMs[0] = MoveMsSince(&t);
        if (g_near.state == NEAR_FAILED) { MoveFail("the tile was not ready"); return; }
        MoveTileIn(world);
        g_move.state = MV_IDLE;
    }
}

/* What a build costs the game thread: MoveTick's time in each frame of a build, the six longest with the build's state,
 * the replay's step and the conform's phase at the frame's start; noted once the build is over (the frame after the tile
 * is in, or after a failure). */
/* Every frame of a build over MOVE_SLOW_MS, the mover's own work and the wall clock apart, counted by the state at the
 * frame's start (the step's op too): the build's slow frames all named, not only its longest. */
#define MOVE_SLOW_MS 16.7
#define MOVE_SLOW_KEYS 12
typedef struct { int mv, ns, op, conf; uint32_t n; double max; } MoveSlowKey;
typedef struct { MoveSlowKey key[MOVE_SLOW_KEYS]; uint32_t keys, over, other; } MoveSlow;
static struct { double ms[6]; int mv[6], ns[6], step[6], conf[6], bld[6]; double sum; uint32_t frames; int pending;
    double wall[3]; int wallMv[3]; uint32_t slow; MoveSlow tick, clock;
    uint32_t fenceFrames, fenceSlow; double fenceSum, fenceMax, fenceSlowWall, fenceSlowSum; int fenceMv, fenceOp; } g_moveCost;
/* Wall-clock frames, level update to level update (once a frame on the game thread, 0xAB519E): the whole frame's time,
 * render and other threads' waits included, which the game-thread cost above leaves out. */
static struct { LARGE_INTEGER last; int mv, ns, op, conf, sp0, sp1; double mover; uint32_t toggles; } g_moveWall; /* sp0, sp1: the 0x63 split at t0, after MoveTick; MoveTick's ms, its shows and hides */

/* A build frame over MOVE_FRAME_SLOW_MS split where the mod runs on the game thread, CoXlTick to CoXlTick: the early
 * update (CoXlTick and the game's update up to the level update, 0xAB519E), the level update, MoveTick, then up to the
 * job consumer's last return and on to the next tick; a wall frame alone cannot say which of them held it. */
#define MOVE_FRAME_SLOW_MS 40.0
#define MOVE_FRAME_LINES 8 /* a build */
enum { MF_TICK, MF_LEVEL, MF_T0, MF_T1, MF_TICK_END, MF_MARKS }; /* MF_TICK_END: CoXlTick's own end */
typedef struct
{
    int64_t at[MF_MARKS], consumer, consumerOut; /* QPC ticks; the consumer's after MoveTick only */
    int64_t fence; /* the frame loop's fence wait (exe 0x8A7C4) between the tick and the level update */
    int have, mv, ns, op, shape, tile, building;
    LONG shapeEnds, tileEnds; /* the job ends counted at the tick: one that ends inside the frame was out in it */
} MoveFrameMarks;
static struct { MoveFrameMarks f; uint32_t lines; int inBuild, fenceTimed; volatile LONG fenceIn, fenceOut, fenceOther; DWORD thread; uintptr_t teb; } g_moveFrame;

/* frame, early update, level update, mover, after the mover, consumer, to the next tick, CoXlTick, frame fence (ms); 0
 * without the four marks. The part after the mover ends at the consumer's last return, so the five parts add up to the
 * frame; CoXlTick (0 without its end mark) and the frame fence are parts of the early update. */
static int MoveFrameParts(const MoveFrameMarks* f, int64_t next, double hz, double part[9])
{
    if ((f->have & 15) != 15 || !(hz > 0)) return 0;
    const int64_t* a = f->at;
    const int64_t x = f->consumerOut > a[MF_T1] && f->consumerOut <= next ? f->consumerOut : a[MF_T1];
    const double k = 1000.0 / hz;
    part[0] = (double)(next - a[MF_TICK]) * k; part[1] = (double)(a[MF_LEVEL] - a[MF_TICK]) * k;
    part[2] = (double)(a[MF_T0] - a[MF_LEVEL]) * k; part[3] = (double)(a[MF_T1] - a[MF_T0]) * k;
    part[4] = (double)(x - a[MF_T1]) * k; part[5] = (double)f->consumer * k; part[6] = (double)(next - x) * k;
    part[7] = (f->have & (1 << MF_TICK_END)) && a[MF_TICK_END] >= a[MF_TICK] && a[MF_TICK_END] <= a[MF_LEVEL] ? (double)(a[MF_TICK_END] - a[MF_TICK]) * k : 0.0;
    part[8] = (double)f->fence * k;
    return 1;
}

static void MoveFrameClose(int64_t next, double hz)
{
    const MoveFrameMarks* f = &g_moveFrame.f;
    double p[9];
    if (!f->building || g_moveFrame.lines >= MOVE_FRAME_LINES || !MoveFrameParts(f, next, hz, p) || !(p[0] > MOVE_FRAME_SLOW_MS)) return;
    g_moveFrame.lines++;
    const int shape = f->shape || g_shapeEnds != f->shapeEnds, tile = f->tile || g_nearJobEnds != f->tileEnds;
    char fence[16] = "untimed";
    if (g_moveFrame.fenceTimed) snprintf(fence, sizeof fence, "%.1f", p[8]);
    Note("difficulty: moving tiles: slow frame %.1f ms (%s/near %d/op 0x%02X): early update %.1f ms (CoXlTick %.1f, frame fence %s), level update %.1f, mover %.1f, "
        "after the mover %.1f (consumer %.1f), to the next tick %.1f; jobs out: shape %d, tile %d", p[0], MoveStateName(f->mv), f->ns, (unsigned)f->op & 0xFFu,
        p[1], p[7], fence, p[2], p[3], p[4], p[5], p[6], shape, tile);
}

/* A mark on the game thread at `now`; a tick closes the frame before it and takes the job end counts the new one starts with. */
static void MoveFrameMark(int mark, int64_t now, double hz)
{
    if (mark == MF_TICK)
    {
        MoveFrameClose(now, hz);
        memset(&g_moveFrame.f, 0, sizeof g_moveFrame.f);
        g_moveFrame.thread = GetCurrentThreadId();
        g_moveFrame.f.shapeEnds = g_shapeEnds; g_moveFrame.f.tileEnds = g_nearJobEnds; /* running then: it ends in the frame or t0 sees it */
    }
    g_moveFrame.f.at[mark] = now;
    g_moveFrame.f.have |= 1 << mark;
}

/* The level's end: no build runs any more (the stall watch's gate, move.c's teardown). */
static void MoveFrameLeft(void) { g_moveFrame.inBuild = 0; g_moveFrame.f.building = 0; }

/* The frame loop's wait on the fence it posted a frame before (exe 0x8A7C4 waits [rsi+0x340], 0x8AAF2 posts it after
 * the kick), timed by xl_free.c's 0x106030 hook: the game thread idle while the render thread ends the last frame. It
 * runs after the world update and before the next level update, so only the game thread's waits in the early update
 * count; the build's line counts the waits a build sees: in early updates, elsewhere on the game thread, other threads. */
static void MoveFrameFence(int64_t in, int64_t out)
{
    MoveFrameMarks* f = &g_moveFrame.f;
    const int game = GetCurrentThreadId() == g_moveFrame.thread;
    if (!game || !(f->have & (1 << MF_TICK)) || (f->have & (1 << MF_LEVEL)))
    {
        if (g_moveFrame.inBuild) InterlockedIncrement(game ? &g_moveFrame.fenceOut : &g_moveFrame.fenceOther);
        return;
    }
    f->fence += out - in;
    if (g_moveFrame.inBuild) InterlockedIncrement(&g_moveFrame.fenceIn);
}
/* The fence in the wall frame ending at this level update (ms), -1 untimed. */
static double MoveFrameFenceMs(double hz) { return g_moveFrame.fenceTimed && hz > 0 ? (double)g_moveFrame.f.fence * 1000.0 / hz : -1.0; }

/* The job consumer (NearConsume), on whichever thread runs it: only the game thread's calls after MoveTick count. */
static void MoveFrameConsumer(int64_t in, int64_t out)
{
    MoveFrameMarks* f = &g_moveFrame.f;
    if (GetCurrentThreadId() != g_moveFrame.thread || !(f->have & (1 << MF_T1)) || in < f->at[MF_T1]) return;
    f->consumer += out - in;
    f->consumerOut = out;
}

/* Where the game thread is for the stall watch (S2): the two stretches of a frame the mod's marks do not explain. */
enum { MS_NONE, MS_EARLY, MS_LATE };
#if defined(CO_RESEARCH) || defined(CO_TEST)
/* A watcher samples the game thread once in such a stretch past MOVE_STALL_MS: its RIP, the call sites on its stack and
 * the engine queues' lengths name the wait the frame parts only bound. Research builds only: it suspends the thread. */
#define MOVE_STALL_MS 40.0
#define MOVE_STALL_SAMPLES 4 /* a build */
static struct { volatile LONG phase, seq, samples, sampled; volatile LONG64 since; } g_moveStall;
static void MoveStallPhase(int phase, int64_t now)
{
    InterlockedExchange64(&g_moveStall.since, now); /* before the phase: a new phase is never read with an old start */
    InterlockedExchange(&g_moveStall.phase, phase);
    InterlockedIncrement(&g_moveStall.seq);
}
/* 1: sample this stretch now (one sample a stretch, MOVE_STALL_SAMPLES a build, only while a build runs). */
static int MoveStallDue(int phase, int64_t since, int64_t now, double hz, LONG seq, LONG sampled, LONG samples, int building)
{
    return building && (phase == MS_EARLY || phase == MS_LATE) && seq != sampled && samples < MOVE_STALL_SAMPLES && hz > 0 &&
        (double)(now - since) * 1000.0 / hz > MOVE_STALL_MS;
}
#else
static void MoveStallPhase(int phase, int64_t now) { (void)phase; (void)now; }
#endif

/* The 0x63 step's wall frames by where its split was (state at the frame's t0 and after its MoveTick): before the op
 * (hides), the op's frame, the ranges, showing all, the last frame; its slowest frame is outside the split's own clock. */
enum { S63W_BEFORE, S63W_OP, S63W_RANGES, S63W_SHOWING, S63W_LAST, S63W_OTHER, S63W_KINDS };
/* Each kind's longest frame's parts (ms, -1 untimed): MoveTick, the engine's world update (0x1B0B10) and the frame fence
 * inside it, the shows and hides it posted, and the next frame's fence: a toggle is a render message (0x2BD140) the
 * render thread applies with the world's render (message 8, 0x108D40 -> 0x1E9540) after this frame's kick, so the game
 * thread waits for it at the next frame's fence. */
static struct
{
    uint32_t n[S63W_KINDS], over, toggles[S63W_KINDS];
    double max[S63W_KINDS], fence[S63W_KINDS], world[S63W_KINDS], mover[S63W_KINDS], next[S63W_KINDS];
    int after; /* the kind whose longest frame was the last one added, -1 none: the next frame's fence is its */
} g_split63Wall = {.after = -1};
static int Split63WallKind(int sp0, int sp1)
{
    if (sp1 == SP_DONE) return S63W_LAST;
    if ((sp1 == SP_QUEUED || sp1 == SP_WHOLE) && (sp0 == SP_NONE || sp0 == SP_HIDING || sp0 == SP_READY)) return S63W_OP;
    if (sp1 == SP_NONE || sp1 == SP_HIDING || sp1 == SP_READY) return S63W_BEFORE;
    if (sp1 == SP_QUEUED || sp1 == SP_RANGES) return S63W_RANGES;
    if (sp1 == SP_SHOWING) return S63W_SHOWING;
    return S63W_OTHER;
}
/* A wall frame of the step: its ms and parts (ms, -1 untimed), the shows and hides its MoveTick posted. */
static void Split63WallAdd(double ms, double fence, double world, double mover, uint32_t toggles, int sp0, int sp1)
{
    const int k = Split63WallKind(sp0, sp1);
    if (g_split63Wall.after >= 0) g_split63Wall.next[g_split63Wall.after] = fence;
    g_split63Wall.after = -1;
    g_split63Wall.n[k]++;
    if (g_split63Wall.n[k] == 1 || ms > g_split63Wall.max[k])
    {
        g_split63Wall.max[k] = ms; g_split63Wall.fence[k] = fence; g_split63Wall.world[k] = world; g_split63Wall.mover[k] = mover;
        g_split63Wall.toggles[k] = toggles; g_split63Wall.next[k] = -1.0; g_split63Wall.after = k;
    }
    if (ms > MOVE_SLOW_MS) g_split63Wall.over++;
}
static void Split63WallNote(void)
{
    uint32_t frames = 0;
    for (int k = 0; k < S63W_KINDS; k++) frames += g_split63Wall.n[k];
    if (frames == 0) return;
    const uint32_t* n = g_split63Wall.n; const double* m = g_split63Wall.max;
    Note("difficulty: moving tiles: 0x63 step's wall-clock frames: %u before the op (max %.1f ms), the op's %u (max %.1f), %u of the ranges (max %.1f), "
        "%u showing all (max %.1f), the last %u (max %.1f), %u other (max %.1f); %u over %.1f ms", n[S63W_BEFORE], m[S63W_BEFORE], n[S63W_OP], m[S63W_OP],
        n[S63W_RANGES], m[S63W_RANGES], n[S63W_SHOWING], m[S63W_SHOWING], n[S63W_LAST], m[S63W_LAST], n[S63W_OTHER], m[S63W_OTHER], g_split63Wall.over, MOVE_SLOW_MS);
    /* where each kind's longest frame went: the game thread's own parts, the render thread's last frame, the rest */
    static const char* const kName[S63W_KINDS] = {"before the op", "the op's", "the ranges'", "showing all", "the last", "other"};
    char parts[1400]; size_t at = 0; parts[0] = 0;
    for (int k = 0; k < S63W_KINDS && at + 1 < sizeof parts; k++)
    {
        if (n[k] == 0) continue;
        double rest = m[k];
        if (g_split63Wall.mover[k] > 0) rest -= g_split63Wall.mover[k];
        if (g_split63Wall.world[k] > 0) rest -= g_split63Wall.world[k];
        if (g_split63Wall.fence[k] > 0) rest -= g_split63Wall.fence[k];
        const int w = snprintf(parts + at, sizeof parts - at, "%s %s %.1f = mover %.1f + world update %.1f + frame fence %.1f + the rest %.1f, %u shows or hides posted, "
            "the next frame's fence %.1f", at ? ";" : "", kName[k], m[k], g_split63Wall.mover[k], g_split63Wall.world[k], g_split63Wall.fence[k], rest,
            g_split63Wall.toggles[k], g_split63Wall.next[k]);
        if (w < 0) break;
        at += (size_t)w < sizeof parts - at ? (size_t)w : sizeof parts - at - 1;
    }
    Note("difficulty: moving tiles: 0x63 step's longest wall-clock frames' parts (ms, -1 untimed; the world update 0x1B0B10, the frame fence 0x8A7C4):%s", parts);
    memset(&g_split63Wall, 0, sizeof g_split63Wall);
    g_split63Wall.after = -1;
}

static void MoveSlowAdd(MoveSlow* t, double ms, int mv, int ns, int op, int conf)
{
    if (!(ms > MOVE_SLOW_MS)) return;
    t->over++;
    uint32_t k = 0;
    while (k < t->keys && !(t->key[k].mv == mv && t->key[k].ns == ns && t->key[k].op == op && t->key[k].conf == conf)) k++;
    if (k == t->keys)
    {
        if (t->keys >= MOVE_SLOW_KEYS) { t->other++; return; }
        t->key[k] = (MoveSlowKey){mv, ns, op, conf, 0, 0.0};
        t->keys++;
    }
    t->key[k].n++;
    if (ms > t->key[k].max) t->key[k].max = ms;
}

/* "<mover state>/near <state>/op 0xNN/conform <phase>: <frames>, longest <ms>" for each key, in first-seen order. */
static void MoveSlowText(const MoveSlow* t, char* out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (uint32_t k = 0; k < t->keys && n + 1 < size; k++)
    {
        const MoveSlowKey* e = &t->key[k];
        char op[8] = "-";
        if (e->op >= 0) snprintf(op, sizeof op, "0x%02X", e->op);
        int w = snprintf(out + n, size - n, " %s/near %d/op %s/conform %d: %u, longest %.0f ms;", MoveStateName(e->mv), e->ns, op, e->conf, e->n, e->max);
        if (w < 0) break;
        n += (size_t)w;
    }
    if (t->other && n + 1 < size) snprintf(out + n, size - n, " %u more of other states;", t->other);
}
static void MoveWallAdd(double ms, int mv)
{
    if (ms > 50.0) g_moveCost.slow++;
    int at = 3;
    while (at > 0 && ms > g_moveCost.wall[at - 1]) at--;
    if (at >= 3) return;
    for (int k = 2; k > at; k--) { g_moveCost.wall[k] = g_moveCost.wall[k - 1]; g_moveCost.wallMv[k] = g_moveCost.wallMv[k - 1]; }
    g_moveCost.wall[at] = ms; g_moveCost.wallMv[at] = mv;
}

/* A wall frame's fence wait (ms, -1 untimed) for the build's fence line, keyed by the state at the frame's start. */
static void MoveFenceAdd(double wall, double fence, int mv, int op)
{
    if (fence < 0) return;
    g_moveCost.fenceFrames++; g_moveCost.fenceSum += fence;
    if (g_moveCost.fenceFrames == 1 || fence > g_moveCost.fenceMax) { g_moveCost.fenceMax = fence; g_moveCost.fenceMv = mv; g_moveCost.fenceOp = op; }
    if (wall > MOVE_SLOW_MS) { g_moveCost.fenceSlow++; g_moveCost.fenceSlowWall += wall; g_moveCost.fenceSlowSum += fence; }
}

static void MoveCostAdd(double ms, int mv, int ns, int step, int conf, int bld)
{
    g_moveCost.sum += ms; g_moveCost.frames++;
    int at = 6;
    while (at > 0 && ms > g_moveCost.ms[at - 1]) at--;
    if (at >= 6) return;
    for (int k = 5; k > at; k--)
    { g_moveCost.ms[k] = g_moveCost.ms[k - 1]; g_moveCost.mv[k] = g_moveCost.mv[k - 1]; g_moveCost.ns[k] = g_moveCost.ns[k - 1]; g_moveCost.step[k] = g_moveCost.step[k - 1]; g_moveCost.conf[k] = g_moveCost.conf[k - 1]; g_moveCost.bld[k] = g_moveCost.bld[k - 1]; }
    g_moveCost.ms[at] = ms; g_moveCost.mv[at] = mv; g_moveCost.ns[at] = ns; g_moveCost.step[at] = step; g_moveCost.conf[at] = conf; g_moveCost.bld[at] = bld;
}

static void MoveCostNote(void)
{
    char line[640]; int n = 0;
    for (int k = 0; k < 6 && g_moveCost.ms[k] > 0 && n < 600; k++)
        n += snprintf(line + n, sizeof line - n, " %.0f ms (%s, near state %d, step %d, building %d, conform phase %d);", g_moveCost.ms[k],
            MoveStateName(g_moveCost.mv[k]), g_moveCost.ns[k], g_moveCost.step[k], g_moveCost.bld[k], g_moveCost.conf[k]);
    Note("difficulty: moving tiles: the build's cost on the game thread: %.0f ms over %u frames; the longest:%s", g_moveCost.sum, g_moveCost.frames, line);
    Note("difficulty: moving tiles: the ready frame: ready and finalize %.1f ms (the quadtree %.1f, the finalize %.1f), coarse hidden %.1f, heights copied %.1f, "
        "band %.1f, hide union %.1f, layer change started %.1f, stamps flagged %.1f", g_moveReadyMs[0], g_shape.quadMs, g_nearFinalMs, g_moveReadyMs[1],
        g_moveReadyMs[2], g_moveReadyMs[3], g_moveReadyMs[4], g_moveReadyMs[5], g_moveReadyMs[6]);
    memset(g_moveReadyMs, 0, sizeof g_moveReadyMs);
    Note("difficulty: moving tiles: the build's wall-clock frames: the longest %.0f ms (%s), %.0f ms (%s), %.0f ms (%s); %u over 50 ms",
        g_moveCost.wall[0], MoveStateName(g_moveCost.wallMv[0]), g_moveCost.wall[1], MoveStateName(g_moveCost.wallMv[1]), g_moveCost.wall[2],
        MoveStateName(g_moveCost.wallMv[2]), g_moveCost.slow);
    {
        char tick[1200], clock[1200];
        MoveSlowText(&g_moveCost.tick, tick, sizeof tick); MoveSlowText(&g_moveCost.clock, clock, sizeof clock);
        Note("difficulty: moving tiles: the build's frames over %.1f ms: the mover's own work %u:%s", MOVE_SLOW_MS, g_moveCost.tick.over, tick);
        Note("difficulty: moving tiles: the build's frames over %.1f ms: wall clock %u:%s", MOVE_SLOW_MS, g_moveCost.clock.over, clock);
    }
    const long fenceIn = InterlockedExchange(&g_moveFrame.fenceIn, 0), fenceOut = InterlockedExchange(&g_moveFrame.fenceOut, 0),
        fenceOther = InterlockedExchange(&g_moveFrame.fenceOther, 0); /* this build's, printed or not */
    if (g_moveCost.fenceFrames)
    {
        char op[8] = "-";
        if (g_moveCost.fenceOp >= 0) snprintf(op, sizeof op, "0x%02X", g_moveCost.fenceOp);
        Note("difficulty: moving tiles: the build's frame fence waits (exe 0x8A7C4): %u wall frames, %.0f ms in all, the longest %.1f ms (%s/op %s); "
            "the %u wall frames over %.1f ms: %.0f of their %.0f ms in the fence; its waits in the build: %ld in early updates, %ld elsewhere on the game thread, "
            "%ld on other threads", g_moveCost.fenceFrames, g_moveCost.fenceSum, g_moveCost.fenceMax, MoveStateName(g_moveCost.fenceMv), op, g_moveCost.fenceSlow,
            MOVE_SLOW_MS, g_moveCost.fenceSlowSum, g_moveCost.fenceSlowWall, fenceIn, fenceOut, fenceOther);
    }
    memset(&g_moveCost, 0, sizeof g_moveCost);
    Split63WallNote(); /* a build that ended inside the step */
    MatResNote();
    MatWaitNote();
    g_moveFrame.lines = 0;
#if defined(CO_RESEARCH) || defined(CO_TEST)
    NearAutoBuildEnd(g_moveStall.samples);
    InterlockedExchange(&g_moveStall.samples, 0);
#else
    NearAutoBuildEnd(0);
#endif
}

#if defined(CO_RESEARCH) || defined(CO_TEST)
/* The jobs on engine queue q (exe 0x624D00's list): the head's low 23 bits are an index + 1 into the links at
 * [sched+0x148], each holding the next head; sched = [[[exe+0x1A0F8B8]+0x318]+0xF8] (exe 0xA4A40, 0x626973). */
static uint32_t MoveStallQueued(uintptr_t exe, int q)
{
    uint64_t p = 0, s = 0, sched = 0, base = 0, links = 0;
    uint32_t head = 0, n = 0;
    if (SafeRead(exe + 0x1A0F8B8u, &p, 8) || p == 0 || SafeRead(p + 0x318, &s, 8) || s == 0 || SafeRead(s + 0xF8, &sched, 8) || sched == 0 ||
        SafeRead(sched + 0x140, &base, 8) || SafeRead(sched + 0x148, &links, 8) || base == 0 || base + (uint64_t)q * 0x40 >= links ||
        SafeRead(base + (uint64_t)q * 0x40, &head, 4)) return 0xFFFFFFFFu;
    for (uint32_t h = head & 0x7FFFFFu; h != 0 && n < 1024; n++)
    {
        uint32_t next = 0;
        if (SafeRead(links + (uint64_t)(h - 1) * 4, &next, 4)) break;
        h = next & 0x7FFFFFu;
    }
    return n;
}

/* A qword on the stack copy that may be a return address: inside a module and right after a call (E8 rel32, FF /2). */
static int MoveStallCallBefore(uintptr_t a)
{
    uint8_t b[8];
    if (SafeRead(a - 8, b, 8)) return 0;
    const uint8_t* e = b + 8; /* e[-k]: k bytes before the address */
    return e[-5] == 0xE8 || (e[-2] == 0xFF && (e[-1] & 0xF8) == 0xD0) || (e[-6] == 0xFF && (e[-5] == 0x15 || ((e[-5] & 0xF8) == 0x90 && e[-5] != 0x94))) ||
        (e[-3] == 0xFF && (e[-2] & 0xF8) == 0x50 && e[-2] != 0x54) || (e[-4] == 0xFF && e[-3] == 0x54) || (e[-7] == 0xFF && e[-6] == 0x94) ||
        (e[-3] == 0x41 && e[-2] == 0xFF && (e[-1] & 0xF8) == 0xD0) || (e[-4] == 0x41 && e[-3] == 0xFF && (e[-2] & 0xF8) == 0x50);
}

static void MoveStallWhere(uintptr_t a, uintptr_t exe, size_t exeSize, uintptr_t game, size_t gameSize, char* out, size_t size, uint64_t* offset)
{
    HMODULE m = NULL; char path[MAX_PATH] = "";
    if (a >= exe && a < exe + exeSize) { snprintf(out, size, "exe"); *offset = a - exe; return; }
    if (a >= game && a < game + gameSize) { snprintf(out, size, "game"); *offset = a - game; return; }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && GetModuleFileNameA(m, path, sizeof path) > 0)
    {
        const char* name = strrchr(path, '\\');
        snprintf(out, size, "%s", name != NULL ? name + 1 : path);
        *offset = a - (uintptr_t)m;
        return;
    }
    snprintf(out, size, "0"); *offset = a;
}

/* The function an exe address lies in: its pdata entry, walked back over chained parts to the primary start; 0 none. */
static uintptr_t MoveStallFunction(uintptr_t a)
{
    DWORD64 base = 0;
    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry((DWORD64)a, &base, NULL);
    for (int hops = 0; f != NULL && hops < 8; hops++)
    {
        const uint8_t* ui = (const uint8_t*)(uintptr_t)(base + f->UnwindData);
        if (!((ui[0] >> 3) & UNW_FLAG_CHAININFO)) return (uintptr_t)(base + f->BeginAddress);
        f = (PRUNTIME_FUNCTION)(ui + 4 + 2 * (((size_t)ui[2] + 1) & ~(size_t)1)); /* the chained entry after the codes */
    }
    return 0;
}

/* The wait an address names (RIP, or a return address on the stack): the job system's waits and helps (0x626900,
 * 0x624D00, 0x627160, 0x6273E0, 0xB3D90), the frame loop's fence (the call at 0x8A7C4), a resource fence (0x1A8D70 the
 * release, 0x1A8449 the create), a terrain destroy's (0x1ADFB3), the game's wait on its queue-14 generator job (game
 * 0xF6E6E6); NULL none. */
static const char* MoveStallWait(uintptr_t a, uintptr_t exe, size_t exeSize, uintptr_t game, size_t gameSize, char* out, size_t size)
{
    static const uint32_t kWaits[] = {0x626900u, 0x624D00u, 0x627160u, 0x6273E0u, 0xB3D90u};
    if (a >= game && a < game + gameSize) return a - game == 0xF6E6E6u ? "the game's wait on its queue-14 generator job (game 0xF6E6E6)" : NULL;
    if (!(a >= exe && a < exe + exeSize)) return NULL;
    if (a - exe == 0x8A7C9u) return "the frame loop's fence (exe 0x8A7C4)";
    if (a - exe == 0x1A8D75u) return "a resource release's fence (exe 0x1A8D70)";
    if (a - exe == 0x1A844Eu) return "a resource create's fence (exe 0x1A8449)";
    if (a - exe == 0x1ADFB8u) return "a terrain destroy's fence (exe 0x1ADFB3)";
    const uintptr_t f = MoveStallFunction(a);
    for (size_t k = 0; k < sizeof kWaits / sizeof kWaits[0]; k++)
        if (f == exe + kWaits[k]) { snprintf(out, size, "the job system's wait or help (exe 0x%X)", kWaits[k]); return out; }
    return NULL;
}

static size_t MoveStallImage(uintptr_t base)
{
    IMAGE_DOS_HEADER dos; uint32_t size = 0;
    if (base == 0 || SafeRead(base, &dos, sizeof dos) || SafeRead(base + (uintptr_t)dos.e_lfanew + 0x50, &size, 4)) return 0; /* OptionalHeader.SizeOfImage */
    return size;
}
#endif

#ifdef CO_RESEARCH
#define MOVE_STALL_STACK 0x4000
#define MOVE_STALL_FRAMES 32
#define MOVE_STALL_QUEUES 21 /* 0x624D00's queues: 7 categories of 3 */
/* The watcher. Between SuspendThread and ResumeThread it only reads the context and copies the stack, which take no
 * lock the game thread may hold (no allocation, log, module or unwind lookup); everything else runs after the resume. */
static DWORD WINAPI MoveStallWatch(void* unused)
{
    (void)unused;
    static uint8_t copy[MOVE_STALL_STACK];
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), game = g_world.game;
    const size_t exeSize = MoveStallImage(exe), gameSize = MoveStallImage(game);
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_moveFrame.thread);
    if (thread == NULL) { Note("difficulty: moving tiles: stall samples off: the game thread could not be opened (%lu)", GetLastError()); return 0; }
    LARGE_INTEGER hz; QueryPerformanceFrequency(&hz);
    for (;;)
    {
        Sleep(10);
        const LONG seq = g_moveStall.seq;
        MemoryBarrier();
        const int phase = (int)g_moveStall.phase;
        const int64_t since = g_moveStall.since;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (!MoveStallDue(phase, since, now.QuadPart, (double)hz.QuadPart, seq, g_moveStall.sampled, g_moveStall.samples, g_moveFrame.inBuild)) continue;
        g_moveStall.sampled = seq;
        CONTEXT c; memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        size_t len = 0;
        if (SuspendThread(thread) == (DWORD)-1) continue;
        const NT_TIB* tib = (const NT_TIB*)g_moveFrame.teb;
        if (GetThreadContext(thread, &c) && g_moveStall.seq == seq && tib != NULL)
        {
            /* the stopped thread's own limits: [rsp, StackBase) is committed when rsp lies at or above StackLimit */
            const uintptr_t lo = (uintptr_t)tib->StackLimit, hi = (uintptr_t)tib->StackBase;
            if (c.Rsp >= lo && c.Rsp < hi) { len = hi - c.Rsp < sizeof copy ? (size_t)(hi - c.Rsp) : sizeof copy; memcpy(copy, (const void*)(uintptr_t)c.Rsp, len); }
        }
        LARGE_INTEGER at; QueryPerformanceCounter(&at);
        ResumeThread(thread);
        if (len == 0) continue;
        InterlockedIncrement(&g_moveStall.samples);
        char where[64], trail[1600], queues[256], waitText[64]; uint64_t off = 0; size_t n = 0; int frames = 0, capped = 0;
        const char* wait = MoveStallWait((uintptr_t)c.Rip, exe, exeSize, game, gameSize, waitText, sizeof waitText);
        MoveStallWhere((uintptr_t)c.Rip, exe, exeSize, game, gameSize, where, sizeof where, &off);
        trail[0] = 0;
        /* MOVE_STALL_FRAMES candidates; past them only the game's frame update [0xAB4520, 0xAB5200), which the reading needs */
        for (size_t i = 0; i + 8 <= len && n + 40 < sizeof trail; i += 8)
        {
            uint64_t v; memcpy(&v, copy + i, 8);
            const int inGame = v >= game && v < game + gameSize;
            if (!((v >= exe && v < exe + exeSize) || inGame) || !MoveStallCallBefore((uintptr_t)v)) continue;
            if (frames >= MOVE_STALL_FRAMES && !(inGame && v - game >= 0xAB4520u && v - game < 0xAB5200u)) { capped = 1; continue; }
            if (wait == NULL) wait = MoveStallWait((uintptr_t)v, exe, exeSize, game, gameSize, waitText, sizeof waitText);
            n += (size_t)snprintf(trail + n, sizeof trail - n, "%s%s+0x%llX", frames ? ", " : "", inGame ? "game" : "exe", (unsigned long long)(v - (inGame ? game : exe)));
            frames++;
        }
        /* every engine queue with jobs on it, q:n; "unread" when the scheduler does not read */
        size_t qn = 0; queues[0] = 0;
        for (int q = 0; q < MOVE_STALL_QUEUES; q++)
        {
            const uint32_t count = MoveStallQueued(exe, q);
            if (count == 0xFFFFFFFFu) { snprintf(queues, sizeof queues, "unread"); qn = 1; break; }
            if (count != 0) qn += (size_t)snprintf(queues + qn, sizeof queues - qn, "%s%d:%u", qn ? " " : "", q, count);
        }
        /* the copy's length: a full one (MOVE_STALL_STACK) can have cut the trail before the game's frame update */
        Note("difficulty: moving tiles: stall sample: the game thread %.0f ms into the %s at %s+0x%llX; wait: %s; stack (%u bytes%s): %s; queues pending: %s; "
            "jobs out: shape %d, tile %d", (double)(at.QuadPart - since) * 1000.0 / (double)hz.QuadPart, phase == MS_EARLY ? "early update" : "frame after the mover",
            where, (unsigned long long)off, wait != NULL ? wait : "none named", (unsigned)len, capped ? ", capped" : "", frames ? trail : "none found", qn ? queues : "none",
            (int)g_shape.running, g_nearJob.inflight > 0);
    }
}

/* Once a session, at the first build frame: on unless HELLFORGE_XL_STALL_SAMPLE=0. */
static void MoveStallStart(void)
{
    static int started;
    if (started || !g_moveFrame.inBuild) return;
    started = 1;
    char v[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_XL_STALL_SAMPLE", v, sizeof v) > 0 && v[0] == '0') { Note("difficulty: moving tiles: stall samples off (HELLFORGE_XL_STALL_SAMPLE=0)"); return; }
    HANDLE t = CreateThread(NULL, 0, MoveStallWatch, NULL, 0, NULL);
    if (t != NULL) CloseHandle(t);
    Note("difficulty: moving tiles: stall samples %s (a game-thread stretch over %.0f ms outside the mod's marks, %d a build; research)", t != NULL ? "on" : "off: no thread",
        MOVE_STALL_MS, MOVE_STALL_SAMPLES);
}
#endif

/* CoXlTick's start (end 0) and end (co_xl.c). */
static void MoveFrameTick(int end)
{
    LARGE_INTEGER now, hz; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&hz);
    if (end) { MoveFrameMark(MF_TICK_END, now.QuadPart, (double)hz.QuadPart); MoveStallPhase(MS_EARLY, now.QuadPart); return; }
    MoveFrameMark(MF_TICK, now.QuadPart, (double)hz.QuadPart);
    MoveStallPhase(MS_NONE, now.QuadPart);
#ifdef CO_RESEARCH
    g_moveFrame.teb = (uintptr_t)NtCurrentTeb();
#endif
}

/* Before the level's own update: the scatter cut's close counts this update (difficulty_world_diag.c). */
static void MoveScatterTick(void) { TdScatterMergeTick((uint64_t)(uintptr_t)g_move.ctx); }

static int LwFpWorldOn(void); /* living_world_fparts.c */
/* The wall frame that ends at this level update's t0 (the last t0 to this one) into the build's tables, with the frame
 * fence and the world update (ms, -1 untimed) inside it; then the new frame's state at t0. */
static void MoveWallClose(int64_t t0, double hz, int mv, int ns, int op, int conf, double world)
{
    if (g_moveWall.last.QuadPart != 0 && (g_moveWall.mv != MV_IDLE || g_moveCost.pending) && hz > 0)
    {
        const double wall = (double)(t0 - g_moveWall.last.QuadPart) * 1000.0 / hz;
        const double fence = MoveFrameFenceMs(hz); /* this frame's early update lies inside the wall frame */
        MoveWallAdd(wall, g_moveWall.mv);
        MoveSlowAdd(&g_moveCost.clock, wall, g_moveWall.mv, g_moveWall.ns, g_moveWall.op, g_moveWall.conf);
        MoveFenceAdd(wall, fence, g_moveWall.mv, g_moveWall.op);
        if (g_moveWall.op == 0x63) Split63WallAdd(wall, fence, world, g_moveWall.mover, g_moveWall.toggles, g_moveWall.sp0, g_moveWall.sp1);
    }
    if (g_moveWall.op == 0x63 && op != 0x63) Split63WallNote();
    g_moveWall.last.QuadPart = t0; g_moveWall.mv = mv; g_moveWall.ns = ns; g_moveWall.op = op; g_moveWall.conf = conf; g_moveWall.sp0 = g_split.state;
    g_moveWall.mover = 0; g_moveWall.toggles = 0;
}
/* After MoveTick: its time and the shows and hides it posted, the split's state after it. */
static void MoveWallTicked(int64_t ticks, double hz, uint32_t toggles)
{
    g_moveWall.sp1 = g_split.state; g_moveWall.mover = hz > 0 ? (double)ticks * 1000.0 / hz : 0; g_moveWall.toggles = toggles;
}
/* A build frame whose MoveTick took more than this holds a step of game-thread work: the stamp slices run to
 * MOVE_UNSTAMP_MS / MOVE_SELECT_MS. The other frames poll jobs: 2209 ms over 980 frames with 22 over 50 ms leaves
 * the rest about 1.1 ms or less each (session f8a9e7ce, a tile's build). */
#define MOVE_STEP_MS 1.5
/* The streamer's tick (g_streamTicks) of the last build frame and of the last heavy step, and that frame's MoveTick
 * time. The level update (0xAB519E) runs before the late update with the streamer's tick (0xAB612D) in the frame
 * function 0xAB5000, so this frame's are known when the streamer asks. */
static uint32_t g_moveBuildAt, g_moveStepAt;
static double g_moveStepMs;
static void MoveStepMark(double ms)
{
    g_moveBuildAt = g_streamTicks + 1; g_moveStepMs = ms; /* the streamer's tick of this frame */
    if (ms > MOVE_STEP_MS) g_moveStepAt = g_moveBuildAt;
}
/* A heavy build step ran in this frame or the last: the streamer's change frame waits (it never adds to a slice). */
static int MoveStepRecent(void) { return g_moveStepAt != 0 && g_streamTicks - g_moveStepAt <= 1u; }
/* A tile was building in this frame or the last: its bodies change layer 4 then. */
static int MoveBuildRecent(void) { return g_moveBuildAt != 0 && g_streamTicks - g_moveBuildAt <= 1u; }
/* This frame's MoveTick time while a tile builds, else 0: the streamer's change frame leaves it room. */
static double MoveStepMsNow(void) { return g_moveBuildAt != 0 && g_moveBuildAt == g_streamTicks ? g_moveStepMs : 0.0; }

/* The mod's parts are timed for the frame-time lines (co_framecost.h); the game's own update between them is not. */
static void MoveLevelUpdate(uint64_t level)
{
    LARGE_INTEGER entry, hz; QueryPerformanceCounter(&entry); QueryPerformanceFrequency(&hz);
    MoveStallPhase(MS_NONE, entry.QuadPart);
    MoveFrameMark(MF_LEVEL, entry.QuadPart, (double)hz.QuadPart);
    int64_t fc = co_framecost_now();
    MoveScatterTick();
    co_framecost_add(CO_FC_SCATTER, fc);
    NearResetTick();
    NearJobLevelUpdate((uintptr_t)level, g_move.levelPtr); /* a level leaving play: its tile job joined before the game's update */
    g_moveUpdate(level);
    int mv = g_move.state, ns = g_near.state, step = g_near.step, conf = g_conform.phase, bld = g_near.building;
    const int op = step >= 0 && step < g_near.steps && step < NEAR_STEPS ? g_near.stepOp[step] : -1;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    MoveFrameMark(MF_T0, t0.QuadPart, (double)hz.QuadPart);
    g_moveFrame.f.mv = mv; g_moveFrame.f.ns = ns; g_moveFrame.f.op = op;
    g_moveFrame.f.shape |= g_shape.running != 0; g_moveFrame.f.tile |= g_nearJob.inflight > 0;
    MoveWallClose(t0.QuadPart, (double)hz.QuadPart, mv, ns, op, conf, LwFpWorldOn() ? co_framecost_world_ms() : -1.0);
    const uint32_t toggles = g_split.hides + g_split.shows;
    MoveTick(level);
    QueryPerformanceCounter(&t1);
    const uint32_t posted = g_split.hides + g_split.shows; /* the split cleared in MoveTick: its counts start at 0 */
    MoveWallTicked(t1.QuadPart - t0.QuadPart, (double)hz.QuadPart, posted >= toggles ? posted - toggles : posted);
    MoveFrameMark(MF_T1, t1.QuadPart, (double)hz.QuadPart);
    g_moveFrame.f.building = g_moveFrame.inBuild = mv != MV_IDLE || g_move.state != MV_IDLE;
    g_moveFrame.f.shape |= g_shape.running != 0; g_moveFrame.f.tile |= g_nearJob.inflight > 0;
    MoveStallPhase(MS_LATE, t1.QuadPart);
#ifdef CO_RESEARCH
    MoveStallStart();
#endif
    g_hitchCost.tiles += (float)((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
    co_framecost_add(CO_FC_MOVE, t0.QuadPart);
    if (mv != MV_IDLE || g_move.state != MV_IDLE)
    {
        const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
        MoveStepMark(ms);
        MoveCostAdd(ms, mv, ns, step, conf, bld);
        MoveSlowAdd(&g_moveCost.tick, ms, mv, ns, op, conf);
        g_moveCost.pending = 1;
    }
    else if (g_moveCost.pending) MoveCostNote();
    const double grass = HitchNow();
    fc = co_framecost_now();
    FarGrassTick(level);
    co_framecost_add(CO_FC_FARGRASS, fc);
    FgGroundTick(level);
    g_hitchCost.tiles += (float)(HitchNow() - grass);
}

static void MoveInstall(uintptr_t game)
{
    if (g_moveInstalled || !MoveOn()) return;
    if (!ApiCodeMatches(game + MOVE_LEVEL_UPDATE, kMoveUpdatePin) || !ApiCodeMatches(game + MOVE_CRATER_UPDATE, kMoveCraterUpdatePin) ||
        !ApiCodeMatches(game + MOVE_CRATER_RENDER, kMoveCraterRenderPin))
    { Note("difficulty: moving tiles: the level update or the terrain deformation is not the reviewed code; the detailed terrain stays at the drop point"); return; }
    if (LoaderHook("extra large maps' crater hold (update)", game + MOVE_CRATER_UPDATE, (void*)MoveCraterUpdate, (void**)&g_moveCraterUpdate) ||
        LoaderHook("extra large maps' crater hold (render)", game + MOVE_CRATER_RENDER, (void*)MoveCraterRender, (void**)&g_moveCraterRender) ||
        LoaderHook("extra large maps' moving tiles", game + MOVE_LEVEL_UPDATE, (void*)MoveLevelUpdate, (void**)&g_moveUpdate))
    { Note("difficulty: moving tiles: a hook did not go in; the detailed terrain stays at the drop point"); return; }
    g_moveInstalled = 1;
    FarGrassInstall(game); /* its bakes run from the level update hook above */
    Note("difficulty: moving tiles: installed (level update 0x%X, crater hold 0x%X / 0x%X)", MOVE_LEVEL_UPDATE, MOVE_CRATER_UPDATE, MOVE_CRATER_RENDER);
}

/* A level init (ship 0) or the ship's set-up (ship 1): a complex mission that ended inside a tile build or before its
 * op 0x69 spawn never reached the code that ends these, and each would act on the next levels. A complex level sets
 * all of them up after its own level init (0x69, the replay, a build in play), so this clears only an older level's. */
static void NearLevelLeft(int ship)
{
    NearBandJoin(); /* first: the pass reads the level's maps */
    NearJobLevelLeft(); /* and a composite tile job reads its records */
    NearLayerSettle();
    MoveBuildLeft(ship); /* the merge cameras go before the terrains they draw */
    NearAutoForget(); /* after the leave's shape join: its MoveCostNote comes later, at the ship */
    MatWaitDrain(); /* op 0x41's setup fence waits: none outlives its level */
    /* A release still owed is never sent outside play. A level init has asked for the package before this hook
     * (0xF712FF, a no-op while it is listed, exe 0x603746..0x603828), and its 0x43 asks again only when has() is 0
     * (0xF61E8B..0xF61EA9), which stays 1 until the queued unload is reached (exe 0x603290): a release here would
     * take the package from this level's own merge. The level holds it now and its 0x67 releases it (0xC17CF7). */
    if (!ship) g_move.pkgPending = 0;
    NearTilesDestroy(ship); /* the world outlives the level: its tiles go with the level */
    TerrainLogState(ship ? "at the ship" : "at a level init");
    NearOneMipDisarm();
    InterlockedExchange(&g_nearActive, 0);
    MoveHold(0);
    FarGrassLevelLeft();
    FgGroundLevelLeft();
    /* only the ship: a level init advances g_world.levels, which every tile and hide gate already compares */
    if (ship) { NearTilesClear(); g_hide.handle = 0; }
}

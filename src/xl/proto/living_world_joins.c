/* Issue C's 10 s run-log lines (../co_joinstat.h), every build, in play on a complex level: the game thread's AI joins
 * with their tail and the budget each step ran with, and what units are about: the AI, the living world's (job 1),
 * the natives, the garrisons active and filled, and how fast the first player moves. A traversal's joins were the
 * streamed forest's navmesh changes, not the units (issue C analysis): these two lines tell the two apart. */
#define LW_JOINS_WINDOW_MS 10000
#define LW_JOINS_FASTEST 120.0f /* m/s: a faster move between two samples is a teleport or a drop, not the player's speed */
static struct
{
    CoJoinStat st; ULONGLONG at, sampleAt; LONG mission; int lines, haveLast; float last[2]; double path, fastest;
} g_lwJoins;

/* Game state 4 is play ([[game + 0x3326340] + 0xAC21C], as co_xl.c's frame windows read it). */
static int LwJoinsInPlay(void)
{
    uintptr_t world = 0; uint32_t state = 0;
    return LwLive() && g_difficultyGame != 0 && !SafeRead(g_difficultyGame + 0x3326340u, &world, 8) && world != 0 &&
        !SafeRead(world + 0xAC21Cu, &state, 4) && state == 4;
}

/* The first player's place a second apart, from the streamer's list (every peer has it); a busy lock skips one. */
static void LwJoinsSample(ULONGLONG now)
{
    float at[2]; int have = 0;
    if (!TryAcquireSRWLockShared(&g_streamLock)) return;
    if ((int)g_stream.playerCount - (g_streamObserverOn ? 1 : 0) >= 1) { at[0] = g_streamPlayerAt[0][0]; at[1] = g_streamPlayerAt[0][1]; have = 1; }
    ReleaseSRWLockShared(&g_streamLock);
    if (!have || !isfinite(at[0]) || !isfinite(at[1])) { g_lwJoins.haveLast = 0; g_lwJoins.sampleAt = now; return; }
    if (g_lwJoins.haveLast && now - g_lwJoins.sampleAt <= 2000)
    {
        const double d = sqrt((double)(at[0] - g_lwJoins.last[0]) * (at[0] - g_lwJoins.last[0]) + (double)(at[1] - g_lwJoins.last[1]) * (at[1] - g_lwJoins.last[1]));
        const double v = d * 1000.0 / (double)(now - g_lwJoins.sampleAt);
        if (v <= LW_JOINS_FASTEST) { g_lwJoins.path += d; if (v > g_lwJoins.fastest) g_lwJoins.fastest = v; }
    }
    g_lwJoins.last[0] = at[0]; g_lwJoins.last[1] = at[1]; g_lwJoins.haveLast = 1; g_lwJoins.sampleAt = now;
}

static void LwJoinsLines(ULONGLONG now)
{
    const CoJoinStat* s = &g_lwJoins.st;
    const double secs = (double)(now - g_lwJoins.at) / 1000.0;
#define LW_JS_KIND(k) s->kindFrames[k], s->kindFrames[k] ? s->kindSum[k] / s->kindFrames[k] : 0.0, s->kindWorst[k]
    Note("living world: AI joins (%.1f s): %u frames, mean %.2f ms a frame, p99 %.1f ms, worst %.1f ms; by the budget the step ran with: lever %d us %u (mean %.2f, worst %.1f), stock %u (mean %.2f, worst %.1f), nav hold raise %u (mean %.2f, worst %.1f), drain %u (mean %.2f, worst %.1f), not known %u",
        secs, s->frames, s->frames ? s->sum / s->frames : 0.0, co_joinstat_quantile(s, 0.99), s->worst, g_lwAib.lowUs,
        LW_JS_KIND(CO_JS_LOW), LW_JS_KIND(CO_JS_STOCK), LW_JS_KIND(CO_JS_RAISE), LW_JS_KIND(CO_JS_DRAIN), s->unknown);
#undef LW_JS_KIND
    const uint32_t ai = LwAiCount(), job1 = g_lwWorld.dir ? EvpU32(g_lwWorld.dir + 0x614, 0) : 0;
    uint32_t active = 0, went = 0;
    for (int i = 0; i < g_lwGarrisonCount; i++)
    { active += g_lwGarrisons[i].wasActive != 0; went += g_lwGarrisons[i].activeAt >= g_lwJoins.at && g_lwGarrisons[i].activeAt <= now; }
    char observer[64] = "";
    if (g_streamObserverOn) snprintf(observer, sizeof observer, "; the stream observer at (%.0f, %.0f)", (double)g_streamObserverAt[0], (double)g_streamObserverAt[1]);
    Note("living world: units (%.1f s): AI %u: the living world's (job 1) %u, natives %d; garrisons active %u, %u went active in the window; the first player at (%.0f, %.0f)%s, %.1f m/s over the window, %.1f m/s at most over a second%s",
        secs, ai, job1, (int)ai - (int)job1, active, went, (double)g_lwJoins.last[0], (double)g_lwJoins.last[1], g_lwJoins.haveLast ? "" : " (not read)",
        secs > 0 ? g_lwJoins.path / secs : 0.0, g_lwJoins.fastest, observer);
}

/* Game thread, once a frame from the AI budget's frame: `join` the frame's joins (ms), `kind` the budget kind of the
 * step they waited for (CO_JS_*, under 0 not known). A window restarts after play pauses or a mission changes. */
static void LwJoinsFrame(double join, int kind)
{
    const ULONGLONG now = GetTickCount64();
    if (!LwJoinsInPlay()) { g_lwJoins.at = 0; return; }
    if (g_lwJoins.mission != g_lwMission) { g_lwJoins.mission = g_lwMission; g_lwJoins.lines = 0; g_lwJoins.at = 0; }
    if (g_lwJoins.at == 0)
    {
        co_joinstat_clear(&g_lwJoins.st); g_lwJoins.at = now; g_lwJoins.path = g_lwJoins.fastest = 0; g_lwJoins.sampleAt = 0; g_lwJoins.haveLast = 0;
        return; /* this frame's joins began before the window */
    }
    co_joinstat_add(&g_lwJoins.st, join, kind);
    if (now - g_lwJoins.sampleAt >= 1000) LwJoinsSample(now);
    if (now - g_lwJoins.at < LW_JOINS_WINDOW_MS) return;
    if (g_lwJoins.lines < CO_JS_LINES) LwJoinsLines(now);
    else if (g_lwJoins.lines == CO_JS_LINES) Note("living world: AI joins: %d window lines this mission; no more until the next", CO_JS_LINES);
    g_lwJoins.lines++;
    co_joinstat_clear(&g_lwJoins.st); g_lwJoins.at = now; g_lwJoins.path = g_lwJoins.fastest = 0;
}

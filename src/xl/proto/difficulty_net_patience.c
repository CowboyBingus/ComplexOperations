/* On unless HELLFORGE_XL_PATIENCE=0: the game sets the transport's pong timeout to 10 s (game 0x134D719 through the
 * network API's +0x68, exe 0x350350). An extra large level's generation holds a joiner's game thread for about 20 s,
 * in which it answers nothing, so the host's connection to it went dead (exe 0x2A5D16) and the host removed it. While
 * an extra large level is current, every connection waits the engine's own default instead (60 s, exe 0x2A9F5B). */
#define PAT_MANAGER 0x1A10278u      /* the network manager: +0x98 the transport */
#define PAT_TRANSPORT_VT 0x1676270u
#define PAT_DEFAULT 0xC0u           /* transport: the timeout a new connection gets (0x2AC520 writes it) */
#define PAT_COUNT 0xA0u             /* transport: connections, the array at +0xA8 */
#define PAT_TIMEOUT 0x44u           /* connection: compared with its silence +0x187AC at 0x2A5BC1 */
#define PAT_WAIT 60.0f

static struct { int state; ULONGLONG next; float game; int raised; } g_patience;

/* The pins hold the timer's compare, the engine's default, the per-connection copy and the game's 10 s. */
static int PatienceReviewed(uintptr_t exe, uintptr_t game)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_XL_PATIENCE", t, sizeof t) > 0 && t[0] == '0') return 0;
    if (exe == 0 || game == 0) return 0;
    if (ApiCodeMatches(exe + 0x2A5BC1u, "F3 0F 10 4B 44") && ApiCodeMatches(exe + 0x2A5BDBu, "0F 2F C1") &&
        ApiCodeMatches(exe + 0x2A9F5Bu, "41 C7 86 C0 00 00 00 00 00 70 42") && ApiCodeMatches(exe + 0x2AC5CBu, "0F 11 42 3C") &&
        ApiCodeMatches(exe + 0x350396u, "F3 0F 11 44 24 28") && ApiCodeMatches(game + 0x134D719u, "B9 0A 00 00 00"))
        return 1;
    Note("difficulty: extra large maps: the transport's pong timeout is not the reviewed code; left at the game's");
    return 0;
}

/* One connection's timeout: up to `want`, or from PAT_WAIT back to `want`. */
static uint32_t PatienceOne(uintptr_t slot, int raise, float want)
{
    uintptr_t connection = 0; float now = 0;
    if (SafeRead(slot, &connection, 8) || connection == 0 || SafeRead(connection + PAT_TIMEOUT, &now, 4)) return 0;
    if (raise ? !(now < want) : now != PAT_WAIT) return 0;
    *(volatile float*)(connection + PAT_TIMEOUT) = want;
    return 1;
}

/* One pass over the default and the live connections: up to PAT_WAIT, or back to the game's value. */
static uint32_t PatienceSet(uintptr_t transport, int raise)
{
    volatile float* standard = (volatile float*)(transport + PAT_DEFAULT);
    uint32_t count = *(const volatile uint32_t*)(transport + PAT_COUNT), changed = 0;
    const uintptr_t* list = *(const uintptr_t* const volatile*)(transport + PAT_COUNT + 8);
    if (raise && *standard < PAT_WAIT) { g_patience.game = *standard; *standard = PAT_WAIT; }
    if (!raise && !(g_patience.game > 0.0f)) return 0; /* the game never set its own: nothing to put back */
    if (!raise) *standard = g_patience.game;
    if (list == NULL) return 0;
    for (uint32_t i = 0; i < count && i < 64; i++) changed += PatienceOne((uintptr_t)&list[i], raise, raise ? PAT_WAIT : g_patience.game);
    return changed;
}

/* Twice a second from TickDetour (packages.c), on the game thread, which also runs the transport's update. */
static void NetPatienceTick(void)
{
    ULONGLONG now = GetTickCount64();
    if (g_patience.state < 0 || now < g_patience.next) return;
    g_patience.next = now + 500;
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL), manager = 0, transport = 0, vt = 0;
    if (g_patience.state == 0) g_patience.state = PatienceReviewed(exe, ApiModuleBase("game.dll", NULL)) ? 1 : -1;
    if (g_patience.state < 0) return;
    if (SafeRead(exe + PAT_MANAGER, &manager, 8) || manager == 0 || SafeRead(manager + 0x98, &transport, 8) || transport == 0 ||
        SafeRead(transport, &vt, 8) || vt != exe + PAT_TRANSPORT_VT)
        return;
    int raise = g_world.live > 1.0f;
    if (!raise && !g_patience.raised) return;
    float before = *(const volatile float*)(transport + PAT_DEFAULT);
    uint32_t changed = PatienceSet(transport, raise);
    if (raise && before < PAT_WAIT)
        Note("difficulty: extra large maps: connections wait %.0f s for an answer instead of the game's %.0f s (%u changed now)", PAT_WAIT, before, changed);
    if (!raise) Note("difficulty: extra large maps: connections wait the game's %.0f s for an answer again (%u changed)", g_patience.game, changed);
    g_patience.raised = raise;
}

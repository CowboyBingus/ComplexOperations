/* The loader's helpers for the extra large world's prototype (co_shim.h), over HfApi and the mod's own parts. The
 * readers below are HellForge's event_polls.c and actions.c readers; what differs from them is said at each. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "co_shim.h"

/* SafeRead as the prototype has it: small reads copied in place, a fault turned into the failure return. */
#include "proto/safe_read.c"

void co_shim_install(void) { InstallSafeRead(); }
int co_safe_read(uintptr_t address, void* buffer, size_t size) { return SafeRead(address, buffer, size); }

/* ---- logs ---------------------------------------------------------------------------------------------------------- */

void co_note(const char* format, ...)
{
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    if (g_coApi != NULL) g_coApi->log(g_coMod, "%s", text);
}

/* The engine's own log option: the diagnostics are written only with it, as in the loader. */
int co_diagnostic_log(void)
{
    static int on = -1;
    if (on < 0)
    {
        const wchar_t* command = GetCommandLineW();
        on = command != NULL && wcsstr(command, L"--log-to-file") != NULL;
    }
    return on;
}

void co_note_diagnostic(const char* format, ...)
{
    char text[1024];
    va_list arguments;
    if (!co_diagnostic_log()) return;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    if (g_coApi != NULL) g_coApi->log(g_coMod, "%s", text);
}

/* The host gives the folder as UTF-8; the prototype builds wide paths from it. Asked again while it is empty. */
const wchar_t* co_shim_logs(void)
{
    static wchar_t logs[MAX_PATH];
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    if (!logs[0] && g_coApi != NULL)
    {
        const char* folder = g_coApi->logs_folder(g_coMod);
        if (folder == NULL || !folder[0] || MultiByteToWideChar(CP_UTF8, 0, folder, -1, logs, MAX_PATH) == 0) logs[0] = 0;
    }
    ReleaseSRWLockExclusive(&lock);
    return logs;
}

HANDLE co_open_log(const wchar_t* name)
{
    wchar_t path[MAX_PATH];
    const wchar_t* logs = co_shim_logs();
    if (!logs[0] || swprintf(path, MAX_PATH, L"%ls\\%ls", logs, name) < 0) return INVALID_HANDLE_VALUE;
    return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

const char* co_shim_build(void)
{
    const char* build = g_coApi != NULL ? g_coApi->build_id() : NULL;
    return build != NULL ? build : "unknown";
}

/* ---- memory and hooks ---------------------------------------------------------------------------------------------- */

/* The mod's allocator reaches 1.75 GB from its target; from the image's middle that covers every instruction of it. */
uint8_t* co_find_near_game(uintptr_t game, size_t bytes, DWORD type)
{
    size_t image = 0;
    if (g_coApi != NULL) g_coApi->module_base("game.dll", &image);
    return (uint8_t*)co_alloc_near_as((game + image / 2) & ~(uintptr_t)0xFFFF, (bytes + 0xFFF) & ~(size_t)0xFFF, type);
}

uint8_t* co_alloc_near_game(uintptr_t game, size_t bytes) { return co_find_near_game(game, bytes, MEM_COMMIT | MEM_RESERVE); }

/* The loader put its own hooks last in a function's chain. A mod's are all before the loader's; the lowest priority
 * is the nearest a mod gets. `what` names it in the mod's log, as the loader's log did. */
int co_loader_hook(const char* what, uintptr_t target, void* detour, void** original)
{
    int refused = g_coApi->hook_ex(g_coMod, target, detour, original, INT32_MIN);
    if (refused) co_note("hook refused: %s", what);
    return refused;
}

uint64_t co_murmur64a(const void* key, size_t length)
{
    const uint64_t m = 0xC6A4A7935BD1E995ull;
    uint64_t h = length * m;
    const uint8_t* data = (const uint8_t*)key;
    size_t whole = length / 8 * 8;
    for (size_t i = 0; i < whole; i += 8)
    {
        uint64_t k;
        memcpy(&k, data + i, 8);
        k *= m;
        k ^= k >> 47;
        h = (h ^ (k * m)) * m;
    }
    if (whole != length)
    {
        uint64_t tail = 0;
        for (size_t i = length; i > whole; i--) tail = (tail << 8) | data[i - 1];
        h = (h ^ tail) * m;
    }
    h ^= h >> 47;
    h *= m;
    return h ^ (h >> 47);
}

/* The loaded module an address lies in, as "name+0xoffset"; 0 when it lies in none. */
int co_module_at(uintptr_t at, char* out, size_t size)
{
    HMODULE module = NULL;
    wchar_t path[MAX_PATH];
    if (at < 0x10000 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)at, &module) ||
        module == NULL || !GetModuleFileNameW(module, path, MAX_PATH)) return 0;
    const wchar_t* name = wcsrchr(path, L'\\');
    snprintf(out, size, "%ls+0x%llx", name != NULL ? name + 1 : path, (unsigned long long)(at - (uintptr_t)module));
    return 1;
}

/* ---- the game's data ----------------------------------------------------------------------------------------------- */

#define EVT_STATE_GAME 0x3326340u
#define EVT_STATE_AT 0xAC21Cu
#define EVT_LOCAL_PEER 0x347CEF0u
#define EVT_LOCAL_PEER_AT 0xB398u
#define EVP_HEALTH 0x3326688u
#define EVP_ENTITIES 0x346BF98u
#define EVP_NETWORK_MAP 0xF22EC8u
#define EVP_ENTITY_MAP 0xF1AEB0u
#define EVP_EXE_ENTITIES 0x1B135B8u
#define EVP_EXE_UNITS 0x1A100F0u
#define ACT_AVATAR 0x4D1C334D294DFA97ull
/* The player list's members in the game's own layout; co_managers_offset says where each is now. */
#define EVP_PLAYER_COUNT 0x84u
#define EVP_PLAYER_DESCRIPTORS 0xE8u
#define EVP_PLAYER_PEERS 0x2C8u
#define EVP_PLAYER_LIFE 0x2E0u
#define EVP_PLAYER_NETWORK 0x3A8u

static const int64_t kRecordsVanilla = 0xF32F18;
static const int64_t* g_records = &kRecordsVanilla;

void co_shim_entity_records(const int64_t* displacement) { g_records = displacement != NULL ? displacement : &kRecordsVanilla; }
#define EVP_DESCRIPTORS ((uintptr_t)*g_records)
#define EVP_NETWORK_ENTITY ((uintptr_t)*g_records + 8u)

int co_evp_ptr(uintptr_t at, uintptr_t* out)
{
    uint64_t v;
    if (SafeRead(at, &v, 8) || v < 0x10000 || (v >> 32) > 0x7FFF) return 0;
    *out = (uintptr_t)v;
    return 1;
}
uint32_t co_evp_u32(uintptr_t at, uint32_t fallback) { uint32_t v; return SafeRead(at, &v, 4) ? fallback : v; }

/* A found key's value at `at`: 1 with *value, 0 unreadable or none (-1). */
static int EvpHashValue(uintptr_t at, uint32_t* value)
{
    uint32_t v;
    if (SafeRead(at, &v, 4) || v == 0xFFFFFFFFu) return 0;
    *value = v;
    return 1;
}

/* The game's u32 hash maps: a header {u64 buckets, u32 capacity, u32 empty key, u32 multiplier}, buckets of `stride`
 * bytes {u32 key, ... the value at valueAt}, probed from key * multiplier. 1 with *value, 0 absent or unreadable. */
int co_evp_hash(uintptr_t header, uint32_t key, uint32_t stride, uint32_t valueAt, uint32_t* value)
{
    uint8_t h[20];
    if (key == 0 || SafeRead(header, h, sizeof h)) return 0;
    uint64_t buckets; uint32_t capacity, empty, multiplier;
    memcpy(&buckets, h, 8); memcpy(&capacity, h + 8, 4); memcpy(&empty, h + 12, 4); memcpy(&multiplier, h + 16, 4);
    if (buckets < 0x10000 || capacity == 0 || capacity > 1048576) return 0;
    uint32_t start = key * multiplier, probes = capacity < 4096 ? capacity : 4096;
    for (uint32_t i = 0; i < probes; i++)
    {
        uint64_t at = buckets + (uint64_t)((start + i) % capacity) * stride;
        uint32_t k;
        if (SafeRead((uintptr_t)at, &k, 4)) return 0;
        if (k == key) return EvpHashValue((uintptr_t)at + valueAt, value);
        if (k == empty) return 0;
    }
    return 0;
}

/* An entity's descriptor through the entity manager's map: its resource (type), 0 when it does not resolve. */
uint64_t co_evp_entity_resource(uintptr_t game, uint32_t entity, uint32_t* unit)
{
    uintptr_t em; uint32_t index; uint8_t d[24];
    if (!entity || !co_evp_ptr(game + EVP_ENTITIES, &em) || !co_evp_hash(em + EVP_ENTITY_MAP, entity, 8, 4, &index) || index >= 0x100000 ||
        SafeRead(em + EVP_DESCRIPTORS + (uintptr_t)index * 24, d, sizeof d)) return 0;
    uint32_t named; memcpy(&named, d + 8, 4);
    if (named != entity) return 0;
    if (unit) memcpy(unit, d + 12, 4);
    uint64_t resource; memcpy(&resource, d, 8);
    return resource;
}

/* Whether an entity is alive in the engine (its index's generation, helldivers2.exe). */
int co_evp_alive(uint32_t entity)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), m, gens;
    uint32_t index = entity & 0x3FFFFFu, size;
    uint8_t gen;
    return co_evp_ptr(exe + EVP_EXE_ENTITIES, &m) && !SafeRead(m + 0x80, &size, 4) && index < size && co_evp_ptr(m + 0x88, &gens) &&
        !SafeRead(gens + index, &gen, 1) && gen == ((entity >> 22) & 0xFF);
}

/* The player list: peer, player entity, Helldiver (0: none), lifecycle, local. The loader's reader took four players
 * at the game's offsets; a build for eight has the peers and lifecycles elsewhere and eight slots, so both are asked
 * of the mover each time (the managers move back when a state is built somewhere else). */
int co_evp_players(uintptr_t game, EvpPlayer* out)
{
    /* The offsets are looked up once for each state of the mover, not on every read. */
    static struct { volatile LONG moved; uint32_t slots, count, peers, lives, descriptors, networks; } layout = {-1};
    uintptr_t manager, user, em = 0;
    uint64_t local = 0;
    const LONG moved = co_managers_moved();
    if (layout.moved != moved)
    {
        layout.slots = co_managers_slots(CO_SHIM_PLAYER_MANAGER);
        layout.count = co_managers_offset(CO_SHIM_PLAYER_MANAGER, EVP_PLAYER_COUNT);
        layout.peers = co_managers_offset(CO_SHIM_PLAYER_MANAGER, EVP_PLAYER_PEERS);
        layout.lives = co_managers_offset(CO_SHIM_PLAYER_MANAGER, EVP_PLAYER_LIFE);
        layout.descriptors = co_managers_offset(CO_SHIM_PLAYER_MANAGER, EVP_PLAYER_DESCRIPTORS);
        layout.networks = co_managers_offset(CO_SHIM_PLAYER_MANAGER, EVP_PLAYER_NETWORK);
        layout.moved = moved;
    }
    const uint32_t slots = layout.slots, peers = layout.peers, lives = layout.lives, descriptors = layout.descriptors, networks = layout.networks;
    if (!co_evp_ptr(game + CO_SHIM_PLAYER_MANAGER, &manager)) return -1;
    if (co_evp_ptr(game + EVT_LOCAL_PEER, &user)) SafeRead(user + EVT_LOCAL_PEER_AT, &local, 8);
    uint32_t count = co_evp_u32(manager + layout.count, 99);
    if (count > slots || count > CO_SHIM_MAX_PLAYERS) return -1;
    co_evp_ptr(game + EVP_ENTITIES, &em);
    for (uint32_t i = 0; i < count; i++)
    {
        EvpPlayer* p = &out[i];
        memset(p, 0, sizeof *p);
        uintptr_t descriptor;
        SafeRead(manager + peers + 0x38 * i, &p->peer, 8);
        p->life = co_evp_u32(manager + lives + 0x38 * i, 0);
        if (co_evp_ptr(manager + descriptors + 8 * i, &descriptor)) p->player = co_evp_u32(descriptor + 8, 0);
        p->local = p->peer != 0 && p->peer == local;
        uint32_t network = co_evp_u32(manager + networks + 0x20 * i, 0x7FFF), slot;
        if (em && network != 0x7FFF && co_evp_hash(em + EVP_NETWORK_MAP, network, 8, 4, &slot))
            p->avatar = co_evp_u32(em + EVP_NETWORK_ENTITY + (uintptr_t)slot * 24, 0);
    }
    return (int)count;
}

/* A Helldiver's health record: its address, health, life and maximum (-1 unknown). */
int co_evp_health_of(uintptr_t game, uint32_t entity, uintptr_t* record, int32_t* health, uint32_t* life, int32_t* max)
{
    uintptr_t manager, records, ext;
    uint32_t index;
    if (!co_evp_ptr(game + EVP_HEALTH, &manager) || !co_evp_hash(manager + 0x1030, entity, 8, 4, &index) || index >= co_evp_u32(manager + 0x1020, 0) ||
        !co_evp_ptr(manager + 0x1058, &records)) return 0;
    *record = records + (uintptr_t)index * 0x1B8;
    if (SafeRead(*record + 0x14, health, 4) || SafeRead(*record + 0x19C, life, 4)) return 0;
    *max = -1;
    if (co_evp_ptr(manager + 0x1060, &ext)) SafeRead(ext + (uintptr_t)index * 0x1C + 0x14, max, 4);
    return 1;
}

/* A unit's scene graph (helldivers2.exe's unit registry: object+0x48 for the level unit class, +0x60 for the plain one,
 * as their vt+0xE8 returns it); 0 when it does not read. */
uintptr_t co_evp_unit_graph(uint32_t unit)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), registry, gens, objects, object, vtable, method;
    uint32_t index = unit & 0x3FFFFFu, count;
    uint8_t gen, code[5];
    if (!unit || !co_evp_ptr(exe + EVP_EXE_UNITS, &registry) || SafeRead(registry + 0x98, &count, 4) || index >= count ||
        !co_evp_ptr(registry + 0xA0, &gens) || SafeRead(gens + index, &gen, 1) || gen != ((unit >> 22) & 0xFF) ||
        !co_evp_ptr(registry + 0x88, &objects) || !co_evp_ptr(objects + (uintptr_t)index * 8, &object) || co_evp_u32(object + 8, 0) != unit ||
        !co_evp_ptr(object, &vtable) || !co_evp_ptr(vtable + 0xE8, &method) || SafeRead(method, code, 5)) return 0;
    static const uint8_t level[5] = {0x48, 0x8d, 0x41, 0x48, 0xc3}, plain[5] = {0x48, 0x8d, 0x41, 0x60, 0xc3};
    uintptr_t graph;
    if (vtable == exe + 0x16770E0u && method == exe + 0x2CAC50u && !memcmp(code, level, 5)) graph = object + 0x48;
    else if (method == exe + 0x2BD870u && !memcmp(code, plain, 5)) graph = object + 0x60;
    else return 0;
    return graph;
}

/* A unit's position (its scene graph's root); 0 when it does not read. */
int co_evp_unit_position(uint32_t unit, float* position)
{
    uintptr_t graph = co_evp_unit_graph(unit), poses;
    if (!graph || !co_evp_ptr(graph + 0x28, &poses) || SafeRead(poses + 0x30, position, 12)) return 0;
    for (int k = 0; k < 3; k++) if (!isfinite(position[k]) || fabsf(position[k]) > 100000.0f) return 0;
    return 1;
}

static int Hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Whether code or data reads as `hex`. The loader passed a range it had rewritten itself; a mod cannot know those, so
 * a pin on bytes the host changed fails here. */
static int ActBytes(uintptr_t at, const char* hex)
{
    uint8_t want[256], got[256];
    size_t n = strlen(hex) / 2;
    if (n > sizeof want) return 0;
    for (size_t i = 0; i < n; i++) want[i] = (uint8_t)(Hex(hex[2 * i]) << 4 | Hex(hex[2 * i + 1]));
    return !SafeRead(at, got, n) && !memcmp(got, want, n);
}

int co_act_pins(uintptr_t game, const ActPin* pins, size_t count)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    for (size_t i = 0; i < count; i++)
        if (!ActBytes((pins[i].exe ? exe : game) + pins[i].rva, pins[i].hex)) return 0;
    return 1;
}

static uint32_t ReadState(uintptr_t game)
{
    uintptr_t g = 0;
    uint32_t state = 0;
    if (!game || SafeRead(game + EVT_STATE_GAME, &g, 8) || g == 0 || SafeRead(g + EVT_STATE_AT, &state, 4) || state > 8) return HF_STATE_NONE;
    return state;
}

/* In a mission: whether this machine hosts it, and the game mode's entity. */
int co_act_mission(uintptr_t game, int* host, uint32_t* modeEntity)
{
    uintptr_t mode, d;
    if (ReadState(game) != HF_STATE_MISSION || !co_evp_ptr(game + 0x33266A0u, &mode) || co_evp_u32(mode + 8, 0) != 1 || !co_evp_ptr(mode + 0x38, &d)) return 0;
    if (host) *host = co_evp_u32(d + 0x14, 0) & 1;
    if (modeEntity) *modeEntity = co_evp_u32(d + 8, 0);
    return 1;
}

/* An entity's health record and its descriptor (the record's own). */
int co_act_health_of(uintptr_t game, uint32_t entity, ActHealth* h)
{
    memset(h, 0, sizeof *h);
    uintptr_t descriptors, descriptor, records, ext;
    uint8_t d[24];
    if (!co_evp_ptr(game + EVP_HEALTH, &h->manager) || !co_evp_hash(h->manager + 0x1030, entity, 8, 4, &h->index) ||
        h->index >= co_evp_u32(h->manager + 0x1020, 0) || !co_evp_ptr(h->manager + 0x1048, &descriptors) ||
        !co_evp_ptr(descriptors + (uintptr_t)h->index * 8, &descriptor) || SafeRead(descriptor, d, sizeof d) || !co_evp_ptr(h->manager + 0x1058, &records)) return 0;
    uint32_t named; memcpy(&named, d + 8, 4);
    if (named != entity) return 0;
    memcpy(&h->resource, d, 8); memcpy(&h->unit, d + 12, 4); memcpy(&h->network, d + 16, 4); h->owned = d[20] & 1;
    h->record = records + (uintptr_t)h->index * 0x1B8;
    if (SafeRead(h->record + 0x14, &h->health, 4) || SafeRead(h->record + 0x19C, &h->life, 4)) return 0;
    h->max = -1;
    if (co_evp_ptr(h->manager + 0x1060, &ext)) SafeRead(ext + (uintptr_t)h->index * 0x1C + 0x14, &h->max, 4);
    return 1;
}

/* This machine's player's Helldiver: owned, alive (or downed or dead with `dead`). 0 with *why. */
uint32_t co_act_local_avatar(uintptr_t game, int dead, ActHealth* h, const char** why)
{
    EvpPlayer players[CO_SHIM_MAX_PLAYERS];
    int count = co_evp_players(game, players);
    uint32_t avatar = 0;
    for (int i = 0; i < count; i++) if (players[i].local) avatar = players[i].avatar;
    *why = "this machine's player has no Helldiver";
    if (!avatar || !co_evp_alive(avatar) || !co_act_health_of(game, avatar, h)) return 0;
    if (h->resource != ACT_AVATAR || !h->owned) { *why = "this machine's player's avatar is not a Helldiver it owns"; return 0; }
    if (!dead && h->life == 1) { *why = "the Helldiver is downed"; return 0; }
    if (!dead && h->life >= 2) { *why = "the Helldiver is dead"; return 0; }
    return avatar;
}

/* The enemy types, sorted. */
static const uint64_t kActEnemies[] = {
    0x0002BA767DF856F3ull, 0x08E6FFC2474287BDull, 0x09FE0BE51A23396Cull, 0x0AB7B92B131C228Cull, 0x0ADC9F9173AD8E1Dull, 0x10081ACEF6163EF6ull,
    0x137988CEA16458F7ull, 0x16023E554FEE830Eull, 0x1897BDD32105D2DCull, 0x19E18B46EC55D94Aull, 0x1A7FCDFF98C664B0ull, 0x1E66EE1F6F7FD00Eull,
    0x1F0A91729C0004E0ull, 0x20B9C7734DAEAD65ull, 0x215CE160A17BE4CDull, 0x257D805CAA7E10C0ull, 0x282EB766C1FFA6A1ull, 0x2CF3488C4845F8BDull,
    0x30CF04B2EC8C9BD4ull, 0x30F2DEE2333F227Aull, 0x31BAE74D2F064D8Dull, 0x32541FC4EC7C9CDCull, 0x33B3E0CE8D179EECull, 0x36AA99CCE5E60146ull,
    0x3AFF5FD7D5450B99ull, 0x3D0E03E2D574E1CAull, 0x3E0537D606438FEAull, 0x4019623142351CB6ull, 0x426B3DF007BE9F18ull, 0x44458A2C52B002FBull,
    0x453FE22C634EB30Full, 0x49674794A165EDA8ull, 0x4E342031A9D5C4F9ull, 0x4E97FB073BDC7A4Bull, 0x51EEA86BF6997E4Eull, 0x53D8919D7B8ABD67ull,
    0x54E107DACF6929CBull, 0x57EED0EAC346CD9Dull, 0x58B2B86C11369241ull, 0x5CA832447445C0BAull, 0x5D142C3A73EBC634ull, 0x6021E22338333D88ull,
    0x604A794EC45BB820ull, 0x63DF3D07B7424588ull, 0x64090088502435DDull, 0x64BA5F030B114EC1ull, 0x672F7DA17F3BA34Aull, 0x67DC32DCA4F02D33ull,
    0x6B202392F4AB605Eull, 0x6DAB2EADF5D8B692ull, 0x728421351D440EBCull, 0x72A83E49CED6DB3Dull, 0x746A7F3BEDA32699ull, 0x74E2285C01DA4F71ull,
    0x78E1497571012C47ull, 0x7ACB4BD753B71DE0ull, 0x7B48CACDBACB3881ull, 0x7ECE5304F868F6B3ull, 0x82A87AD8D595B2BAull, 0x843D18D4B5512B63ull,
    0x856E9710E45E760Full, 0x883401AF2A98A5F6ull, 0x8FB47DD28B398CF7ull, 0x8FC2784308E9BA99ull, 0x8FF0A839830A7692ull, 0x905809A4C28D8A45ull,
    0x960B48A421A3FAAAull, 0x9647B00CC3A9D36Full, 0x965EAE5A51ACDD4Aull, 0x96BA14C9EBB49CE1ull, 0x9A8A3AAE287B230Cull, 0x9D8827FED763650Eull,
    0x9E2E17F2CCCCAFDDull, 0x9F57782F00E6ED20ull, 0xA05BD1EC67B3AC4Cull, 0xA1F37BF2A40FBDE4ull, 0xA35207C6F2150806ull, 0xA381A11C07D3EB94ull,
    0xA6A68D8AF177F3A1ull, 0xA71AAFD82C6EBC92ull, 0xAAB438596F5E8FD9ull, 0xABDB2E2A0479D8CAull, 0xAC60E78435098C9Dull, 0xAE63E525853D7044ull,
    0xAEAEF7A1851E6C9Dull, 0xAF0F9B3A163787A5ull, 0xAF8529D541954E88ull, 0xB2A6FA1E4284C7E6ull, 0xB5DBC0C240C921ADull, 0xB8580CAC32214FC0ull,
    0xB92435FBF60F0748ull, 0xB940C889D77F8219ull, 0xBC242702FB46B7E7ull, 0xBE39E313A1E46BB9ull, 0xBE743B2FAA3A6E26ull, 0xC0DF2A134BD15AD5ull,
    0xC2B9EB21B06F8378ull, 0xC626D2BB495A202Dull, 0xC6449FFD9EA3779Cull, 0xC9BCCCB0A54A82A4ull, 0xCBB1BA3366009C3Aull, 0xCC188F0C80505C6Cull,
    0xCC7022FDD172089Bull, 0xCCAE5264ACD591B7ull, 0xCD28A27A79BE53D5ull, 0xD37E8D120D2836E3ull, 0xD465D9C7F77A07CBull, 0xD522FD4748D443A5ull,
    0xD5792F6856B06BA4ull, 0xD63FCBFF0851B7AFull, 0xD8CBC4A807A6D035ull, 0xD9511E9F6BD62E3Full, 0xDA40BB347C7447F2ull, 0xDAC7D53FE0749F5Full,
    0xDB90077E76FAA025ull, 0xDB964631BE1CF501ull, 0xDCF8E74212FBEE3Bull, 0xDFBACBD977A948DCull, 0xE0353177F1329573ull, 0xE2E62FDD39407FE3ull,
    0xE683D2CA5618D74Aull, 0xE8F19A0AA958E46Dull, 0xEACEE39FA017B495ull, 0xEF04CB84D097A497ull, 0xEF570293245A17C2ull, 0xF0B26FA9258128D3ull,
    0xF1610AC48CDC5240ull, 0xF196069E7E14C497ull, 0xF22D027B37BEF107ull, 0xF540CA9D9D4A422Eull, 0xF66D0BAD8693779Aull, 0xF79CD8BB654397DFull,
    0xF8131632AA867107ull, 0xF8B5A81A86D5D4EBull, 0xF96257FCAE258CAAull, 0xFB9937035D652C43ull, 0xFEB484EE972469F7ull, 0xFF5CC825B9571052ull,
};
int co_act_enemy(uint64_t resource)
{
    size_t lo = 0, hi = sizeof kActEnemies / sizeof kActEnemies[0];
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (kActEnemies[mid] < resource) lo = mid + 1; else hi = mid; }
    return lo < sizeof kActEnemies / sizeof kActEnemies[0] && kActEnemies[lo] == resource;
}

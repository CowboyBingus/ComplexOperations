/* The extra large world's prototype was written inside HellForge's loader and calls the loader's own helpers by name.
 * This is those helpers for a mod: over the SDK's HfApi and the mod's own parts (co_shim.c). The loader's names
 * themselves exist only in the unit that defines CO_XL_UNIT (co_xl.c), so no other file of the mod sees them. */
#ifndef CO_SHIM_H
#define CO_SHIM_H

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "co.h"

/* The prototype's research switches (HELLFORGE_*) change what one machine generates, and every peer of a mission must
 * generate alike: a build reads none of them, each as if unset, unless it is a research build (scripts/build.sh
 * --research, which no package is made from). Every environment read of the copied files is such a switch. */
#ifndef CO_RESEARCH
static inline DWORD CoNoSwitch(char* buffer, DWORD size) { if (buffer != NULL && size != 0) buffer[0] = 0; return 0; }
#define GetEnvironmentVariableA(name, buffer, size) CoNoSwitch((buffer), (DWORD)(size))
#endif

/* Per-player arrays of the copied code: the most slots any build's player manager has. The count that is read is
 * checked against the manager's slots now (co_managers_slots), four or eight. */
#define CO_SHIM_MAX_PLAYERS 8
#define CO_SHIM_PLAYER_MANAGER 0x3326468u

/* Once, before any read that should be cheap: the fault handler small reads need (proto/safe_read.c). Without it every
 * read is the system call. */
void co_shim_install(void);

/* A line in the mod's log, up to the loader's 1024 bytes; `diagnostic` only with the engine's own log on. */
void co_note(const char* format, ...);
void co_note_diagnostic(const char* format, ...);
int co_diagnostic_log(void);
/* The session's log folder (L"" when the host gives none) and a file in it, opened for append. */
const wchar_t* co_shim_logs(void);
HANDLE co_open_log(const wchar_t* name);
const char* co_shim_build(void);

int co_safe_read(uintptr_t address, void* buffer, size_t size);
int co_safe_find(uintptr_t list, size_t count, uint64_t value); /* 1 found, 0 absent, -1 unreadable */
/* Zeroed read-write pages within reach of a 32-bit offset from all of game.dll; NULL when there are none. `find` with
 * MEM_RESERVE alone only takes the address space. */
uint8_t* co_find_near_game(uintptr_t game, size_t bytes, DWORD type);
uint8_t* co_alloc_near_game(uintptr_t game, size_t bytes);
int co_loader_hook(const char* what, uintptr_t target, void* detour, void** original);
uint64_t co_murmur64a(const void* key, size_t length);
int co_module_at(uintptr_t at, char* out, size_t size);

/* The game's data as the loader's event and action code reads it (HellForge's event_polls.c and actions.c). */
typedef struct EvpPlayer { uint64_t peer; uint32_t player, avatar, life; int local; } EvpPlayer;
typedef struct ActPin { int exe; uint32_t rva; const char* hex; } ActPin;
typedef struct ActHealth { uintptr_t manager, record; uint32_t index, unit, network, owned, life; uint64_t resource; int32_t health, max; } ActHealth;

/* Where the entity records are, as a distance from the entity state: the game's place until a mover names its own. */
void co_shim_entity_records(const int64_t* displacement);

int co_evp_ptr(uintptr_t at, uintptr_t* out);
uint32_t co_evp_u32(uintptr_t at, uint32_t fallback);
int co_evp_hash(uintptr_t header, uint32_t key, uint32_t stride, uint32_t valueAt, uint32_t* value);
uint64_t co_evp_entity_resource(uintptr_t game, uint32_t entity, uint32_t* unit);
int co_evp_alive(uint32_t entity);
/* The player list into `out` (CO_SHIM_MAX_PLAYERS rows): the count, -1 unreadable or more than the manager's slots. */
int co_evp_players(uintptr_t game, EvpPlayer* out);
int co_evp_health_of(uintptr_t game, uint32_t entity, uintptr_t* record, int32_t* health, uint32_t* life, int32_t* max);
uintptr_t co_evp_unit_graph(uint32_t unit);
int co_evp_unit_position(uint32_t unit, float* position);
int co_act_pins(uintptr_t game, const ActPin* pins, size_t count);
/* `pattern` (hex bytes, spaces allowed) at exe address `at`: the host's check, or else the bytes read before any hook
 * with the tile stubs' jumps read as the engine's code they lead to, as co_act_pins reads its exe pins. */
int co_exe_code_matches(uintptr_t at, const char* pattern);
/* A call or jump this mod wrote over engine code that another of its parts pins: `engine` the `n` bytes (at most 8)
 * it replaced, `wrote` what stands there now. While the site still reads as `wrote`, co_exe_code_matches reads it as
 * `engine`; any other change there still fails. 0 when it is recorded. */
int co_exe_own_write(uintptr_t at, const uint8_t* engine, const uint8_t* wrote, size_t n);
#ifdef CO_TEST
void co_exe_own_reset(void);
#endif
int co_act_mission(uintptr_t game, int* host, uint32_t* modeEntity);
int co_act_health_of(uintptr_t game, uint32_t entity, ActHealth* h);
uint32_t co_act_local_avatar(uintptr_t game, int dead, ActHealth* h, const char** why);
int co_act_enemy(uint64_t resource);
/* The game's state (HF_STATE_*), as the loader's event code reads it; HF_STATE_NONE when it cannot be read. */
uint32_t co_read_state(uintptr_t game);

#ifdef CO_XL_UNIT
/* The loader's names. Static, as the loader has them, so the prototype's own forward declarations still fit. */
#define EVT_MAX_PLAYERS CO_SHIM_MAX_PLAYERS
#define EVP_EXE_UNITS 0x1A100F0u
#define Note co_note
#define NoteDiagnostic co_note_diagnostic
#define g_build (co_shim_build())
#define g_logs (co_shim_logs())

static inline int DiagnosticLog(void) { return co_diagnostic_log(); }
/* Every mod the loader's code logs for is this one. */
static inline void ApiLog(HfMod* mod, const char* format, ...)
{
    char text[1024];
    va_list arguments;
    (void)mod;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    co_note("%s", text);
}
static inline HANDLE OpenLog(const wchar_t* name) { return co_open_log(name); }
static inline int SafeRead(uintptr_t address, void* buffer, size_t size) { return co_safe_read(address, buffer, size); }
static inline int SafeFind(uintptr_t list, size_t count, uint64_t value) { return co_safe_find(list, count, value); }
static inline int ApiWrite(uintptr_t address, const void* data, size_t size) { return g_coApi->write(address, data, size); }
static inline int ApiCodeMatches(uintptr_t address, const char* pattern) { return g_coApi->code_matches(address, pattern); }
static inline int ExeCodeMatches(uintptr_t address, const char* pattern) { return co_exe_code_matches(address, pattern); }
static inline uintptr_t ApiModuleBase(const char* module, size_t* size) { return g_coApi->module_base(module, size); }
static inline int LoaderHook(const char* what, uintptr_t target, void* detour, void** original) { return co_loader_hook(what, target, detour, original); }
static inline uint8_t* FindNearGame(uintptr_t game, size_t bytes, DWORD type) { return co_find_near_game(game, bytes, type); }
static inline uint8_t* AllocateNearGame(uintptr_t game, size_t bytes) { return co_alloc_near_game(game, bytes); }
static inline uint64_t Murmur64A(const void* key, size_t length) { return co_murmur64a(key, length); }
static inline int ModuleAt(uintptr_t at, char* out, size_t size) { return co_module_at(at, out, size); }
/* The stock level table's row; ready once the host has one. */
static inline uintptr_t DifficultyRowOf(uint64_t id, int* ready)
{
    uintptr_t row = id <= 11 ? g_coApi->difficulty_row((uint32_t)id) : 0;
    *ready = row != 0;
    return row;
}
/* The host heals this machine's Helldiver itself; the loader's return values. */
static inline int ActHealNow(HfMod* mod, uintptr_t game, float amount) { (void)mod; (void)game; return g_coApi->heal(g_coMod, amount); }

static inline int EvpPtr(uintptr_t at, uintptr_t* out) { return co_evp_ptr(at, out); }
static inline uint32_t EvpU32(uintptr_t at, uint32_t fallback) { return co_evp_u32(at, fallback); }
static inline int EvpHash(uintptr_t header, uint32_t key, uint32_t stride, uint32_t valueAt, uint32_t* value) { return co_evp_hash(header, key, stride, valueAt, value); }
static inline uint64_t EvpEntityResource(uintptr_t game, uint32_t entity, uint32_t* unit) { return co_evp_entity_resource(game, entity, unit); }
static inline int EvpAlive(uint32_t entity) { return co_evp_alive(entity); }
static inline int EvpPlayers(uintptr_t game, EvpPlayer* out) { return co_evp_players(game, out); }
static inline int EvpHealthOf(uintptr_t game, uint32_t entity, uintptr_t* record, int32_t* health, uint32_t* life, int32_t* max) { return co_evp_health_of(game, entity, record, health, life, max); }
static inline uintptr_t EvpUnitGraph(uint32_t unit) { return co_evp_unit_graph(unit); }
static inline int EvpUnitPosition(uint32_t unit, float* position) { return co_evp_unit_position(unit, position); }
static inline int ActPins(uintptr_t game, const ActPin* pins, size_t count) { return co_act_pins(game, pins, count); }
static inline int ActMission(uintptr_t game, int* host, uint32_t* modeEntity) { return co_act_mission(game, host, modeEntity); }
static inline int ActHealthOf(uintptr_t game, uint32_t entity, ActHealth* h) { return co_act_health_of(game, entity, h); }
static inline uint32_t ActLocalAvatar(uintptr_t game, int dead, ActHealth* h, const char** why) { return co_act_local_avatar(game, dead, h, why); }
static inline int ActEnemy(uint64_t resource) { return co_act_enemy(resource); }
static inline uint32_t ReadState(uintptr_t game) { return co_read_state(game); }
#define EVP_HEALTH 0x3326688u
#define ACT_AVATAR 0x4D1C334D294DFA97ull

/* The prototype's own code writes, one at a time. The host's hook installs take their own lock; these writes change
 * the exe and ntdll in the middle of functions no hook starts at. */
static SRWLOCK g_hookLock = SRWLOCK_INIT;
/* The loader's records of its rewrites: the rewrite list covers game.dll only and the patch audit only the host's
 * difficulty install, and the prototype's switched bytes are in neither. */
static inline void NoteRewrite(uintptr_t address, size_t size) { (void)address; (void)size; }
static inline void RecordPatch(uintptr_t address, const void* data, size_t size) { (void)address; (void)data; (void)size; }
/* The host does not say where in a chain a hook is: the research instruments that need the first place stay off. */
typedef struct HookTarget HookTarget;
static inline HookTarget* HookTargetOf(uintptr_t target) { (void)target; return NULL; }
static inline int HookPlace(const HookTarget* h, void* detour) { (void)h; (void)detour; return 0; }
/* An IDA-style pattern's bytes and which count (0 for ?? ); the count, or 0 when malformed. */
static inline size_t ParsePattern(const char* pattern, uint8_t bytes[256], uint8_t mask[256])
{
    size_t count = 0;
    for (const char* c = pattern; c != NULL && *c && count < 256;)
    {
        if (*c == ' ') { c++; continue; }
        if (c[0] == '?') { mask[count] = 0; bytes[count++] = 0; c += c[1] == '?' ? 2 : 1; continue; }
        int v[2];
        for (int k = 0; k < 2; k++)
        {
            char h = c[k];
            v[k] = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
            if (v[k] < 0) return 0;
        }
        mask[count] = 1; bytes[count++] = (uint8_t)(v[0] << 4 | v[1]); c += 2;
    }
    return count;
}
#endif

#endif

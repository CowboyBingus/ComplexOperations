/* The Helldiver's voice lines, logged in every build: a line was silent and the values on its way could not be read
 * from outside. Three places, each with what the game itself reads there:
 *   the spawn's voice delta (0x878260): the applied voice at customization + (record + k) * 0x44, k as the code holds it
 *   now (0x24 in the game, 0x30 once the applied records have moved), and the delta list it hands the spawn;
 *   a line's event name (0x139ECA0): the speaker's CharacterVoiceComponent value, the name "<prefix>_<line>_vNN" and its
 *   Wwise event id (0 is the game's "vo_event_not_found": the line is dropped before it is queued);
 *   the post (0x13A1CD0): the event id and the playing id it gave back (0 posted nothing, or was deferred).
 * Every spawn, the first lines and each new value, at most CO_VOICE_LINES lines a session, so it costs a few probes a voice line. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "co.h"

#define CO_VOICE_DELTA 0x878260u   /* (unused, u32 player entity): the avatar spawn's delta list, from the applied voice */
static const char kVoiceDeltaPin[] = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 90 00 00 00";
#define CO_VOICE_READ 0x878329u    /* add rax,k ; imul rax,rax,0x44 ; mov eax,[rax+r11]: k is its fourth byte */
static const char kVoiceReadPin[] = "48 83 C0 ?? 48 89 4C 24 28 48 6B C0 44";
#define CO_VOICE_NAME 0x139ECA0u   /* (system, u32 entity, u32 line, u32 variant, char[256]): bool, the event's name */
static const char kVoiceNamePin[] = "48 89 5C 24 20 57 41 54 41 57 48 83 EC 30 45 8B F9 41 8B F8 8B DA";
#define CO_VOICE_POST 0x13A1CD0u   /* (system, u32 name hash, u32 line, u64* source, unit, entity, data, callback) */
static const char kVoicePostPin[] = "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57";
#define CO_NAME_HASH 0xAAFF30u     /* u64 (const char*, int length, u32 seed): the name's hash is the upper half */
static const char kNameHashPin[] = "48 89 5C 24 08 4C 63 DA 4C 8B D1 41 8B C3 4D 8B CB";
#define CO_EVENT_OF 0x12556E0u     /* u32 (u32 name hash): the Wwise event id; 0 for none */
static const char kEventOfPin[] = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 8B 05";
#define CO_VOICE_SYSTEM 0x346D520u
#define CO_CUSTOMIZATION 0x33264F8u /* records by entity: data +0x930, size +0x938, empty key +0x93C, multiplier +0x940 */
#define CO_VOICE_MANAGER 0x3326C30u /* by entity: data +0x80, size +0x88, empty key +0x8C, multiplier +0x90; count +0x130 */
#define CO_VOICE_VALUES 0x134u
#define CO_VOICE_ROOM 8u           /* values the manager has room for; its add checks no count */
#define CO_VOICE_ROWS 0x32F9518u   /* the four voices, 0x38 each, the id first */
#define CO_VOICE_PREFIXES 0x21CB530u /* char* per component value 1..9 */
#define CO_VOICE_LINES 200u

typedef uint64_t (*DeltaFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*NameFn)(uint64_t, uint64_t, uint64_t, uint64_t, char*, uint64_t);
typedef uint64_t (*PostFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*HashFn)(const char*, int, uint32_t);
typedef uint32_t (*EventFn)(uint32_t);
static DeltaFn g_delta;
static NameFn g_name;
static PostFn g_post;
static HashFn g_hash;   /* the game's two lookups; a test gives its own */
static EventFn g_event;
static uintptr_t g_game;
static unsigned g_lines, g_names, g_posts;
static uint32_t g_lastValue = 0xFFFFFFFEu, g_lastCount = 0xFFFFFFFEu, g_lastEvent = 0xFFFFFFFEu;

/* The game's open-addressing probe: {u32 key, u32 value} pairs. 0xFFFFFFFF when the key is not there. */
static uint32_t Find(const uint8_t* map, uint32_t key)
{
    const uint32_t* data = *(const uint32_t* const*)map;
    const uint32_t size = *(const uint32_t*)(map + 8), empty = *(const uint32_t*)(map + 12), mult = *(const uint32_t*)(map + 16);
    if (data == NULL) return 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++)
    {
        const uint32_t at = (i + key * mult) & (size - 1u);
        if (data[2 * at] == key) return data[2 * at + 1];
        if (data[2 * at] == empty) break;
    }
    return 0xFFFFFFFFu;
}

static void Line(const char* format, ...);

/* What 0x878260 read for `entity` and what it gave the spawn (`list`: u64 count, then the ids; 0 for none). */
void co_voice_delta(uint32_t entity, uintptr_t list)
{
    const uint8_t* cm = *(const uint8_t* const*)(g_game + CO_CUSTOMIZATION);
    const uint8_t k = *(const uint8_t*)(g_game + CO_VOICE_READ + 3);
    uint32_t record = 0xFFFFFFFFu, voice = 0xFFFFFFFFu, count = 0;
    uint64_t first = 0;
    int row = -1;
    if (cm != NULL) record = Find(cm + 0x930, entity);
    if (record != 0xFFFFFFFFu) voice = *(const uint32_t*)(cm + ((uint64_t)record + k) * 0x44u);
    for (int i = 0; i < 4; i++)
        if (voice != 0xFFFFFFFFu && *(const uint32_t*)(g_game + CO_VOICE_ROWS + 0x38u * i) == voice) row = i + 1;
    if (list != 0) { count = *(const uint32_t*)list; first = count != 0 ? *(const uint64_t*)(list + 8) : 0; }
    Line("voice: spawn of entity %u: record %d, applied voice 0x%08X (voice %d of 4; 0 none) read at +0x%X; %u delta(s), the first 0x%016llX",
         entity, (int)record, voice, row < 0 ? 0 : row, (unsigned)(k * 0x44u), count, (unsigned long long)first);
}

/* A line's name as the game built it for `entity`, with its component value and the manager's count. */
void co_voice_name(uint32_t entity, const char* name, int built)
{
    const uint8_t* vm = *(const uint8_t* const*)(g_game + CO_VOICE_MANAGER);
    uint32_t slot = 0xFFFFFFFFu, value = 0xFFFFFFFFu, count = 0xFFFFFFFFu;
    const char* prefix = "-";
    if (vm != NULL)
    {
        count = *(const uint32_t*)(vm + 0x130);
        slot = Find(vm + 0x80, entity);
        if (slot != 0xFFFFFFFFu) value = *(const uint32_t*)(vm + CO_VOICE_VALUES + 4u * (uint64_t)slot);
    }
    if (value - 1u < 9u)
    {
        const char* p = *(const char* const*)(g_game + CO_VOICE_PREFIXES + 8u * (value - 1u));
        if (p != NULL) prefix = p;
    }
    uint32_t event = 0;
    if (built)
    {
        const uint32_t hash = (uint32_t)(g_hash(name, (int)strlen(name), 0) >> 32);
        event = g_event(hash);
    }
    g_names++;
    if (value == g_lastValue && count == g_lastCount && (event != 0) == (g_lastEvent != 0) && g_names > 24 && g_names % 50 != 0) return;
    g_lastValue = value;
    g_lastCount = count;
    g_lastEvent = event;
    Line("voice: line %u for entity %u: component slot %d value %d (%s), %u voiced entities (room for %u); name \"%.96s\"%s, event 0x%08X%s",
         g_names, entity, (int)slot, (int)value, prefix, count, CO_VOICE_ROOM, built ? name : "", built ? "" : " not built", event,
         event == 0 ? " (none: the game drops the line)" : "");
}

/* A post as 0x13A1CD0 made it: the event of `hash`, the speaker, and the playing id it gave back. */
void co_voice_post(uint64_t system, uint32_t hash, uint32_t line, uint32_t unit, uint32_t entity, uint32_t playing)
{
    const uint32_t event = g_event(hash);
    g_posts++;
    if (playing != 0 && event != 0 && g_posts > 24 && g_posts % 50 != 0) return;
    Line("voice: post %u%s: entity %u unit %u, line 0x%08X, name hash 0x%08X, event 0x%08X; playing id %u%s", g_posts,
         system == *(const uint64_t*)(g_game + CO_VOICE_SYSTEM) ? "" : " (another system)", entity, unit, line, hash, event, playing,
         playing == 0 ? " (nothing posted, or deferred to the next update)" : "");
}

static uint64_t DeltaDetour(uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9)
{
    const uint64_t list = g_delta(rcx, rdx, r8, r9);
    co_voice_delta((uint32_t)rdx, (uintptr_t)list);
    return list;
}

static uint64_t NameDetour(uint64_t system, uint64_t entity, uint64_t line, uint64_t variant, char* name, uint64_t s6)
{
    const uint64_t built = g_name(system, entity, line, variant, name, s6);
    co_voice_name((uint32_t)entity, name, (uint8_t)built != 0 && name != NULL);
    return built;
}

static uint64_t PostDetour(uint64_t system, uint64_t hash, uint64_t line, uint64_t source, uint64_t unit, uint64_t entity,
    uint64_t data, uint64_t callback, uint64_t s9, uint64_t s10)
{
    const uint64_t playing = g_post(system, hash, line, source, unit, entity, data, callback, s9, s10);
    co_voice_post(system, (uint32_t)hash, (uint32_t)line, (uint32_t)unit, (uint32_t)entity, (uint32_t)playing);
    return playing;
}

static void Line(const char* format, ...)
{
    char text[400];
    va_list args;
    if (g_lines >= CO_VOICE_LINES) return;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (++g_lines == CO_VOICE_LINES) co_log("%s (the last voice line of this session)", text);
    else co_log("%s", text);
}

void co_voice_install(uintptr_t game)
{
    g_game = game;
    g_hash = (HashFn)(game + CO_NAME_HASH);
    g_event = (EventFn)(game + CO_EVENT_OF);
    if (g_coApi->code_matches(game + CO_VOICE_DELTA, kVoiceDeltaPin) != 1 || g_coApi->code_matches(game + CO_VOICE_READ, kVoiceReadPin) != 1 ||
        g_coApi->code_matches(game + CO_VOICE_NAME, kVoiceNamePin) != 1 || g_coApi->code_matches(game + CO_VOICE_POST, kVoicePostPin) != 1 ||
        g_coApi->code_matches(game + CO_NAME_HASH, kNameHashPin) != 1 || g_coApi->code_matches(game + CO_EVENT_OF, kEventOfPin) != 1)
    { co_log("voice: the voice code is not the one the mod was made for; voice lines are not logged"); return; }
    if (g_coApi->hook(g_coMod, game + CO_VOICE_DELTA, (void*)DeltaDetour, (void**)&g_delta) != 0 ||
        g_coApi->hook(g_coMod, game + CO_VOICE_NAME, (void*)NameDetour, (void**)&g_name) != 0 ||
        g_coApi->hook(g_coMod, game + CO_VOICE_POST, (void*)PostDetour, (void**)&g_post) != 0)
    { co_log("voice: a hook could not be placed; the voice lines' log is incomplete"); return; }
    co_log("voice: logging the spawn's voice delta, each line's name and each post (at most %u lines)", CO_VOICE_LINES);
}

#ifdef CO_TEST
void co_voice_test_reset(void)
{
    g_game = 0; g_lines = g_names = g_posts = 0;
    g_lastValue = g_lastCount = g_lastEvent = 0xFFFFFFFEu;
    g_delta = NULL; g_name = NULL; g_post = NULL;
}

void co_voice_test_lookups(uint64_t (*hash)(const char*, int, uint32_t), uint32_t (*event)(uint32_t)) { g_hash = hash; g_event = event; }
#endif

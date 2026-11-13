/* The Helldiver's voice lines, logged in every build: a line was silent and the values on its way could not be read
 * from outside. Four places, each with what the game itself reads there:
 *   the spawn's voice delta (0x878260): the applied voice at customization + (record + k) * 0x44, k as the code holds it
 *   now (0x24 in the game, 0x30 once the applied records have moved), and the delta list it hands the spawn;
 *   a line's request (0x13A0090): its line-def's state mask against the voice system's state, and whether a line was
 *   queued; this is where the game refuses a line without a word, the solo rule among them;
 *   a line's event name (0x139ECA0): the speaker's component value, the name and its Wwise event id;
 *   the post (0x13A1CD0): the event id and the playing id it gave back (0 posted nothing, or was deferred).
 * Requests have their own budget, so breathing names cannot use up the lines that say why a call-out was refused. */
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
/* (system, u32 line, u32 variant, u32 group, u32 entity, float delay, u32 intensity, mode, ...; 12 in all): the queued
 * slot, 0 when refused. Its state gate (0x13A01A5): refused when [system] & (mask >> 6), or when the mask's low six bits
 * are not all in [system]; 0x13A7020 sets bit 0 of [system] when the player manager counts one player (+0x84 <= 1). */
#define CO_VOICE_REQUEST 0x13A0090u
static const char kVoiceRequestPin[] = "40 55 53 56 57 41 54 41 55 41 57 48 8D 6C 24 A0 48 81 EC 60 01 00 00";
#define CO_LINE_DEF 0x11FA230u     /* u8* (u32 voice, u32 group, u32 intensity): the line-def, its state mask at +0x34 */
static const char kLineDefPin[] = "44 89 44 24 18 89 54 24 10 89 4C 24 08 55 53 57 48 8B EC 48 83 EC 50";
#define CO_LINE_TEXT 0x1229C20u    /* const char* (u32 voice, u32 line): the line's name, NULL for none */
static const char kLineTextPin[] = "48 83 EC 48 81 F9 49 3A 5E 8F";
#define CO_ENTITY_RECORD 0xFD9D40u /* void* (u32 entity): its record; the voice component is keyed by [record + 8] */
static const char kEntityRecordPin[] = "48 83 EC 08 3B 0D ?? ?? ?? ?? 0F 84 9B 00 00 00";
#define CO_VOICE_OF 0x4F8850u      /* u32* (record): the CharacterVoiceComponent value, NULL for none */
static const char kVoiceOfPin[] = "48 83 EC 28 4C 8B D1 48 85 C9 75 07 33 C0 48 83 C4 28 C3 8B 41 08";
#define CO_NAME_HASH 0xAAFF30u     /* u64 (const char*, int length, u32 seed): the name's hash is the upper half */
static const char kNameHashPin[] = "48 89 5C 24 08 4C 63 DA 4C 8B D1 41 8B C3 4D 8B CB";
#define CO_EVENT_OF 0x12556E0u     /* u32 (u32 name hash): the Wwise event id; 0 for none */
static const char kEventOfPin[] = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 8B 05";
#define CO_VOICE_SYSTEM 0x346D520u
#define CO_CUSTOMIZATION 0x33264F8u /* records by entity: data +0x930, size +0x938, empty key +0x93C, multiplier +0x940 */
#define CO_VOICE_MANAGER 0x3326C30u /* by [record + 8]: data +0x80; count +0x130, the values at +0x134 */
#define CO_VOICE_VALUES 0x134u
#define CO_VOICE_ROOM 8u           /* values the manager has room for; its add checks no count */
#define CO_VOICE_ROWS 0x32F9518u   /* the four voices, 0x38 each, the id first */
#define CO_VOICE_PREFIXES 0x21CB530u /* char* per component value 1..9 */
#define CO_VOICE_SETS 0x32B4598u   /* u32 voice set per component value 1..9; 0x8BC32987 for none, as 0x13A0105 */
#define CO_VOICE_NO_SET 0x8BC32987u
#define CO_VOICE_LINES 200u
#define CO_VOICE_REQUESTS 150u
#define CO_VOICE_KINDS 64u         /* (group, verdict, state) the request log tells apart */

typedef uint64_t (*DeltaFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*NameFn)(uint64_t, uint64_t, uint64_t, uint64_t, char*, uint64_t);
typedef uint64_t (*PostFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*RequestFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t, uint64_t);
typedef uint64_t (*HashFn)(const char*, int, uint32_t);
typedef uint32_t (*EventFn)(uint32_t);
typedef const uint8_t* (*LineDefFn)(uint32_t, uint32_t, uint32_t);
typedef const char* (*LineTextFn)(uint32_t, uint32_t);
typedef const uint8_t* (*RecordFn)(uint32_t);
typedef const uint32_t* (*VoiceOfFn)(const uint8_t*);
static DeltaFn g_delta;
static NameFn g_name;
static PostFn g_post;
static RequestFn g_request;
static HashFn g_hash;   /* the game's lookups; a test gives its own */
static EventFn g_event;
static LineDefFn g_lineDef;
static LineTextFn g_lineText;
static RecordFn g_record;
static VoiceOfFn g_voiceOf;
static uintptr_t g_game;
static unsigned g_lines, g_names, g_posts, g_requests, g_requestLines, g_otherKinds;
static uint32_t g_lastValue = 0xFFFFFFFEu, g_lastCount = 0xFFFFFFFEu, g_lastEvent = 0xFFFFFFFEu;
static struct { uint32_t group, verdict, state, seen; } g_kinds[CO_VOICE_KINDS];

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
static void RequestLine(const char* format, ...);

/* The speaker's component value through the game's own two lookups (0xFD9D40, 0x4F8850), as 0x139ECA0 and 0x13A0090 make
 * them; *slot gets its place in the manager, -1 when the value comes from elsewhere. 0 when the speaker has none. */
static uint32_t VoiceValue(uint32_t entity, int* slot)
{
    const uint8_t* record = g_record(entity);
    const uint32_t* value = record != NULL ? g_voiceOf(record) : NULL;
    const uint8_t* vm = *(const uint8_t* const*)(g_game + CO_VOICE_MANAGER);
    *slot = -1;
    if (value == NULL) return 0;
    if (vm != NULL && (const uint8_t*)value >= vm + CO_VOICE_VALUES && (const uint8_t*)value < vm + CO_VOICE_VALUES + 4u * 16u)
        *slot = (int)(((const uint8_t*)value - (vm + CO_VOICE_VALUES)) / 4u);
    return *value;
}

static uint32_t VoiceSet(uint32_t value)
{
    const uint32_t set = value - 1u < 9u ? *(const uint32_t*)(g_game + CO_VOICE_SETS + 4u * (value - 1u)) : 0;
    return set != 0 ? set : CO_VOICE_NO_SET;
}

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

/* A line's name as the game built it for `entity`, with its component value and the manager's count. Variant 0 is the
 * base name, built for subtitles, the variant table and the queue's checks: its missing event drops nothing. */
void co_voice_name(uint32_t entity, uint32_t variant, const char* name, int built)
{
    const uint8_t* vm = *(const uint8_t* const*)(g_game + CO_VOICE_MANAGER);
    int slot;
    const uint32_t value = VoiceValue(entity, &slot);
    const uint32_t count = vm != NULL ? *(const uint32_t*)(vm + 0x130) : 0xFFFFFFFFu;
    const char* prefix = "-";
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
    Line("voice: line %u for entity %u: component slot %d value %d (%s), %u voiced entities (room for %u); name \"%.96s\"%s, variant %u, event 0x%08X%s",
         g_names, entity, slot, value == 0 ? -1 : (int)value, prefix, count, CO_VOICE_ROOM, built ? name : "", built ? "" : " not built", variant,
         event, event != 0 || !built ? "" : variant == 0 ? " (variant 0: a base name, not an event; nothing dropped)" : " (no such event: a request queued with it is dropped)");
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

enum { kQueued, kNoLineDef, kForbidden, kNeeded, kNotQueued };

/* The gate 0x13A0090 applied (0x13A01A5..0x13A01D0) for this request, read after it ran: [system] is what 0x13A7020 left
 * there for a nonzero mask. `queued` is its result. */
uint32_t co_voice_verdict(uint32_t mask, uint32_t state, int haveLineDef, int queued)
{
    if (!haveLineDef) return kNoLineDef;
    if (mask != 0 && (state & (mask >> 6)) != 0) return kForbidden;
    if (mask != 0 && (state & mask & 0x3Fu) != (mask & 0x3Fu)) return kNeeded;
    return queued ? kQueued : kNotQueued;
}

/* One line per (group, verdict, state) when first seen and each 20th time, at most CO_VOICE_REQUESTS a session. */
void co_voice_request(uint64_t system, uint32_t line, uint32_t group, uint32_t entity, uint32_t intensity, uint64_t queued)
{
    int slot;
    const uint32_t value = VoiceValue(entity, &slot), set = VoiceSet(value);
    const uint8_t* def = g_lineDef(set, group, intensity);
    const uint32_t mask = def != NULL ? *(const uint32_t*)(def + 0x34) : 0;
    const uint32_t state = system != 0 ? *(const uint32_t*)(uintptr_t)system : 0;
    const uint32_t verdict = co_voice_verdict(mask, state, def != NULL, queued != 0);
    const char* text = g_lineText(set, line);
    const char* mood = intensity == 0x5AE3F662u ? "spoken" : intensity == 0x802F7915u ? "combat" : intensity == 0x53347FB1u ? "mayhem" : "other";
    unsigned i, seen = 0;
    char why[160];
    g_requests++;
    for (i = 0; i < CO_VOICE_KINDS && g_kinds[i].seen != 0; i++)
        if (g_kinds[i].group == group && g_kinds[i].verdict == verdict && g_kinds[i].state == state) break;
    if (i < CO_VOICE_KINDS) { if (g_kinds[i].seen == 0) { g_kinds[i].group = group; g_kinds[i].verdict = verdict; g_kinds[i].state = state; } seen = ++g_kinds[i].seen; }
    else if (++g_otherKinds % 20 != 1) return; /* the table is full: each 20th of the kinds it has no room for */
    if (seen > 1 && seen % 20 != 0) return;
    switch (verdict)
    {
    case kNoLineDef: snprintf(why, sizeof why, "refused: no line-def for this group"); break;
    case kForbidden:
        snprintf(why, sizeof why, "refused: mask 0x%X, state 0x%X, forbidden bits 0x%X%s", mask, state, state & (mask >> 6),
                 (state & (mask >> 6) & 1u) != 0 ? " (solo: the game's rule for one player)" : "");
        break;
    case kNeeded: snprintf(why, sizeof why, "refused: mask 0x%X, state 0x%X, needed bits 0x%X missing", mask, state, (mask & 0x3Fu) & ~state); break;
    case kNotQueued: snprintf(why, sizeof why, "mask 0x%X, state 0x%X passed; not queued (speaker busy, priority, chance or no event)", mask, state); break;
    default: snprintf(why, sizeof why, "mask 0x%X, state 0x%X; queued", mask, state); break;
    }
    RequestLine("voice: request %u for entity %u (value %d): line \"%.64s\" 0x%08X, group 0x%08X, %s; %s%s", g_requests, entity,
                value == 0 ? -1 : (int)value, text != NULL ? text : "?", line, group, mood, why, seen > 1 ? " (again)" : "");
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
    co_voice_name((uint32_t)entity, (uint32_t)variant, name, (uint8_t)built != 0 && name != NULL);
    return built;
}

static uint64_t PostDetour(uint64_t system, uint64_t hash, uint64_t line, uint64_t source, uint64_t unit, uint64_t entity,
    uint64_t data, uint64_t callback, uint64_t s9, uint64_t s10)
{
    const uint64_t playing = g_post(system, hash, line, source, unit, entity, data, callback, s9, s10);
    co_voice_post(system, (uint32_t)hash, (uint32_t)line, (uint32_t)unit, (uint32_t)entity, (uint32_t)playing);
    return playing;
}

/* The delay (argument 6) is a float on the stack: passed on as its eight bytes, untouched. */
static uint64_t RequestDetour(uint64_t system, uint64_t line, uint64_t variant, uint64_t group, uint64_t entity, uint64_t delay,
    uint64_t intensity, uint64_t mode, uint64_t s9, uint64_t s10, uint64_t s11, uint64_t s12)
{
    const uint64_t queued = g_request(system, line, variant, group, entity, delay, intensity, mode, s9, s10, s11, s12);
    co_voice_request(system, (uint32_t)line, (uint32_t)group, (uint32_t)entity, (uint32_t)intensity, queued);
    return queued;
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

static void RequestLine(const char* format, ...)
{
    char text[400];
    va_list args;
    if (g_requestLines >= CO_VOICE_REQUESTS) return;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (++g_requestLines == CO_VOICE_REQUESTS) co_log("%s (the last voice request line of this session)", text);
    else co_log("%s", text);
}

void co_voice_install(uintptr_t game)
{
    g_game = game;
    g_hash = (HashFn)(game + CO_NAME_HASH);
    g_event = (EventFn)(game + CO_EVENT_OF);
    g_lineDef = (LineDefFn)(game + CO_LINE_DEF);
    g_lineText = (LineTextFn)(game + CO_LINE_TEXT);
    g_record = (RecordFn)(game + CO_ENTITY_RECORD);
    g_voiceOf = (VoiceOfFn)(game + CO_VOICE_OF);
    if (g_coApi->code_matches(game + CO_VOICE_DELTA, kVoiceDeltaPin) != 1 || g_coApi->code_matches(game + CO_VOICE_READ, kVoiceReadPin) != 1 ||
        g_coApi->code_matches(game + CO_VOICE_NAME, kVoiceNamePin) != 1 || g_coApi->code_matches(game + CO_VOICE_POST, kVoicePostPin) != 1 ||
        g_coApi->code_matches(game + CO_VOICE_REQUEST, kVoiceRequestPin) != 1 || g_coApi->code_matches(game + CO_LINE_DEF, kLineDefPin) != 1 ||
        g_coApi->code_matches(game + CO_LINE_TEXT, kLineTextPin) != 1 || g_coApi->code_matches(game + CO_ENTITY_RECORD, kEntityRecordPin) != 1 ||
        g_coApi->code_matches(game + CO_VOICE_OF, kVoiceOfPin) != 1 || g_coApi->code_matches(game + CO_NAME_HASH, kNameHashPin) != 1 ||
        g_coApi->code_matches(game + CO_EVENT_OF, kEventOfPin) != 1)
    { co_log("voice: the voice code is not the one the mod was made for; voice lines are not logged"); return; }
    if (g_coApi->hook(g_coMod, game + CO_VOICE_DELTA, (void*)DeltaDetour, (void**)&g_delta) != 0 ||
        g_coApi->hook(g_coMod, game + CO_VOICE_NAME, (void*)NameDetour, (void**)&g_name) != 0 ||
        g_coApi->hook(g_coMod, game + CO_VOICE_POST, (void*)PostDetour, (void**)&g_post) != 0 ||
        g_coApi->hook(g_coMod, game + CO_VOICE_REQUEST, (void*)RequestDetour, (void**)&g_request) != 0)
    { co_log("voice: a hook could not be placed; the voice lines' log is incomplete"); return; }
    co_log("voice: logging the spawn's voice delta, each request's gate, each line's name and each post (at most %u + %u lines)",
           CO_VOICE_LINES, CO_VOICE_REQUESTS);
}

#ifdef CO_TEST
void co_voice_test_reset(void)
{
    g_game = 0; g_lines = g_names = g_posts = g_requests = g_requestLines = g_otherKinds = 0;
    g_lastValue = g_lastCount = g_lastEvent = 0xFFFFFFFEu;
    memset(g_kinds, 0, sizeof g_kinds);
    g_delta = NULL; g_name = NULL; g_post = NULL; g_request = NULL;
}

void co_voice_test_lookups(uint64_t (*hash)(const char*, int, uint32_t), uint32_t (*event)(uint32_t)) { g_hash = hash; g_event = event; }

void co_voice_test_game(const uint8_t* (*lineDef)(uint32_t, uint32_t, uint32_t), const char* (*lineText)(uint32_t, uint32_t),
    const uint8_t* (*record)(uint32_t), const uint32_t* (*voiceOf)(const uint8_t*))
{
    g_lineDef = lineDef; g_lineText = lineText; g_record = record; g_voiceOf = voiceOf;
}
#endif

/* The HUD's changing texts behind the engine's lookup (co_text.h). */
#include <string.h>

#include "co_text.h"
#include "co_text_sites.h"

typedef const char* (*LookupFn)(uint64_t id, uint64_t language, uint64_t r8, uint64_t r9);
static struct { LookupFn original; CoTextFn dynamic; uint32_t first, count; } g_text;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[32];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* The lookup's first read: mov rax, [the string tables' root]. The root is 0 until the engine has loaded its texts
 * (the title screen still has none), and the lookup then reads through it unchecked. */
#define CO_TEXT_ROOT_LOAD 0x321C5Cu
static const uint8_t kCoTextRootLoad[7] = {0x48, 0x8B, 0x05, 0x7D, 0xE5, 0x6E, 0x01};
#define CO_TEXT_ROOT (CO_TEXT_ROOT_LOAD + 7u + 0x16EE57Du)

static int Loaded(uintptr_t exe)
{
    uint64_t root = 0;
    return g_coApi->read(exe + CO_TEXT_ROOT, &root, sizeof root) == 0 && root != 0;
}

const char* co_text_check(uintptr_t exe)
{
    if (exe == 0 || !Same(exe + CO_TEXT_ENTRY, kCoTextEntry, sizeof kCoTextEntry) || !Same(exe + CO_TEXT_LOOKUP, kCoTextLookup, sizeof kCoTextLookup) ||
        !Same(exe + CO_TEXT_ROOT_LOAD, kCoTextRootLoad, sizeof kCoTextRootLoad))
        return "the engine's text lookup is not the code this mod was made for";
    return NULL;
}

/* The ids are the mod's only while its provider has a text for them; any other id costs one compare. */
static const char* Lookup(uint64_t id, uint64_t language, uint64_t r8, uint64_t r9)
{
    const CoTextFn dynamic = g_text.dynamic;
    if ((uint32_t)id - g_text.first < g_text.count && dynamic != NULL)
    {
        /* the engine's own text keeps an id it has one for: the install could only ask once the texts were loaded */
        const char* own = g_text.original(id, language, r8, r9);
        if (own != NULL && own[0] != 0) return own;
        const char* text = dynamic((uint32_t)id);
        return text != NULL ? text : own;
    }
    return g_text.original(id, language, r8, r9);
}

const char* co_text_install(uintptr_t exe, uint32_t first, uint32_t count)
{
    const char* why;
    if (g_text.original != NULL) return NULL;
    if ((why = co_text_check(exe)) != NULL) return why;
    /* asked of the engine as the game asks it, before anything is hooked: each of the ids must be free */
    const LookupFn engine = (LookupFn)(exe + CO_TEXT_LOOKUP);
    for (uint32_t i = 0; Loaded(exe) && i < count; i++)
    {
        const char* text = engine(first + i, 0, 0, 0);
        if (text != NULL && text[0] != 0) return "the engine has a text of its own at one of the HUD rows' ids";
    }
    g_text.first = first; g_text.count = count;
    if (g_coApi->hook_ex(g_coMod, exe + CO_TEXT_LOOKUP, (void*)Lookup, (void**)&g_text.original, INT32_MIN) != 0 || g_text.original == NULL)
    {
        g_text.original = NULL; g_text.count = 0;
        return "the engine's text lookup could not be hooked";
    }
    return NULL;
}

CoTextFn* co_text_dynamic(void) { return &g_text.dynamic; }

static const char* Engine(uint32_t id, void* a, void* b, void* c) { return g_text.original(id, 0, 0, 0); }
CoEngineTextFn co_text_engine(void) { return g_text.original != NULL ? Engine : NULL; }

#ifdef CO_TEST
void co_text_test_reset(void) { memset(&g_text, 0, sizeof g_text); }
void co_text_test_original(void* original) { g_text.original = (LookupFn)original; }
#endif

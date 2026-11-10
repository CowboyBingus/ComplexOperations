/* Eight players: the engine's peer list message. helldivers2.exe sends the session's peers as [count][peer...] with
 * the count in a byte of its own; the sender writes the whole byte, the readers take three bits of it, so eight
 * peers arrive as none. The readers take four bits here. Up to seven peers the message is the game's own, bit for
 * bit.
 * A build for eight players also lets the lobby search list lobbies with up to seven members. */
#include <string.h>
#include <windows.h>

#include "co.h"

/* {rva in helldivers2.exe, offset of the value, size, its new value, the instruction bytes with the vanilla value} */
typedef struct CoEngineSite { uint32_t rva; uint8_t at, size, now; uint8_t code[12]; } CoEngineSite;
static const CoEngineSite kSites[] = {
    {0x291869u, 6, 10, 4, {0xB9, 0x08, 0x00, 0x00, 0x00, 0xB8, 0x03, 0x00, 0x00, 0x00}},       /* a client's read: width */
    {0x29189Fu, 5, 12, 4, {0x03, 0xDA, 0x41, 0x83, 0xFB, 0x03, 0x0F, 0x82, 0x6D, 0xFF, 0xFF, 0xFF}}, /* and its bit loop */
    {0x291735u, 1, 8, 4, {0xBA, 0x03, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF}},                     /* the host's read past it */
    {0x294AC0u, 6, 10, 4, {0xB9, 0x08, 0x00, 0x00, 0x00, 0xB8, 0x03, 0x00, 0x00, 0x00}},       /* the statistics walker: width */
    {0x294AFAu, 2, 5, 4, {0x83, 0xFF, 0x03, 0x72, 0x81}},                                       /* and its bit loop */
};
/* The search results' copier skips a lobby with this many members: cmp dword [r14-4],4; jae. */
static const CoEngineSite kLobbySites[] = {{0x8DE9CBu, 4, 5, 8, {0x41, 0x83, 0x7E, 0xFC, 0x04}}};
#define CO_ENGINE_SITES (sizeof kSites / sizeof kSites[0])
#define CO_LOBBY_SITES (sizeof kLobbySites / sizeof kLobbySites[0])

static uintptr_t g_engine;
static int g_state, g_lobby; /* 0 not done, 1 written, -1 left as the game has it */

/* All sites or none: the reviewed bytes first, and a failed write takes back the ones before it. */
static int Apply(const CoEngineSite* sites, size_t count, const char* stays)
{
    for (size_t i = 0; i < count; i++)
    {
        uint8_t bytes[12];
        if (g_coApi->read(g_engine + sites[i].rva, bytes, sites[i].size) != 0 || memcmp(bytes, sites[i].code, sites[i].size) != 0)
        { co_log("engine: the code at helldivers2.exe+0x%X is not the reviewed one; %s", sites[i].rva, stays); return -1; }
    }
    for (size_t i = 0; i < count; i++)
        if (g_coApi->write(g_engine + sites[i].rva + sites[i].at, &sites[i].now, 1) != 0)
        {
            for (size_t j = 0; j < i; j++) g_coApi->write(g_engine + sites[j].rva + sites[j].at, &sites[j].code[sites[j].at], 1);
            co_log("engine: helldivers2.exe+0x%X could not be written; %s", sites[i].rva, stays);
            return -1;
        }
    return 1;
}

void co_engine_install(uintptr_t engine)
{
    g_engine = engine;
    g_state = g_lobby = -1;
    if (engine == 0) { co_log("engine: helldivers2.exe was not found; the peer list message stays as it is"); return; }
    /* A reader with four bits beside one with three reads two different counts. */
    g_state = Apply(kSites, CO_ENGINE_SITES, "the peer list message stays as it is");
    if (g_state == 1) co_log("engine: the peer list message counts up to 15 peers (%u code sites changed)", (unsigned)CO_ENGINE_SITES);
    if (CO_PLAYERS != 8) return;
    g_lobby = Apply(kLobbySites, CO_LOBBY_SITES, "the lobby search skips lobbies of four as before");
    if (g_lobby == 1) co_log("engine: the lobby search lists lobbies with up to 7 members (%u code site changed)", (unsigned)CO_LOBBY_SITES);
}

#ifdef CO_TEST
void co_engine_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_ENGINE_SITES; i++) memcpy(image + kSites[i].rva, kSites[i].code, kSites[i].size);
    for (size_t i = 0; i < CO_LOBBY_SITES; i++) memcpy(image + kLobbySites[i].rva, kLobbySites[i].code, kLobbySites[i].size);
}
int co_engine_test_state(void) { return g_state; }
int co_engine_test_lobby(void) { return g_lobby; }
#endif

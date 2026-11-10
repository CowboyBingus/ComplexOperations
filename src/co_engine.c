/* Eight players: the engine's peer list message. helldivers2.exe sends the session's peers as [count][peer...] with
 * the count in a byte of its own; the sender writes the whole byte, the readers take three bits of it, so eight
 * peers arrive as none. The readers take four bits here. Up to seven peers the message is the game's own, bit for
 * bit. */
#include <string.h>
#include <windows.h>

#include "co.h"

/* {rva in helldivers2.exe, offset of the value, the instruction bytes with the vanilla value 3} */
typedef struct CoEngineSite { uint32_t rva; uint8_t at, size; uint8_t code[12]; } CoEngineSite;
static const CoEngineSite kSites[] = {
    {0x291869u, 6, 10, {0xB9, 0x08, 0x00, 0x00, 0x00, 0xB8, 0x03, 0x00, 0x00, 0x00}},       /* a client's read: width */
    {0x29189Fu, 5, 12, {0x03, 0xDA, 0x41, 0x83, 0xFB, 0x03, 0x0F, 0x82, 0x6D, 0xFF, 0xFF, 0xFF}}, /* and its bit loop */
    {0x291735u, 1, 8, {0xBA, 0x03, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF}},                     /* the host's read past it */
    {0x294AC0u, 6, 10, {0xB9, 0x08, 0x00, 0x00, 0x00, 0xB8, 0x03, 0x00, 0x00, 0x00}},       /* the statistics walker: width */
    {0x294AFAu, 2, 5, {0x83, 0xFF, 0x03, 0x72, 0x81}},                                       /* and its bit loop */
};
#define CO_ENGINE_SITES (sizeof kSites / sizeof kSites[0])

static uintptr_t g_engine;
static int g_state; /* 0 not done, 1 four bits, -1 left as the game has it */

void co_engine_install(uintptr_t engine)
{
    static const uint8_t four = 4, three = 3;
    g_engine = engine;
    g_state = -1;
    if (engine == 0) { co_log("engine: helldivers2.exe was not found; the peer list message stays as it is"); return; }
    for (size_t i = 0; i < CO_ENGINE_SITES; i++)
    {
        uint8_t bytes[12];
        if (g_coApi->read(engine + kSites[i].rva, bytes, kSites[i].size) != 0 || memcmp(bytes, kSites[i].code, kSites[i].size) != 0)
        { co_log("engine: the code at helldivers2.exe+0x%X is not the reviewed one; the peer list message stays as it is", kSites[i].rva); return; }
    }
    for (size_t i = 0; i < CO_ENGINE_SITES; i++)
        if (g_coApi->write(engine + kSites[i].rva + kSites[i].at, &four, 1) != 0)
        {
            /* A reader with four bits beside one with three reads two different counts: all go back. */
            for (size_t j = 0; j < i; j++) g_coApi->write(engine + kSites[j].rva + kSites[j].at, &three, 1);
            co_log("engine: helldivers2.exe+0x%X could not be written; the peer list message stays as it is", kSites[i].rva);
            return;
        }
    g_state = 1;
    co_log("engine: the peer list message counts up to 15 peers (%u code sites changed)", (unsigned)CO_ENGINE_SITES);
}

#ifdef CO_TEST
void co_engine_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_ENGINE_SITES; i++) memcpy(image + kSites[i].rva, kSites[i].code, kSites[i].size);
}
int co_engine_test_state(void) { return g_state; }
#endif

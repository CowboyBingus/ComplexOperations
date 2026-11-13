/* Whether an operation is a complex operation. Every peer builds the same operation board from one synced seed, and
 * each operation record carries its own seed and planet, so a roll made of those two alone gives every peer the same
 * answer with no message of the mod's own. Integers only: no floating point and no library generator, whose results
 * may differ between builds. Every build of the mod must keep these constants.
 *
 *   h = fmix32(seed ^ (planet * 0x9E3779B1) ^ CO_ROLL_SALT)      all in 32 bits, wrapping
 *   complex when (h >> 16) < chance_65536
 *
 * fmix32 is MurmurHash3's 32-bit finaliser: h ^= h >> 16; h *= 0x85EBCA6B; h ^= h >> 13; h *= 0xC2B2AE35; h ^= h >> 16.
 * It maps the 2^32 inputs one to one, so over all seeds the upper 16 bits take each value equally often and the chance
 * is exact in 1/65536. The planet is spread by the golden ratio's odd multiplier before it meets the seed, so two
 * planets with the same seed do not roll alike. */
#include "co.h"

#define CO_ROLL_SALT 0xC0A11CE5u
#define CO_PICK_SALT 0x0E9E1A7Bu

static uint32_t Mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

int co_complex_roll(uint32_t operation_seed, uint32_t planet, uint32_t chance_65536)
{
    return (Mix(operation_seed ^ (planet * 0x9E3779B1u) ^ CO_ROLL_SALT) >> 16) < chance_65536;
}

/* Every planet with a level-11 operation has one complex operation for certain; the others roll as above. That
 * guaranteed one is the board slot with the lowest key:
 *
 *   key = fmix32(board_seed ^ (planet * 0x9E3779B1) ^ (slot * 0x85EBCA77) ^ CO_PICK_SALT)
 *
 * A slot and the board seed are what a committed operation keeps: its record goes back into its own slot with its seed,
 * and the slots generated after it draw other seeds (0x11E3E73 skips a slot in use without a draw). A key of the
 * operation's seed would move the pick to or from the operation in play. A lowest key stays when another slot goes or
 * one with a higher key comes, in any order of reading; the odd multiplier and fmix32 give two slots two keys. */
uint32_t co_pick_key(uint32_t board_seed, uint32_t planet, uint32_t slot)
{
    return Mix(board_seed ^ (planet * 0x9E3779B1u) ^ (slot * 0x85EBCA77u) ^ CO_PICK_SALT);
}

uint32_t co_planet_decide(const CoOperation* ops, uint32_t count, uint32_t board_seed, uint32_t planet, uint32_t chance_65536, uint8_t* complex)
{
    uint32_t pick = count;
    for (uint32_t i = 0; i < count; i++)
        if (pick == count || co_pick_key(board_seed, planet, ops[i].slot) < co_pick_key(board_seed, planet, ops[pick].slot)) pick = i;
    for (uint32_t i = 0; i < count; i++) complex[i] = (uint8_t)(i == pick || co_complex_roll(ops[i].seed, planet, chance_65536));
    return pick;
}

/* The decision travels in the mission's seed. A client generates a mission with the host's seed (the host sends its
 * string at launch and to a player who joins), so the host's answer reaches every peer with no message of the mod's
 * own. The game feeds all 32 bits of a seed into its generator and takes any value, so no bit is spare: one bit is
 * forced, set on a complex operation's missions and clear on every other level-11 mission, and read back at generation.
 * Seeds of other levels are left as the game made them. Every build of the mod must keep the bit. */
#define CO_SEED_MARK 0x1u

uint32_t co_seed_marked(uint32_t mission_seed, int complex) { return complex ? mission_seed | CO_SEED_MARK : mission_seed & ~CO_SEED_MARK; }

int co_seed_is_complex(uint32_t mission_seed) { return (mission_seed & CO_SEED_MARK) != 0; }

static int Hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* The text keeps the seed's 32 bits, highest digit first: the mark is the lowest bit of digit 11. Another version's
 * text, or one cut short, is no complex mission's. */
int co_seed_text_marked(const char* text)
{
    if (text[0] != 'f' || text[1] != '3' || text[2] != 'f' || text[3] != 'b') return 0;
    for (int i = 4; i < 24; i++) if (Hex(text[i]) < 0) return 0;
    return Hex(text[22]) * 16 + Hex(text[23]) == (int)CO_COMPLEX_LEVEL && co_seed_is_complex((uint32_t)Hex(text[11]));
}

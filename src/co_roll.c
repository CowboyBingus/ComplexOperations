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

int co_complex_roll(uint32_t operation_seed, uint32_t planet, uint32_t chance_65536)
{
    uint32_t h = operation_seed ^ (planet * 0x9E3779B1u) ^ CO_ROLL_SALT;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return (h >> 16) < chance_65536;
}

/* The decision travels in the mission's seed. A client generates a mission with the host's seed (the host sends its
 * string at launch and to a player who joins), so the host's answer reaches every peer with no message of the mod's
 * own. The game feeds all 32 bits of a seed into its generator and takes any value, so no bit is spare: one bit is
 * forced, set on a complex operation's missions and clear on every other level-11 mission, and read back at generation.
 * Seeds of other levels are left as the game made them. Every build of the mod must keep the bit. */
#define CO_SEED_MARK 0x1u

uint32_t co_seed_marked(uint32_t mission_seed, int complex) { return complex ? mission_seed | CO_SEED_MARK : mission_seed & ~CO_SEED_MARK; }

int co_seed_is_complex(uint32_t mission_seed) { return (mission_seed & CO_SEED_MARK) != 0; }

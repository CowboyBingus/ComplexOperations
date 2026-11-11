/* The C side of the stack fixes (scripts/sites/stack_sites.py). The game sums distances to every avatar, and shuffles
 * avatar indexes, in stack arrays of four; with more avatars than that these callbacks do the work without an array.
 * Both give the game's own result for four avatars or fewer, bit for bit, and skip an avatar with no motion record,
 * which the game's code would read at record 0xFFFFFFFF. */
#include <xmmintrin.h>

#include "co.h"

/* Single precision in the game's order: no fused multiply-add. */
#pragma STDC FP_CONTRACT OFF

#define CO_AVATARS 0x3326D20u     /* the avatar manager: count at +0x6C, slot pointers at +0x110, the id at entity+8 */
#define CO_MOTION 0x3326558u      /* the motion manager: a hash of entity ids to records of 0x2C, position first */
#define CO_NULL_ID 0x3483C20u     /* the id of no entity */
#define CO_ENC_STATE 0x3483C38u   /* the random state of the encounter's shuffle */
#define CO_ENC_CALL 0x9FAC40u     /* one enemy call towards the avatar index in edx; -1 for none */
#define CO_AVATAR_ROOM 16u        /* the avatar manager's slots in a build for eight; a build for four has eight */

typedef void (*EncFn)(uint64_t, uint32_t);

static uintptr_t g_game;

void co_avatars_install(uintptr_t game) { g_game = game; }

static uint32_t Count(void)
{
    const uint8_t* avatars = *(const uint8_t* const*)(g_game + CO_AVATARS);
    const uint32_t n = *(const uint32_t*)(avatars + 0x6C);
    return n < CO_AVATAR_ROOM ? n : CO_AVATAR_ROOM;
}

/* The motion record of an entity id, as the game's inline probe finds it; 0xFFFFFFFF for none. */
static uint32_t MotionRecord(const uint8_t* motion, uint32_t id)
{
    const uint32_t size = *(const uint32_t*)(motion + 0x48A8), empty = *(const uint32_t*)(motion + 0x48AC);
    const uint32_t mult = *(const uint32_t*)(motion + 0x48B0);
    const uint32_t* buckets = *(const uint32_t* const*)(motion + 0x48A0);
    for (uint32_t k = 0; k < size; k++)
    {
        const uint32_t s = (k + mult * id) & (size - 1u);
        if (buckets[2 * s] == empty) return id == empty ? buckets[2 * s + 1] : 0xFFFFFFFFu;
        if (buckets[2 * s] == id) return buckets[2 * s + 1];
    }
    return 0xFFFFFFFFu;
}

/* eax the avatar count the cull took, rdx the candidate's position: eax gets the float bits of the sum of distances. */
void co_avatars_dist_sum(CoRegs* regs)
{
    const float* c = (const float*)(uintptr_t)regs->rdx;
    const uint8_t* avatars = *(const uint8_t* const*)(g_game + CO_AVATARS);
    const uint8_t* motion = *(const uint8_t* const*)(g_game + CO_MOTION);
    const uint32_t none = *(const uint32_t*)(g_game + CO_NULL_ID);
    uint32_t n = (uint32_t)regs->rax, bits;
    float sum = 0.0f;
    if (n > CO_AVATAR_ROOM) n = CO_AVATAR_ROOM;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint8_t* entity = *(const uint8_t* const*)(avatars + 0x110 + 8u * i);
        const uint32_t id = *(const uint32_t*)(entity + 8);
        if (id == none) continue;
        const uint32_t record = MotionRecord(motion, id);
        if (record == 0xFFFFFFFFu) continue;
        const float* p = (const float*)(*(const uint8_t* const*)(motion + 0x48D8) + 0x2Cu * (uint64_t)record);
        const float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
        const float d2 = (dy * dy + dx * dx) + dz * dz;
        sum += d2 == 0.0f ? 0.0f : _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(d2)));
    }
    __builtin_memcpy(&bits, &sum, 4);
    regs->rax = bits;
}

/* esi the stage's enemy calls: a cyclic shuffle of the avatar indexes from the game's random state, then the calls to
 * the avatars in that order, round and round. No avatar: one call with -1, as the game makes it. */
void co_avatars_enc_spread(CoRegs* regs)
{
    const EncFn call = (EncFn)(g_game + CO_ENC_CALL);
    const uint32_t calls = (uint32_t)regs->rsi, n = Count();
    uint32_t order[CO_AVATAR_ROOM];
    if (n == 0) { call(0, 0xFFFFFFFFu); return; }
    for (uint32_t i = 0; i < n; i++) order[i] = i;
    if (n > 1)
    {
        uint64_t* state = (uint64_t*)(g_game + CO_ENC_STATE);
        uint64_t s = *state;
        for (uint32_t k = 0; k + 1 < n; k++)
        {
            s = s * 0x5851F42D4C957F2Du + 0x14057B7EF767814Fu;
            const uint32_t j = k + 1u + (uint32_t)(s >> 32) % (n - k - 1u);
            const uint32_t t = order[k];
            order[k] = order[j];
            order[j] = t;
        }
        *state = s;
    }
    for (uint32_t c = 0; c < calls; c++) call(0, order[c % n]);
}

/* The coarse terrain compare's pure parts (research, HELLFORGE_XL_COARSE_COMPARE in proto/difficulty_world_diag.c): a
 * readback's size from its format and the bitwise compare of two images. */
#ifndef CO_COMPARE_H
#define CO_COMPARE_H

#include <stddef.h>
#include <stdint.h>

#define CO_COMPARE_BPT 0x1658990u /* exe: bits a texel by format, u32 each; +0x1F0 (0x31C962) and +0x1F8 (0x31DEFF) read it */
#define CO_COMPARE_BPT_COUNT 0x1Bu /* the entries co_compare_table_matches checks, formats 0..0x1A */

/* Bytes a sample for the formats the compare reads (bpt / 8, packed): 0x00, 0x01, 0x10 -> 4, 0x16, 0x17 -> 2,
 * 0x1A -> 1. 0 for any other format, which the compare refuses. */
uint32_t co_compare_sample_bytes(uint32_t format);
/* 1 when the exe's table (its first CO_COMPARE_BPT_COUNT entries) holds the sizes above. */
int co_compare_table_matches(const uint32_t* bpt);

typedef struct CoCompareDiff
{
    uint64_t samples, differ; /* samples whose bytes differ */
    double most;              /* largest |a - b| among them: f32 for format 0x1, else the unsigned sample */
    const char* unit;
} CoCompareDiff;
/* Bitwise, sample by sample (+0 and -0 differ, equal NaN bits do not); 0, or -1 for a format the compare refuses. */
int co_compare_diff(const uint8_t* a, const uint8_t* b, uint32_t w, uint32_t h, uint32_t format, CoCompareDiff* out);

#endif

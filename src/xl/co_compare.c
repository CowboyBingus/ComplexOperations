/* The coarse terrain compare's pure parts (co_compare.h). */
#include <math.h>
#include <string.h>

#include "co_compare.h"

uint32_t co_compare_sample_bytes(uint32_t format)
{
    switch (format)
    {
    case 0x00: case 0x01: case 0x10: return 4;
    case 0x16: case 0x17: return 2;
    case 0x1A: return 1;
    default: return 0;
    }
}

int co_compare_table_matches(const uint32_t* bpt)
{
    for (uint32_t format = 0; format < CO_COMPARE_BPT_COUNT; format++)
    {
        uint32_t bytes = co_compare_sample_bytes(format);
        if (bytes != 0 && bpt[format] != 8u * bytes) return 0;
    }
    return 1;
}

int co_compare_diff(const uint8_t* a, const uint8_t* b, uint32_t w, uint32_t h, uint32_t format, CoCompareDiff* out)
{
    memset(out, 0, sizeof *out);
    uint32_t bytes = co_compare_sample_bytes(format);
    if (bytes == 0) return -1;
    out->samples = (uint64_t)w * h;
    out->unit = format == 0x01 ? "f32" : bytes == 4 ? "u32" : bytes == 2 ? "u16" : "u8";
    for (uint64_t i = 0; i < out->samples; i++)
    {
        const uint8_t *x = a + i * bytes, *y = b + i * bytes;
        if (memcmp(x, y, bytes) == 0) continue;
        out->differ++;
        double d;
        if (format == 0x01) { float p, q; memcpy(&p, x, 4); memcpy(&q, y, 4); d = fabs((double)p - (double)q); }
        else if (bytes == 4) { uint32_t p, q; memcpy(&p, x, 4); memcpy(&q, y, 4); d = p > q ? p - q : q - p; }
        else if (bytes == 2) { uint16_t p, q; memcpy(&p, x, 2); memcpy(&q, y, 2); d = p > q ? p - q : q - p; }
        else d = x[0] > y[0] ? x[0] - y[0] : y[0] - x[0];
        if (d > out->most) out->most = d; /* a NaN difference leaves `most` as it is */
    }
    return 0;
}

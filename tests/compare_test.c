/* The coarse terrain compare (research): a readback is read at the exe's size for its format and an unknown format is
 * refused, two images are compared bit for bit, and a preview's minimap pass reads the record's own CPU copies. The
 * texel table below is the live exe's at 0x1658990, formats 0..0x1A. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_compare.h"
#include "xl/co_shim.h"

int CoXlTestCompareOn(void);
void CoXlTestCompareImage(const char* label, uintptr_t descriptor);
void CoXlTestComparePass(uint32_t op, uintptr_t level);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const uint32_t kExeBpt[CO_COMPARE_BPT_COUNT] = {32, 32, 16, 32, 32, 4, 8, 8, 4, 8, 8, 8, 128, 64, 32, 64, 32, 32, 16, 64, 64, 64, 16, 16,
    32, 32, 8};

/* An image as the game holds it: {w, h, format, ?, data} (the render table's +0x1D8 copy, exe 0x31BAB0). */
typedef struct { uint32_t w, h, format, unused; uint64_t data; } Readback;

static char g_logs[MAX_PATH];
static HfApi g_api;
static const char* Logs(HfMod* mod) { return g_logs; }

static void Reset(const char* name, const uint32_t* bpt)
{
    g_case = name;
    g_api = *fake_host_reset(CO_GAME_BUILD);
    g_api.logs_folder = Logs;
    g_coApi = &g_api;
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    if (bpt != NULL) memcpy(exe + CO_COMPARE_BPT, bpt, CO_COMPARE_BPT_COUNT * 4);
}

static long long PipelineBytes(const char* pipeline, const char* label)
{
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\coarse-compare-%s-%s.bin", g_logs, pipeline, label);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    return ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}
static long long FileBytes(const char* label) { return PipelineBytes("mission", label); }

/* Every file a case writes, removed before and after: the folder's name repeats from run to run under wine. */
static void Clean(void)
{
    static const char* labels[] = {"mission-f16", "mission-f17", "mission-f1a", "mission-f10", "mission-f00", "mission-f01", "mission-f02", "mission-g16",
        "preview-minimap-heights", "preview-minimap-water"};
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++)
    {
        char path[MAX_PATH * 2];
        snprintf(path, sizeof path, "%s\\coarse-compare-%s.bin", g_logs, labels[i]);
        DeleteFileA(path);
    }
}

/* An image of w x h samples of `bytes` each, exactly that large: a read past it would show in the file's size. */
static void Image(const char* label, uint32_t format, uint32_t w, uint32_t h, uint32_t bytes)
{
    size_t size = (size_t)w * h * (bytes ? bytes : 4);
    uint8_t* data = malloc(size);
    for (size_t i = 0; i < size; i++) data[i] = (uint8_t)(i * 7 + 1);
    Readback r = {w, h, format, 0, (uint64_t)(uintptr_t)data};
    CoXlTestCompareImage(label, (uintptr_t)&r);
    free(data);
}

int main(void)
{
    g_case = "sample sizes";
    {
        static const uint32_t known[][2] = {{0x00, 4}, {0x01, 4}, {0x10, 4}, {0x16, 2}, {0x17, 2}, {0x1A, 1}};
        for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        {
            CHECK(co_compare_sample_bytes(known[i][0]) == known[i][1]);
            CHECK(kExeBpt[known[i][0]] == 8 * known[i][1]);
        }
        static const uint32_t unknown[] = {0x02, 0x05, 0x0C, 0x11, 0x18, 0x1B, 0xFF, 0xFFFFFFFFu};
        for (size_t i = 0; i < sizeof unknown / sizeof unknown[0]; i++) CHECK(co_compare_sample_bytes(unknown[i]) == 0);
    }

    g_case = "the exe's table";
    {
        uint32_t t[CO_COMPARE_BPT_COUNT];
        CHECK(co_compare_table_matches(kExeBpt));
        memcpy(t, kExeBpt, sizeof t); t[0x17] = 32; CHECK(!co_compare_table_matches(t));
        memcpy(t, kExeBpt, sizeof t); t[0x1A] = 32; CHECK(!co_compare_table_matches(t));
        memcpy(t, kExeBpt, sizeof t); t[0x02] = 99; CHECK(co_compare_table_matches(t)); /* a format the compare does not read */
    }

    g_case = "bitwise compare";
    {
        CoCompareDiff d;
        float a[4] = {0.0f, 1.0f, 2.0f, 3.0f}, b[4] = {-0.0f, 1.0f, 2.5f, 3.0f};
        CHECK(co_compare_diff((const uint8_t*)a, (const uint8_t*)b, 2, 2, 0x01, &d) == 0);
        CHECK(d.samples == 4 && d.differ == 2 && d.most == 0.5 && strcmp(d.unit, "f32") == 0); /* +0 and -0 differ */
        uint32_t nan = 0x7FC00001u, nan2 = 0x7FC00002u, x[2] = {nan, 5}, y[2] = {nan, 5}, z[2] = {nan2, 5};
        CHECK(co_compare_diff((const uint8_t*)x, (const uint8_t*)y, 2, 1, 0x01, &d) == 0 && d.differ == 0);
        CHECK(co_compare_diff((const uint8_t*)x, (const uint8_t*)z, 2, 1, 0x01, &d) == 0 && d.differ == 1);
        uint32_t u[2] = {10, 7}, v[2] = {3, 7};
        CHECK(co_compare_diff((const uint8_t*)u, (const uint8_t*)v, 1, 2, 0x10, &d) == 0 && d.differ == 1 && d.most == 7 && strcmp(d.unit, "u32") == 0);
        uint16_t p[3] = {100, 200, 300}, q[3] = {100, 150, 301};
        CHECK(co_compare_diff((const uint8_t*)p, (const uint8_t*)q, 3, 1, 0x17, &d) == 0 && d.samples == 3 && d.differ == 2 && d.most == 50);
        uint8_t m[4] = {1, 2, 3, 4}, n[4] = {1, 9, 3, 4};
        CHECK(co_compare_diff(m, n, 2, 2, 0x1A, &d) == 0 && d.samples == 4 && d.differ == 1 && d.most == 7 && strcmp(d.unit, "u8") == 0);
        CHECK(co_compare_diff(m, n, 2, 2, 0x02, &d) == -1 && d.samples == 0 && d.differ == 0);
    }

    DWORD n = GetTempPathA(sizeof g_logs - 32, g_logs);
    snprintf(g_logs + n, sizeof g_logs - n, "co-compare-test-%lu", (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_logs, NULL);
    Clean();

    Reset("each format read at its size", kExeBpt);
    CHECK(CoXlTestCompareOn() == 1);
    Image("f16", 0x16, 8, 4, 2);
    Image("f17", 0x17, 8, 4, 2);
    Image("f1a", 0x1A, 8, 4, 1);
    Image("f10", 0x10, 8, 4, 4);
    Image("f00", 0x00, 8, 4, 4);
    Image("f01", 0x01, 8, 4, 4);
    CHECK(FileBytes("f16") == 64 && FileBytes("f17") == 64 && FileBytes("f1a") == 32);
    CHECK(FileBytes("f10") == 128 && FileBytes("f00") == 128 && FileBytes("f01") == 128);
    CHECK(fake_host_logged("f17 (batch") && fake_host_logged(": 8 x 4, 2 B a sample (format 0x17)"));
    CHECK(fake_host_logged(": 8 x 4, 1 B a sample (format 0x1A)"));
    CHECK(!fake_host_logged("assumed"));

    Reset("an unknown format is refused", kExeBpt);
    CHECK(CoXlTestCompareOn() == 1);
    Image("f02", 0x02, 8, 4, 0);
    CHECK(FileBytes("f02") == -1 && fake_host_logged("f02: 8 x 4, format 0x2: no known sample size, not read"));

    Reset("another exe's table refuses every image", kExeBpt);
    {
        uint32_t t[CO_COMPARE_BPT_COUNT];
        memcpy(t, kExeBpt, sizeof t); t[0x17] = 32;
        memcpy(fake_host_exe() + CO_COMPARE_BPT, t, sizeof t);
    }
    CHECK(CoXlTestCompareOn() == 0 && fake_host_logged("are not the ones the compare knows"));
    Image("g16", 0x16, 8, 4, 2);
    CHECK(FileBytes("g16") == -1 && fake_host_logged("g16: 8 x 4, format 0x16: no known sample size, not read"));

    Reset("a preview's minimap: the record's CPU copies after 0x56", kExeBpt);
    CHECK(CoXlTestCompareOn() == 1);
    {
        uint8_t* level = calloc(1, 0x3000);
        uint8_t* record = calloc(1, 0x5000);
        uint16_t* heights = malloc(64 * 64 * 2);
        uint16_t* water = malloc(128 * 128 * 2);
        for (int i = 0; i < 64 * 64; i++) heights[i] = (uint16_t)(i * 3);
        for (int i = 0; i < 128 * 128; i++) water[i] = (uint16_t)(i ^ 0x5A5A);
        Readback h = {64, 64, 0x16, 0, (uint64_t)(uintptr_t)heights}, w = {128, 128, 0x16, 0, (uint64_t)(uintptr_t)water};
        memcpy(record + 0x4DE8, &h, sizeof h);
        memcpy(record + 0x4E08, &w, sizeof w);
        uint64_t r = (uint64_t)(uintptr_t)record;
        level[0x2D48] = 1; /* a preview */
        memcpy(level + 0x2D68, &r, 8);
        CoXlTestComparePass(0x56, (uintptr_t)level);
        CHECK(PipelineBytes("preview", "minimap-heights") == 64 * 64 * 2 && PipelineBytes("preview", "minimap-water") == 128 * 128 * 2);
        CHECK(fake_host_logged("preview minimap-heights (batch") && fake_host_logged(": 64 x 64, 2 B a sample (format 0x16)"));
        CHECK(fake_host_logged(": 128 x 128, 2 B a sample (format 0x16)"));

        memset(level + 0x2D68, 0, 8);
        CoXlTestComparePass(0x56, (uintptr_t)level);
        CHECK(fake_host_logged("coarse compare: no preview record at 0x56"));
        memcpy(level + 0x2D68, &r, 8);
        memset(record + 0x4E08, 0, sizeof w);
        CoXlTestComparePass(0x56, (uintptr_t)level);
        CHECK(fake_host_logged("coarse compare: preview minimap-water: no image"));
        free(level); free(record); free(heights); free(water);
    }

    Clean();
    RemoveDirectoryA(g_logs);

    if (g_failed) { printf("compare: %d checks failed\n", g_failed); return 1; }
    printf("compare: all checks passed\n");
    return 0;
}

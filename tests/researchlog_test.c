/* The prototype's research files (co_shim.h OpenResearchLog): a package build, neither research nor test, opens none
 * of them (about 90 MB a session: heightfields, plans, grade and road reports), while the log folder itself works. */
#undef CO_RESEARCH
#undef CO_TEST
#include <stdio.h>
#include <windows.h>

#include "harness/fake_host.h"
#define CO_XL_UNIT
#include "xl/co_shim.h"

static const char* Folder(HfMod* mod) { (void)mod; return "."; }

int main(void)
{
    int failed = 0;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    ((HfApi*)g_coApi)->logs_folder = Folder;
    DeleteFileW(L"researchlog-research.tsv"); /* a file an earlier run left */
    HANDLE h = co_open_log(L"researchlog-plain.txt"); /* the folder takes a file */
    if (h == INVALID_HANDLE_VALUE) { printf("FAILED: the log folder took no file\n"); failed++; }
    else CloseHandle(h);
    h = OpenResearchLog(L"researchlog-research.tsv");
    if (h != INVALID_HANDLE_VALUE) { printf("FAILED: a package build opened a research file\n"); failed++; CloseHandle(h); }
    if (GetFileAttributesW(L"researchlog-research.tsv") != INVALID_FILE_ATTRIBUTES) { printf("FAILED: the research file exists\n"); failed++; }
    DeleteFileW(L"researchlog-plain.txt");
    if (failed) { printf("researchlog: %d check(s) failed\n", failed); return 1; }
    printf("researchlog: all checks passed\n");
    return 0;
}

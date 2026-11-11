/* SafeRead runs thousands of times a frame (the living world's tick alone), and ReadProcessMemory is a system call
 * each time. Small reads are copied in place by HfSafeCopy instead; a fault inside it is turned into its failure
 * return by SafeReadFault. HELLFORGE_SAFEREAD=0 keeps the system call. */
int HfSafeCopy(void* to, const void* from, size_t size);
extern const char HfSafeCopyStart[], HfSafeCopyEnd[], HfSafeCopyFault[];
__asm__(".text\n.p2align 4\nHfSafeCopy:\n  testq %r8, %r8\n  jz 2f\nHfSafeCopyStart:\n1:\n  movb (%rdx), %al\n  movb %al, (%rcx)\n"
        "  incq %rdx\n  incq %rcx\n  decq %r8\n  jnz 1b\nHfSafeCopyEnd:\n2:\n  xorl %eax, %eax\n  ret\n"
        "HfSafeCopyFault:\n  movl $1, %eax\n  ret\n");
static volatile LONG g_safeCopyOn, g_safeCopyFaults;

/* 1 for a fault inside HfSafeCopy: the other vectored handlers that take faults on their own threads pass it on. */
static int SafeReadOwns(const EXCEPTION_POINTERS* info)
{
    uintptr_t at = (uintptr_t)info->ContextRecord->Rip;
    return at >= (uintptr_t)HfSafeCopyStart && at < (uintptr_t)HfSafeCopyEnd;
}

/* A guard page's fault has already taken the page's guard away; it is put back so the stack it belongs to still grows. */
static LONG CALLBACK SafeReadFault(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode, old;
    if (!SafeReadOwns(info)) return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_GUARD_PAGE_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR) return EXCEPTION_CONTINUE_SEARCH;
    if (code == STATUS_GUARD_PAGE_VIOLATION && info->ExceptionRecord->NumberParameters > 1)
    {
        MEMORY_BASIC_INFORMATION m;
        void* page = (void*)(info->ExceptionRecord->ExceptionInformation[1] & ~(ULONG_PTR)0xFFF);
        if (VirtualQuery(page, &m, sizeof m) && m.State == MEM_COMMIT) VirtualProtect(page, 0x1000, m.Protect | PAGE_GUARD, &old);
    }
    info->ContextRecord->Rip = (DWORD64)(uintptr_t)HfSafeCopyFault;
    InterlockedIncrement(&g_safeCopyFaults);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void InstallSafeRead(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_SAFEREAD", t, sizeof t) > 0 && t[0] == '0') return;
    if (AddVectoredExceptionHandler(1, SafeReadFault) != NULL) InterlockedExchange(&g_safeCopyOn, 1);
}

static int SafeRead(uintptr_t address, void* buffer, size_t size)
{
    SIZE_T read = 0;
    if (g_safeCopyOn && size <= 256)
        return address < 0x10000 || address > 0x7FFFFFFEFFFFull - size ? 1 : HfSafeCopy(buffer, (const void*)address, size);
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, buffer, size, &read) && read == size ? 0 : 1;
}

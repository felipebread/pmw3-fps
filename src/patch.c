#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include "MinHook.h"
#include "config.h"
#include "hook.h"

_Static_assert(sizeof(void *) == 4, "Build this patch for 32-bit x86.");

uint32_t g_targetBits;
volatile long g_hookSeen;
void *g_original;
static HMODULE g_module;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static wchar_t g_iniPath[32768], g_logPath[32768], g_exePath[32768];

enum { FRAME_RVA = 0x26ECC0, FRAME_FILE_OFFSET = 0x26E0C0,
       FRAME_SIZE = 0x178, EXPECTED_IMAGE_SIZE = 0x161E000 };
static const unsigned char kHash[32] = {
    0x4d,0xda,0x14,0xf6,0xf4,0x26,0x9b,0x55,
    0xe7,0x55,0xc8,0xde,0xbf,0x25,0x2e,0xe6,
    0x70,0x31,0x67,0x40,0x89,0x59,0xab,0xc2,
    0x64,0xbb,0xed,0x1f,0xbb,0x41,0x14,0xa9
};

static void Log(const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text) - 3, format, args);
    va_end(args);
    size_t length = strlen(text);
    memcpy(text + length, "\r\n", 3);
    OutputDebugStringA(text);
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_log, text, (DWORD)(length + 2), &written, NULL);
        FlushFileBuffers(g_log);
    }
}

static int ReadAt(HANDLE file, DWORD offset, void *buffer, DWORD length)
{
    LARGE_INTEGER position;
    position.QuadPart = offset;
    DWORD count = 0;
    return SetFilePointerEx(file, position, NULL, FILE_BEGIN) &&
           ReadFile(file, buffer, length, &count, NULL) && count == length;
}

/* Hash and instruction bytes come from the same handle; the EXE is never edited. */
static int VerifyDisk(const wchar_t *path, unsigned char expected[FRAME_SIZE])
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        Log("Cannot open executable for verification (Windows error %lu).", GetLastError());
        return 0;
    }
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR object = NULL;
    DWORD objectLength = 0, returned = 0, count;
    unsigned char digest[32], buffer[16384];
    int ok = 0;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart != 15388672) goto done;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectLength,
                          sizeof(objectLength), &returned, 0) < 0) goto done;
    object = HeapAlloc(GetProcessHeap(), 0, objectLength);
    if (!object) goto done;
    if (BCryptCreateHash(algorithm, &hash, object, objectLength, NULL, 0, 0) < 0) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, NULL)) goto done;
        if (!count) break;
        if (BCryptHashData(hash, buffer, count, 0) < 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto done;
    if (memcmp(digest, kHash, sizeof(digest))) goto done;
    ok = ReadAt(file, FRAME_FILE_OFFSET, expected, FRAME_SIZE);
done:
    if (hash) BCryptDestroyHash(hash);
    if (object) HeapFree(GetProcessHeap(), 0, object);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) Log("Executable verification failed. Only the analyzed SHA-256 is supported; no hook installed.");
    return ok;
}

static int ReadMemory(const void *address, void *buffer, SIZE_T size)
{
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, buffer, size, &read) && read == size;
}

static void *VerifyMemory(const unsigned char expected[FRAME_SIZE])
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    unsigned char actual[FRAME_SIZE];
    /* This particular game build has a fixed preferred base. Refuse relocation
       rather than comparing absolute-address operands against the wrong bytes. */
    if ((uintptr_t)base != 0x400000 ||
        !ReadMemory(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !ReadMemory(base + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage != EXPECTED_IMAGE_SIZE ||
        !ReadMemory(base + FRAME_RVA, actual, sizeof(actual)) ||
        memcmp(actual, expected, sizeof(actual))) {
        Log("Runtime timing routine does not match the verified EXE (or is already modified). No hook installed.");
        return NULL;
    }
    return base + FRAME_RVA;
}

static DWORD WINAPI Initialize(void *unused)
{
    (void)unused;
    DWORD length = GetModuleFileNameW(g_module, g_iniPath, 32768);
    if (!length || length >= 32760) return 0;
    wchar_t *slash = wcsrchr(g_iniPath, L'\\');
    wchar_t *dot = wcsrchr(slash ? slash + 1 : g_iniPath, L'.');
    if (!dot) dot = g_iniPath + length;
    wcscpy(dot, L".ini");
    wcscpy(g_logPath, g_iniPath);
    wcscpy(g_logPath + (dot - g_iniPath), L".log");
    g_log = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Log("PacManWorld3FPSFix 1.0.0 (x86) starting.");
    if (g_log == INVALID_HANDLE_VALUE) Log("Cannot create log beside ASI; messages are available through OutputDebugString.");
    wchar_t text[128];
    GetPrivateProfileStringW(L"FPS", L"Enabled", L"1", text, 128, g_iniPath);
    if (!wcscmp(text, L"0")) { Log("Disabled by INI. No hook installed."); goto done; }
    if (wcscmp(text, L"1")) { Log("Invalid Enabled setting; use 0 or 1. No hook installed."); goto done; }
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES)
        Log("INI not found; using default TargetFPS=144. Place the supplied INI beside this ASI.");
    GetPrivateProfileStringW(L"FPS", L"TargetFPS", L"144", text, 128, g_iniPath);
    float target;
    if (!ParseTarget(text, &target)) {
        Log("Invalid TargetFPS; enter a number from 1 to 1000, using a dot for decimals. No hook installed.");
        goto done;
    }
    memcpy(&g_targetBits, &target, sizeof(target));
    Log("Configured target: %.6g FPS. INI changes take effect after restarting the game.", (double)target);
    unsigned char expected[FRAME_SIZE];
    length = GetModuleFileNameW(NULL, g_exePath, 32768);
    if (!length || length >= 32768) { Log("Cannot resolve executable path."); goto done; }
    if (!VerifyDisk(g_exePath, expected)) goto done;
    void *address = VerifyMemory(expected);
    if (!address) goto done;
    /* The installed jump must never outlive the DLL containing its destination. */
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)&FrameHook, &pinned)) {
        Log("Cannot pin ASI in memory. No hook installed."); goto done;
    }
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) { Log("MinHook initialization failed: %s", MH_StatusToString(status)); goto done; }
    status = MH_CreateHook(address, (LPVOID)(uintptr_t)&FrameHook, &g_original);
    if (status == MH_OK) status = MH_EnableHook(address);
    if (status != MH_OK) {
        Log("Hook installation failed: %s", MH_StatusToString(status));
        MH_Uninitialize();
        goto done;
    }
    Log("Hook installed at game RVA 0x26ECC0. Waiting for the first frame-timing call.");
    for (unsigned i = 0; i < 300; ++i) {
        if (InterlockedCompareExchange(&g_hookSeen, 0, 0)) {
            Log("Frame hook executed: configured FPS target applied successfully.");
            goto done;
        }
        Sleep(100);
    }
    Log("No frame call observed in the first 30 seconds. Hook remains installed; the game may still be starting.");
done:
    if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(NULL, 0, Initialize, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
        else OutputDebugStringA("PacManWorld3FPSFix: failed to create initialization thread.\n");
    }
    /* No waits or hook-library operations under the loader lock. The module is
       pinned before activation; Windows reclaims it with the game process. */
    return TRUE;
}

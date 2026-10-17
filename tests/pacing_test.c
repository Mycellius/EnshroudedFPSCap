/* Loads EnshroudedBaseCap.dll and a stand-in sl.interposer.dll from this
   executable's folder, then presents frames the way a game would and checks
   that only the real-frame rate is paced:
     1. a game that could run at 200 fps is held at the cap (40);
     2. a game slower than the cap is never held back;
     3. every call still reaches the original vkQueuePresentKHR.
   Exit code 0 = pass. */
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t(__stdcall* PFN_present)(void*, const void*);

static LONGLONG g_freq;
static LONG g_presented;

static LONGLONG Now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static int LogContains(const wchar_t* dir, const char* needle) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsEnshroudedBaseCap.log", dir);
    FILE* f = _wfopen(path, L"r");
    if (!f) return 0;
    char line[512];
    int hit = 0;
    while (fgets(line, sizeof line, f))
        if (strstr(line, needle)) hit = 1;
    fclose(f);
    return hit;
}

/* Presents for `seconds`, spinning `workMs` before each present to stand in for
   rendering. Returns the achieved rate; reports interval statistics. */
static double Run(PFN_present present, double seconds, double workMs, const char* label) {
    static LONGLONG ts[20000];
    int n = 0;
    const LONGLONG end = Now() + (LONGLONG)(seconds * g_freq);
    while (Now() < end && n < 20000) {
        const LONGLONG work = Now() + (LONGLONG)(workMs * g_freq / 1000.0);
        while (Now() < work) {
        }
        present(NULL, NULL);
        g_presented++;
        ts[n++] = Now();
    }
    double sum = 0, sum2 = 0, lo = 1e9, hi = 0;
    for (int i = 1; i < n; i++) {
        const double d = (double)(ts[i] - ts[i - 1]) * 1000.0 / g_freq;
        sum += d;
        sum2 += d * d;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    const int k = n - 1;
    const double mean = sum / k;
    const double sd = sqrt(fmax(0.0, sum2 / k - mean * mean));
    const double rate = k / ((double)(ts[n - 1] - ts[0]) / g_freq);
    printf("%-34s %5.2f fps  interval mean %.3f ms, sd %.3f, min %.3f, max %.3f\n", label, rate, mean, sd, lo, hi);
    return rate;
}

int main(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;

    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wcsrchr(dir, L'\\')[1] = 0;

    /* A known config, independent of whatever ini sits next to the build. */
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%lsEnshroudedBaseCap.ini", dir);
    FILE* cfg = _wfopen(ini, L"w");
    if (!cfg) {
        printf("FAIL cannot write %ls\n", ini);
        return 1;
    }
    fputs("[EnshroudedBaseCap]\nBaseFpsCap=40\nShowReadout=0\n", cfg);
    fclose(cfg);

    if (!LoadLibraryW(L"EnshroudedBaseCap.dll")) {
        printf("FAIL loading EnshroudedBaseCap.dll (error %lu)\n", GetLastError());
        return 1;
    }
    Sleep(300); /* the add-on is loaded before Streamline, as in the game */
    HMODULE sl = LoadLibraryW(L"sl.interposer.dll");
    if (!sl) {
        printf("FAIL loading the stand-in sl.interposer.dll (error %lu)\n", GetLastError());
        return 1;
    }
    PFN_present present = (PFN_present)GetProcAddress(sl, "vkQueuePresentKHR");
    volatile LONG* calls = (volatile LONG*)GetProcAddress(sl, "g_calls");

    for (int i = 0; i < 500 && !LogContains(dir, "hooked sl.interposer.dll"); i++) Sleep(10);
    if (!LogContains(dir, "hooked sl.interposer.dll")) {
        printf("FAIL the add-on did not report hooking vkQueuePresentKHR\n");
        return 1;
    }

    int failures = 0;
    const LONG before = *calls;

    const double capped = Run(present, 4.0, 5.0, "cap 40, game able to do 200 fps:");
    if (capped < 38.8 || capped > 40.4) {
        printf("FAIL expected about 40 fps\n");
        failures++;
    }
    const double slow = Run(present, 2.0, 30.0, "cap 40, game only able to do 33 fps:");
    if (slow < 31.5) {
        printf("FAIL a game slower than the cap was held back\n");
        failures++;
    }
    if (*calls - before != g_presented) {
        printf("FAIL %ld of %ld presents reached the original function\n", *calls - before, g_presented);
        failures++;
    }

    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}

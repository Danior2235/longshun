/*
 * common.c - 通用基础库实现
 */
#include "common.h"

/* ---------------- 线程 ---------------- */

#if LS_WINDOWS

typedef struct {
    ls_thread_fn fn;
    void* arg;
} ls_thread_args;

static unsigned __stdcall ls_thread_entry(void* p)
{
    ls_thread_args* a = (ls_thread_args*)p;
    ls_thread_fn fn = a->fn;
    void* arg = a->arg;
    free(a);
    fn(arg);
    return 0;
}

int ls_thread_create(LS_THREAD* t, ls_thread_fn fn, void* arg)
{
    ls_thread_args* a = (ls_thread_args*)malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = fn;
    a->arg = arg;
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, ls_thread_entry, a, 0, NULL);
    if (!h) { free(a); return -1; }
    *t = h;
    return 0;
}

int ls_thread_join(LS_THREAD t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
}

void ls_thread_yield(void) { SwitchToThread(); }

void ls_sleep_ms(int ms) { Sleep((DWORD)ms); }

#else /* POSIX */

#include <sched.h>

typedef struct {
    ls_thread_fn fn;
    void* arg;
} ls_thread_args;

static void* ls_thread_entry(void* p)
{
    ls_thread_args* a = (ls_thread_args*)p;
    ls_thread_fn fn = a->fn;
    void* arg = a->arg;
    free(a);
    fn(arg);
    return NULL;
}

int ls_thread_create(LS_THREAD* t, ls_thread_fn fn, void* arg)
{
    ls_thread_args* a = (ls_thread_args*)malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = fn;
    a->arg = arg;
    if (pthread_create(t, NULL, ls_thread_entry, a) != 0) {
        free(a);
        return -1;
    }
    return 0;
}

int ls_thread_join(LS_THREAD t)
{
    pthread_join(t, NULL);
    return 0;
}

void ls_thread_yield(void) { sched_yield(); }

void ls_sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#endif

/* ---------------- 时间 ---------------- */

int64_t ls_now_ms(void)
{
#if LS_WINDOWS
    FILETIME ft;
    ULARGE_INTEGER ul;
    GetSystemTimeAsFileTime(&ft);
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    /* 从 1601-01-01 到 1970-01-01 的 100ns 间隔数 */
    return (int64_t)((ul.QuadPart - 116444736000000000ULL) / 10000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void ls_time_str(char* buf, size_t n)
{
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    if (!tm) { ls_safe_copy(buf, n, "1970-01-01 00:00:00"); return; }
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", tm);
}

void ls_time_str_ms(int64_t ms, char* buf, size_t n)
{
    time_t sec = (time_t)(ms / 1000);
    struct tm* tm = localtime(&sec);
    if (!tm) { ls_safe_copy(buf, n, "1970-01-01 00:00:00"); return; }
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", tm);
}

/* ---------------- 字符串 ---------------- */

char* ls_strdup(const char* s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char* ls_trim(char* s)
{
    if (!s) return s;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    return s;
}

void ls_to_lower(char* s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

int ls_strnicmp(const char* a, const char* b, size_t n)
{
#if LS_WINDOWS
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

int ls_str_icontains(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !*needle) return 1;
    size_t nl = strlen(needle);
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)needle[0])) {
            if (ls_strnicmp(haystack, needle, nl) == 0) return 1;
        }
    }
    return 0;
}

int ls_istarts_with(const char* s, const char* prefix)
{
    if (!s || !prefix) return 0;
    return ls_strnicmp(s, prefix, strlen(prefix)) == 0;
}

void ls_safe_copy(char* dst, size_t n, const char* src)
{
    if (!dst || n == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

void ls_gen_id(char* out, size_t n)
{
    if (n < 2) return;
    int64_t ms = ls_now_ms();
    unsigned r = (unsigned)(ms & 0x7fffffff);
    r ^= (unsigned)(ms >> 32) ^ ((unsigned)rand() << 16) ^ (unsigned)rand();
    snprintf(out, n, "%08x%08x", (unsigned)(ms & 0xffffffffu),
             (unsigned)(r ^ ((ms * 2654435761u) >> 32)));
    out[n - 1] = '\0';
}

/* ---------------- 随机 ---------------- */

int ls_rand_int(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(rand() % (unsigned)(hi - lo + 1));
}

/*
 * log.c - 分级日志系统实现
 * 输出格式: [YYYY-MM-DD HH:MM:SS] [LEVEL] message (file:line)
 */
#include "log.h"

#if LS_WINDOWS
    #include <direct.h>
#else
    #include <sys/stat.h>
#endif

/* 确保日志目录存在 */
static void ensure_dir(const char* path)
{
    if (!path || !path[0]) return;
#if LS_WINDOWS
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static LS_MUTEX g_lock;
static ls_log_level g_level = LS_LOG_INFO;
static char g_dir[512] = {0};
static FILE* g_file = NULL;
static int g_inited = 0;

const char* ls_log_level_name(ls_log_level level)
{
    switch (level) {
        case LS_LOG_DEBUG: return "DEBUG";
        case LS_LOG_INFO:  return "INFO";
        case LS_LOG_WARN:  return "WARN";
        case LS_LOG_ERROR: return "ERROR";
        default: return "?";
    }
}

void ls_log_init(const char* dir, ls_log_level level)
{
    if (!g_inited) {
        ls_mutex_init(&g_lock);
        g_inited = 1;
    }
    g_level = level;
    ls_safe_copy(g_dir, sizeof(g_dir), dir ? dir : "data/logs");
    ensure_dir(g_dir);

    ls_mutex_lock(&g_lock);
    if (g_file) fclose(g_file);
    g_file = NULL;

    char path[640];
    char ts[32];
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    if (tm) strftime(ts, sizeof(ts), "%Y%m%d", tm); else ls_safe_copy(ts, sizeof(ts), "unknown");

    if (g_dir[0]) {
        snprintf(path, sizeof(path), "%s/app_%s.log", g_dir, ts);
        g_file = fopen(path, "a");
    }
    ls_mutex_unlock(&g_lock);
}

void ls_log_set_level(ls_log_level level) { g_level = level; }

void ls_log_write(ls_log_level level, const char* file, int line,
                  const char* fmt, ...)
{
    if (level < g_level) return;

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char ts[32];
    ls_time_str(ts, sizeof(ts));

    const char* base = file ? strrchr(file, '/') : NULL;
    if (!base) base = file ? strrchr(file, '\\') : NULL;
    const char* short_file = (base && base[1]) ? base + 1 : (file ? file : "");

    char line_out[2300];
    snprintf(line_out, sizeof(line_out), "[%s] [%s] %s (%s:%d)",
             ts, ls_log_level_name(level), msg, short_file, line);

    ls_mutex_lock(&g_lock);
    printf("%s\n", line_out);
    if (g_file) fprintf(g_file, "%s\n", line_out);
    if (g_file) fflush(g_file);
    ls_mutex_unlock(&g_lock);
}

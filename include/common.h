/*
 * common.h - 龙顺 AI 销售与客户运营助手
 * 通用基础库:平台抽象、线程、时间与字符串工具
 */
#ifndef LS_COMMON_H
#define LS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

/* ---------------- 平台宏 ---------------- */
#if defined(_WIN32) || defined(_WIN64)
    #define LS_WINDOWS 1
#else
    #define LS_WINDOWS 0
#endif

#if LS_WINDOWS
    #define LS_INLINE __inline
#else
    #define LS_INLINE inline
#endif

/* ---------------- 线程/互斥锁抽象 ---------------- */
#if LS_WINDOWS
    #include <winsock2.h>   /* 必须先于 windows.h 包含,避免宏冲突 */
    #include <windows.h>
    #include <process.h>
    #define LS_THREAD HANDLE
    #define LS_MUTEX CRITICAL_SECTION
    #define ls_mutex_init(m)   InitializeCriticalSection((m))
    #define ls_mutex_lock(m)   EnterCriticalSection((m))
    #define ls_mutex_unlock(m) LeaveCriticalSection((m))
    #define ls_mutex_destroy(m) DeleteCriticalSection((m))
#else
    #include <pthread.h>
    #define LS_THREAD pthread_t
    #define LS_MUTEX pthread_mutex_t
    #define ls_mutex_init(m)   pthread_mutex_init((m), NULL)
    #define ls_mutex_lock(m)   pthread_mutex_lock((m))
    #define ls_mutex_unlock(m) pthread_mutex_unlock((m))
    #define ls_mutex_destroy(m) pthread_mutex_destroy((m))
#endif

/* 线程创建/等待的跨平台封装 */
typedef void* (*ls_thread_fn)(void* arg);
int ls_thread_create(LS_THREAD* t, ls_thread_fn fn, void* arg);
int ls_thread_join(LS_THREAD t);

/* 让出 CPU */
void ls_thread_yield(void);
void ls_sleep_ms(int ms);

/* ---------------- 时间工具 ---------------- */
/* 当前毫秒时间戳 */
int64_t ls_now_ms(void);
/* 格式化当前时间为 "YYYY-MM-DD HH:MM:SS" */
void ls_time_str(char* buf, size_t n);
/* 将毫秒时间戳格式化为可读字符串 */
void ls_time_str_ms(int64_t ms, char* buf, size_t n);

/* ---------------- 字符串工具 ---------------- */
/* 安全 strdup */
char* ls_strdup(const char* s);
/* 去除首尾空白(原地),返回原指针 */
char* ls_trim(char* s);
/* 转小写(原地) */
void ls_to_lower(char* s);
/* 不区分大小写的 n 字符比较(平台无关) */
int ls_strnicmp(const char* a, const char* b, size_t n);
/* 不区分大小写的子串包含判断 */
int ls_str_icontains(const char* haystack, const char* needle);
/* 不区分大小写的前缀判断 */
int ls_istarts_with(const char* s, const char* prefix);
/* 安全拷贝 */
void ls_safe_copy(char* dst, size_t n, const char* src);
/* 生成 32 位十六进制 id(基于时间与随机数) */
void ls_gen_id(char* out, size_t n);

/* ---------------- 随机工具 ---------------- */
int ls_rand_int(int lo, int hi);

#endif /* LS_COMMON_H */

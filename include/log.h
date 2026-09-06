/*
 * log.h - 分级日志系统(问题追溯的底层支撑)
 * 日志同时输出到控制台与 data/logs/ 目录下的文件,线程安全。
 */
#ifndef LS_LOG_H
#define LS_LOG_H

#include "common.h"

typedef enum {
    LS_LOG_DEBUG = 0,
    LS_LOG_INFO,
    LS_LOG_WARN,
    LS_LOG_ERROR
} ls_log_level;

/* 初始化日志系统:dir 为日志目录,level 为最低输出级别 */
void ls_log_init(const char* dir, ls_log_level level);
void ls_log_set_level(ls_log_level level);
const char* ls_log_level_name(ls_log_level level);

/* 核心写日志函数 */
void ls_log_write(ls_log_level level, const char* file, int line,
                  const char* fmt, ...);

#define LOG_DEBUG(...) ls_log_write(LS_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  ls_log_write(LS_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  ls_log_write(LS_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) ls_log_write(LS_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* LS_LOG_H */

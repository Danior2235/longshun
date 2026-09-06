/*
 * json.h - 零依赖轻量级 JSON 解析/序列化库
 * 支持 null / bool / number / string / array / object,
 * 供数据清洗、看板接口、存储层共同使用。
 */
#ifndef LS_JSON_H
#define LS_JSON_H

#include "common.h"

typedef enum {
    LS_JSON_NULL = 0,
    LS_JSON_BOOL,
    LS_JSON_NUMBER,
    LS_JSON_STRING,
    LS_JSON_ARRAY,
    LS_JSON_OBJECT
} ls_json_type;

typedef struct ls_json ls_json;

typedef struct {
    char* key;
    ls_json* value;
} ls_json_pair;

struct ls_json {
    ls_json_type type;
    bool boolean;       /* type == BOOL   */
    double number;      /* type == NUMBER */
    char* string;       /* type == STRING */
    ls_json_pair* items;/* type == ARRAY/OBJECT */
    int count;          /* 元素个数 */
    int capacity;       /* 容量 */
};

/* ---------- 解析 ---------- */
/* 解析 JSON 文本,失败返回 NULL(调用方可打印日志) */
ls_json* ls_json_parse(const char* text);

/* ---------- 构造 ---------- */
ls_json* ls_json_object(void);
ls_json* ls_json_array(void);
ls_json* ls_json_string(const char* s);
ls_json* ls_json_number(double v);
ls_json* ls_json_bool(bool b);
ls_json* ls_json_null(void);

/* ---------- 释放 ---------- */
void ls_json_free(ls_json* j);

/* ---------- 对象操作 ---------- */
void ls_json_set(ls_json* obj, const char* key, ls_json* value);
ls_json* ls_json_get(const ls_json* obj, const char* key);

/* ---------- 数组操作 ---------- */
void ls_json_array_add(ls_json* arr, ls_json* value);
int ls_json_array_size(const ls_json* arr);
ls_json* ls_json_array_get(const ls_json* arr, int idx);

/* ---------- 序列化 ---------- */
char* ls_json_dump(const ls_json* j);        /* 紧凑格式 */
char* ls_json_dump_pretty(const ls_json* j); /* 美化格式(2空格缩进) */

/* ---------- 便捷取值 ---------- */
const char* ls_json_get_string(const ls_json* obj, const char* key, const char* def);
double ls_json_get_number(const ls_json* obj, const char* key, double def);
int ls_json_get_bool(const ls_json* obj, const char* key, int def);

#endif /* LS_JSON_H */

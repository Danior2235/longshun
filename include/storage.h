/*
 * storage.h - 基于 JSON 文件的持久化存储层
 * 业务数据(订单/房态/线索/任务)以 JSON 文件保存在 data/ 目录,便于上云与追溯。
 */
#ifndef LS_STORAGE_H
#define LS_STORAGE_H

#include "common.h"
#include "json.h"

/* 加载 JSON 文件,失败返回 NULL */
ls_json* ls_storage_load(const char* path);
/* 保存 JSON 到文件(美化格式),返回 0 成功 */
int ls_storage_save(const char* path, ls_json* j);
/* 读取文件原始文本(内存),调用方 free */
char* ls_storage_read_text(const char* path);

/* 向 JSON 数组文件追加一个元素(自动创建数组),返回 0 成功 */
int ls_storage_append_item(const char* path, ls_json* item);
/* 统计 JSON 数组文件元素个数,失败返回 -1 */
int ls_storage_count(const char* path);

#endif /* LS_STORAGE_H */

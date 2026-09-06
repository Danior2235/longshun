/*
 * synonym.h - 同义词/近义词匹配
 *
 * 目的: 让对话理解"换个说法"。例如:
 *   "我要一间单人床" / "订个大床房" / "开一间大床"  -> 都识别为 大床房
 *   "不含早" 不会被误判成 "含早"(否定前缀识别)
 *
 * 词表来源: <data_dir>/config/synonyms.json(可选, 运营可维护);
 * 文件缺失或非法时自动回退内置默认表, 保证功能始终可用。
 */
#ifndef LS_SYNONYM_H
#define LS_SYNONYM_H

#include <stddef.h>

/* 加载词表。data_dir 为数据目录, 可为 NULL(仅用内置表) */
void syn_init(const char* data_dir);

/* 释放词表 */
void syn_shutdown(void);

/* 在分组中查找首个命中的标准值, 拷贝至 out; 命中返回 1, 否则返回 0
 * 例: syn_pick("我要一间单人床", "room_type", out, sizeof(out)) -> out = "大床房" */
int syn_pick(const char* q, const char* section, char* out, size_t outsz);

/* 判断 q 是否命中分组下指定标准值的同义词 */
int syn_has(const char* q, const char* section, const char* key);

/* 判断 q 是否命中分组下任意一项(用于否定词等扁平词组) */
int syn_has_any(const char* q, const char* section);

/* 提取数量: 匹配 "数字 + 单位(含同义词)", 取最大值; 未找到返回 -1
 * 例: syn_qty("住2个晚上", "unit", "nights") -> 2 */
int syn_qty(const char* q, const char* section, const char* key);

#endif /* LS_SYNONYM_H */

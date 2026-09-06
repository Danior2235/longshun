/*
 * crawler.h - 多线程线索爬虫
 * 对应需求: 模块一「客户线索获取」—— AI 爬取招标网站与行业公开信息源,
 * 清洗后输出结构化潜在客户线索清单(联系方式、需求线索、匹配业态)。
 *
 * 支持两种数据来源:
 *  - demo_mode = 0: 通过 HTTP 抓取真实网页/JSON 接口(需网络)
 *  - demo_mode = 1: 从本地 data/demo_pages/ 读取模拟页面,便于离线演示
 */
#ifndef LS_CRAWLER_H
#define LS_CRAWLER_H

#include "common.h"
#include "json.h"

typedef struct {
    char url[256];        /* 信息源地址(HTTP URL 或本地文件路径) */
    char name[128];       /* 信息源名称,如"招标网-政府采购" */
    char kind[16];        /* webpage / json / file */
    int freq_min;         /* 抓取频率(分钟),0 表示一次性 */
    int enabled;
} ls_seed;

typedef struct {
    ls_seed* seeds;
    int seed_count;
    int thread_count;     /* 并发线程数,默认 4 */
    int max_items;        /* 单次采集最多输出线索数,默认 50 */
    int demo_mode;        /* 1 表示读取本地 demo 页面 */
    int sleep_ms;         /* 每次抓取间的礼貌延时(合规节流) */
    const char* out_path; /* 输出线索文件路径 */
} ls_crawler_cfg;

/* 从 JSON 文件加载信息源清单,返回数量;失败返回 -1 */
int ls_crawler_load_seeds(const char* path, ls_seed** out, int* count);
void ls_crawler_free_seeds(ls_seed* seeds, int count);

/* 运行爬虫,结果写入 cfg->out_path(JSON 数组)。返回产出线索数 */
int ls_crawler_run(const ls_crawler_cfg* cfg);

#endif /* LS_CRAWLER_H */

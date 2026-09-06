/*
 * lead.h - 客户线索数据模型
 * 对应需求: 模块一「客户线索获取」与「商机转化」
 * 爬虫清洗后输出结构化线索;评分模型评估成交概率并给出沟通建议。
 */
#ifndef LS_LEAD_H
#define LS_LEAD_H

#include "common.h"
#include "json.h"

/* 业态类型(与任务分派模块对齐) */
typedef enum {
    BIZ_HOTEL = 0,      /* 酒店 */
    BIZ_CATERING,       /* 餐饮 */
    BIZ_MEETING,        /* 会议 */
    BIZ_OTHER
} ls_biz_type;

const char* ls_biz_type_name(ls_biz_type t);
/* 根据文本识别业态 */
ls_biz_type ls_biz_type_detect(const char* text);

typedef struct {
    char id[32];
    char company[128];      /* 公司名 */
    char contact[64];       /* 联系人 */
    char phone[32];         /* 电话 */
    char email[64];
    char needs[512];        /* 需求关键词/描述 */
    ls_biz_type biz_type;   /* 匹配业态 */
    char source[64];        /* 信息源名称 */
    char source_url[256];
    int score;              /* 成交概率 0-100 */
    char advice[256];       /* 沟通建议 */
    char created_at[32];
} ls_lead;

/* 从原始 JSON 记录构建线索(原始记录来自爬虫清洗结果) */
int ls_lead_from_raw(ls_lead* lead, ls_json* raw);
/* 线索转 JSON(用于持久化与看板接口) */
ls_json* ls_lead_to_json(const ls_lead* lead);
void ls_lead_print(const ls_lead* lead);

/* 商机评分:基于需求命中度、联系方式完整度、业态匹配度 */
int ls_lead_score(const ls_lead* lead);
/* 沟通建议:推荐触达方式(电话/微信/邮件)与最佳时机 */
void ls_lead_advice(const ls_lead* lead, char* out, size_t n);

/* 按评分排序线索数组(降序) */
void ls_lead_sort_by_score(ls_lead* leads, int count);

#endif /* LS_LEAD_H */

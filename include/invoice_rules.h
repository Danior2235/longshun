/*
 * invoice_rules.h - 开票支持的税务规则引擎
 * 对应需求: 模块三「开票支持」—— 根据需求与税务规则选择合适的营业执照签约,
 * 规避税务风险。
 */
#ifndef LS_INVOICE_RULES_H
#define LS_INVOICE_RULES_H

#include "common.h"

typedef struct {
    char company[64];          /* 营业执照主体 */
    char taxpayer_type[16];    /* general=一般纳税人 / small=小规模 */
    char industry[32];         /* hotel / catering / meeting / service / goods */
    double rate;               /* 适用增值税率(如 0.06 表示 6%) */
    double annual_limit;       /* 小规模年开票上限(万元),0 表示不限制 */
    int enabled;
} ls_license;

typedef struct {
    const ls_license* license; /* 推荐执照 */
    double tax_amount;         /* 预计税额 */
    double rate;               /* 适用税率 */
    char reason[256];          /* 推荐理由 */
    char risk[256];            /* 风险提示,空串表示无 */
} ls_invoice_advice;

/* 从 JSON 文件加载营业执照清单 */
int ls_invoice_load_licenses(const char* path, ls_license** out, int* count);
void ls_invoice_free_licenses(ls_license* arr, int count);

/* 推荐开票主体。service_type: hotel/catering/meeting/service/goods */
int ls_invoice_recommend(const ls_license* licenses, int count,
                         const char* service_type, double amount,
                         const char* customer_note, /* 客户要求(可空) */
                         ls_invoice_advice* advice);

void ls_invoice_print_advice(const ls_invoice_advice* a);

#endif /* LS_INVOICE_RULES_H */

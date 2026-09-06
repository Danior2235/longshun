/*
 * invoice_rules.c - 开票支持的税务规则引擎实现
 * 规则要点:
 *  1. 优先选择与服务类型匹配行业、税率最低且无风险的执照;
 *  2. 小规模纳税人年累计开票接近/超过 500 万 → 强制转一般纳税人风险;
 *  3. 行业不匹配 → 超范围开票风险提示。
 */
#include "invoice_rules.h"
#include "storage.h"
#include "log.h"

int ls_invoice_load_licenses(const char* path, ls_license** out, int* count)
{
    ls_json* root = ls_storage_load(path);
    if (!root || root->type != LS_JSON_ARRAY) {
        if (root) ls_json_free(root);
        return -1;
    }
    int n = ls_json_array_size(root);
    ls_license* arr = (ls_license*)calloc((size_t)(n ? n : 1), sizeof(ls_license));
    if (!arr) { ls_json_free(root); return -1; }
    int i;
    for (i = 0; i < n; i++) {
        ls_json* item = ls_json_array_get(root, i);
        ls_safe_copy(arr[i].company, sizeof(arr[i].company),
                     ls_json_get_string(item, "company", ""));
        ls_safe_copy(arr[i].taxpayer_type, sizeof(arr[i].taxpayer_type),
                     ls_json_get_string(item, "taxpayer_type", "small"));
        ls_safe_copy(arr[i].industry, sizeof(arr[i].industry),
                     ls_json_get_string(item, "industry", "service"));
        arr[i].rate = ls_json_get_number(item, "rate", 0.01);
        arr[i].annual_limit = ls_json_get_number(item, "annual_limit", 0);
        arr[i].enabled = ls_json_get_bool(item, "enabled", 1);
    }
    ls_json_free(root);
    *out = arr;
    *count = n;
    return n;
}

void ls_invoice_free_licenses(ls_license* arr, int count)
{
    (void)count;
    free(arr);
}

/* 行业匹配打分: 2=完全匹配, 1=相关, 0=无关 */
static int industry_match(const char* lic_industry, const char* service_type)
{
    if (strcmp(lic_industry, service_type) == 0) return 2;
    /* 关联规则 */
    if (strcmp(service_type, "service") == 0 &&
        (strcmp(lic_industry, "meeting") == 0 ||
         strcmp(lic_industry, "hotel") == 0 ||
         strcmp(lic_industry, "catering") == 0))
        return 1;
    if (strcmp(lic_industry, "service") == 0 &&
        (strcmp(service_type, "meeting") == 0 ||
         strcmp(service_type, "hotel") == 0 ||
         strcmp(service_type, "catering") == 0))
        return 1;
    return 0;
}

static int is_small(const ls_license* lic)
{
    return strcmp(lic->taxpayer_type, "small") == 0;
}

int ls_invoice_recommend(const ls_license* licenses, int count,
                         const char* service_type, double amount,
                         const char* customer_note,
                         ls_invoice_advice* advice)
{
    if (!licenses || count <= 0 || !advice) return -1;
    memset(advice, 0, sizeof(*advice));

    const ls_license* best = NULL;
    int best_score = -1;

    int i;
    for (i = 0; i < count; i++) {
        const ls_license* lic = &licenses[i];
        if (!lic->enabled) continue;

        int score = 0;
        score += industry_match(lic->industry, service_type) * 10;
        score += (int)((1.0 - lic->rate) * 30);      /* 税率越低越好 */
        if (is_small(lic)) score += 3;               /* 小规模税负通常更低 */

        /* 客户要求一般纳税人/专票 */
        if (customer_note && strstr(customer_note, "专票") && !is_small(lic))
            score += 8;
        if (customer_note && strstr(customer_note, "一般纳税人") && !is_small(lic))
            score += 8;

        /* 小规模超限风险: 年开票累计接近上限 */
        if (is_small(lic) && lic->annual_limit > 0 && amount > 0) {
            double used = lic->annual_limit * 0.8; /* 假设已用 80% */
            if (used + amount / 10000.0 > lic->annual_limit) {
                score -= 40; /* 超限风险,大幅降权 */
            }
        }

        if (score > best_score) {
            best_score = score;
            best = lic;
        }
    }

    if (!best) {
        ls_safe_copy(advice->reason, sizeof(advice->reason),
                     "无可用营业执照主体");
        return -1;
    }

    advice->license = best;
    advice->rate = best->rate;
    advice->tax_amount = amount * best->rate;

    /* 组装理由 */
    char buf[256];
    snprintf(buf, sizeof(buf),
             "推荐使用「%s」签约开票: %s纳税人,适用 %s 行业税率 %.1f%%,"
             "预计税额 %.2f 元。",
             best->company,
             is_small(best) ? "小规模" : "一般",
             best->industry, best->rate * 100.0, advice->tax_amount);
    ls_safe_copy(advice->reason, sizeof(advice->reason), buf);

    /* 风险检查 */
    if (industry_match(best->industry, service_type) == 0) {
        snprintf(buf, sizeof(buf),
                 "风险: 主体「%s」经营范围为 %s,与本次 %s 业务不匹配,"
                 "超范围开票存在税务风险,建议确认经营范围后调整。",
                 best->company, best->industry, service_type);
        ls_safe_copy(advice->risk, sizeof(advice->risk), buf);
    } else if (is_small(best) && best->annual_limit > 0 && amount > 0) {
        double used = best->annual_limit * 0.8;
        if (used + amount / 10000.0 > best->annual_limit) {
            snprintf(buf, sizeof(buf),
                     "风险: 小规模纳税人年开票累计接近上限 %.0f 万元,"
                     "本次开票后可能触发强制转一般纳税人,请评估。",
                     best->annual_limit);
            ls_safe_copy(advice->risk, sizeof(advice->risk), buf);
        }
    }
    return 0;
}

void ls_invoice_print_advice(const ls_invoice_advice* a)
{
    if (!a || !a->license) {
        printf("  (无可用建议)\n");
        return;
    }
    printf("  推荐主体: %s (%s纳税人, %s 行业)\n",
           a->license->company, a->license->taxpayer_type, a->license->industry);
    printf("  适用税率: %.1f%% | 预计税额: %.2f 元\n",
           a->rate * 100.0, a->tax_amount);
    printf("  理由: %s\n", a->reason);
    if (a->risk[0])
        printf("  风险: %s\n", a->risk);
    else
        printf("  风险: 无\n");
}

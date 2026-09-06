/*
 * lead.c - 客户线索数据模型与商机评分实现
 */
#include "lead.h"

const char* ls_biz_type_name(ls_biz_type t)
{
    switch (t) {
        case BIZ_HOTEL:    return "酒店";
        case BIZ_CATERING: return "餐饮";
        case BIZ_MEETING:  return "会议";
        default:           return "其他";
    }
}

ls_biz_type ls_biz_type_detect(const char* text)
{
    if (!text) return BIZ_OTHER;
    int hotel = 0, catering = 0, meeting = 0;
    const char* keywords_hotel[] = {
        "酒店", "宾馆", "客房", "民宿", "房间", "住宿", "前台", "OTA"
    };
    const char* keywords_cater[] = {
        "餐饮", "宴会", "餐厅", "菜单", "厨房", "菜品", "包厢", "堂食"
    };
    const char* keywords_meet[] = {
        "会议", "年会", "培训", "发布会", "论坛", "研讨", "团建", "会展", "会议室"
    };
    size_t i;
    for (i = 0; i < sizeof(keywords_hotel) / sizeof(keywords_hotel[0]); i++)
        if (strstr(text, keywords_hotel[i])) { hotel++; }
    for (i = 0; i < sizeof(keywords_cater) / sizeof(keywords_cater[0]); i++)
        if (strstr(text, keywords_cater[i])) { catering++; }
    for (i = 0; i < sizeof(keywords_meet) / sizeof(keywords_meet[0]); i++)
        if (strstr(text, keywords_meet[i])) { meeting++; }

    if (meeting >= catering && meeting >= hotel && meeting > 0) return BIZ_MEETING;
    if (catering >= hotel && catering > 0) return BIZ_CATERING;
    if (hotel > 0) return BIZ_HOTEL;
    return BIZ_OTHER;
}

int ls_lead_from_raw(ls_lead* lead, ls_json* raw)
{
    if (!lead || !raw) return -1;
    memset(lead, 0, sizeof(*lead));

    ls_gen_id(lead->id, sizeof(lead->id));
    ls_safe_copy(lead->company,
                 sizeof(lead->company),
                 ls_json_get_string(raw, "company", ""));
    ls_safe_copy(lead->contact,
                 sizeof(lead->contact),
                 ls_json_get_string(raw, "contact", ""));
    ls_safe_copy(lead->phone,
                 sizeof(lead->phone),
                 ls_json_get_string(raw, "phone", ""));
    ls_safe_copy(lead->email,
                 sizeof(lead->email),
                 ls_json_get_string(raw, "email", ""));
    ls_safe_copy(lead->needs,
                 sizeof(lead->needs),
                 ls_json_get_string(raw, "needs", ""));
    ls_safe_copy(lead->source,
                 sizeof(lead->source),
                 ls_json_get_string(raw, "source", ""));
    ls_safe_copy(lead->source_url,
                 sizeof(lead->source_url),
                 ls_json_get_string(raw, "url", ""));

    const char* biz = ls_json_get_string(raw, "biz_type", "");
    if (strcmp(biz, "hotel") == 0) lead->biz_type = BIZ_HOTEL;
    else if (strcmp(biz, "catering") == 0) lead->biz_type = BIZ_CATERING;
    else if (strcmp(biz, "meeting") == 0) lead->biz_type = BIZ_MEETING;
    else lead->biz_type = ls_biz_type_detect(lead->needs);

    ls_time_str(lead->created_at, sizeof(lead->created_at));
    lead->score = ls_lead_score(lead);
    ls_lead_advice(lead, lead->advice, sizeof(lead->advice));
    return 0;
}

ls_json* ls_lead_to_json(const ls_lead* lead)
{
    ls_json* j = ls_json_object();
    ls_json_set(j, "id", ls_json_string(lead->id));
    ls_json_set(j, "company", ls_json_string(lead->company));
    ls_json_set(j, "contact", ls_json_string(lead->contact));
    ls_json_set(j, "phone", ls_json_string(lead->phone));
    ls_json_set(j, "email", ls_json_string(lead->email));
    ls_json_set(j, "needs", ls_json_string(lead->needs));
    const char* biz = "other";
    if (lead->biz_type == BIZ_HOTEL) biz = "hotel";
    else if (lead->biz_type == BIZ_CATERING) biz = "catering";
    else if (lead->biz_type == BIZ_MEETING) biz = "meeting";
    ls_json_set(j, "biz_type", ls_json_string(biz));
    ls_json_set(j, "source", ls_json_string(lead->source));
    ls_json_set(j, "url", ls_json_string(lead->source_url));
    ls_json_set(j, "score", ls_json_number(lead->score));
    ls_json_set(j, "advice", ls_json_string(lead->advice));
    ls_json_set(j, "created_at", ls_json_string(lead->created_at));
    return j;
}

void ls_lead_print(const ls_lead* lead)
{
    printf("  [%s] 评分: %d/100\n", lead->company, lead->score);
    printf("      业态: %s | 联系人: %s | 电话: %s | 邮箱: %s\n",
           ls_biz_type_name(lead->biz_type),
           lead->contact[0] ? lead->contact : "-",
           lead->phone[0] ? lead->phone : "-",
           lead->email[0] ? lead->email : "-");
    printf("      需求: %s\n", lead->needs[0] ? lead->needs : "-");
    printf("      来源: %s (%s)\n", lead->source, lead->source_url);
    printf("      建议: %s\n", lead->advice);
}

int ls_lead_score(const ls_lead* lead)
{
    int score = 0;

    /* 1. 需求关键词命中度(0-40) */
    const char* strong[] = { "采购", "招标", "合作", "需求", "预算", "签约" };
    const char* medium[] = { "会议", "年会", "宴会", "客房", "餐饮", "装修", "系统" };
    size_t i;
    int hits = 0;
    for (i = 0; i < sizeof(strong) / sizeof(strong[0]); i++)
        if (strstr(lead->needs, strong[i])) hits += 3;
    for (i = 0; i < sizeof(medium) / sizeof(medium[0]); i++)
        if (strstr(lead->needs, medium[i])) hits += 2;
    if (hits > 40) hits = 40;
    score += hits;

    /* 2. 联系方式完整度(0-30) */
    if (lead->phone[0] && strlen(lead->phone) >= 7) score += 15;
    if (lead->email[0] && strchr(lead->email, '@')) score += 10;
    if (lead->contact[0]) score += 5;

    /* 3. 业态匹配度(0-20) */
    if (lead->biz_type != BIZ_OTHER) score += 15;
    if (lead->source_url[0]) score += 5;

    /* 4. 时效加成:当天采集的线索 +5 */
    if (lead->created_at[0]) {
        char now[32];
        ls_time_str(now, sizeof(now));
        if (memcmp(lead->created_at, now, 10) == 0) score += 5;
    }

    if (score > 100) score = 100;
    return score;
}

void ls_lead_advice(const ls_lead* lead, char* out, size_t n)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char period[32] = "上午";
    if (tm) {
        if (tm->tm_hour >= 9 && tm->tm_hour < 11) ls_safe_copy(period, sizeof(period), "上午 9-11 点(建议即刻触达)");
        else if (tm->tm_hour >= 14 && tm->tm_hour < 17) ls_safe_copy(period, sizeof(period), "下午 2-5 点(黄金时段)");
        else ls_safe_copy(period, sizeof(period), "次日 9-11 点(工作黄金时段)");
    }
    (void)ts;

    const char* method = "微信/电话";
    if (lead->score >= 70) method = "电话优先(高意向)";
    else if (lead->email[0] && !lead->phone[0]) method = "邮件先行";
    else if (lead->phone[0]) method = "微信或电话";
    else method = "邮件触达";

    snprintf(out, n, "%s,建议%s。成交概率评分 %d/100。",
             period, method, lead->score);
}

void ls_lead_sort_by_score(ls_lead* leads, int count)
{
    int i, j;
    for (i = 0; i < count - 1; i++) {
        for (j = 0; j < count - 1 - i; j++) {
            if (leads[j].score < leads[j + 1].score) {
                ls_lead tmp = leads[j];
                leads[j] = leads[j + 1];
                leads[j + 1] = tmp;
            }
        }
    }
}

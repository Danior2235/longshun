/*
 * crawler.c - 多线程线索爬虫实现
 * 流程: 抓取 -> 去标签/清洗 -> 实体提取(公司/电话/邮箱/需求) -> 业态识别 -> 去重 -> 落盘
 */
#include "crawler.h"
#include "http.h"
#include "storage.h"
#include "log.h"

/* 需求关键词库(用于从文本中识别客户需求线索) */
static const char* NEED_WORDS[] = {
    "采购", "招标", "投标", "合作", "签约", "预算",
    "会议", "年会", "培训", "发布会", "论坛", "团建", "会展",
    "宴会", "婚宴", "酒席", "包厢",
    "客房", "住宿", "套房",
    "装修", "改造", "翻新",
    "系统", "信息化", "数字化", "官网", "小程序",
    "营销", "推广", "广告", "物料",
    "维修", "保养", "设备",
    NULL
};

/* 候选公司后缀词 */
static const char* COMPANY_SUFFIX[] = {
    "有限公司", "有限责任公司", "集团", "股份公司", "事务所", "工作室",
    NULL
};

/* ---------------- 文本清洗 ---------------- */

/* 将 HTML 转为纯文本(去 script/style/标签,压缩空白) */
static char* html_to_text(const char* html)
{
    if (!html) return ls_strdup("");
    size_t n = strlen(html);
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    size_t w = 0;
    int in_tag = 0;
    int in_script = 0;
    int last_space = 0;
    const char* p = html;

    while (*p) {
        if (in_script) {
            if (ls_strnicmp(p, "</script", 8) == 0) in_script = 0;
            p++;
            continue;
        }
        if (in_tag) {
            if (*p == '>') in_tag = 0;
            p++;
            continue;
        }
        if (*p == '<') {
            if (ls_strnicmp(p, "<script", 7) == 0 ||
                ls_strnicmp(p, "<style", 6) == 0) {
                in_script = 1;
            } else {
                in_tag = 1;
            }
            p++;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            if (!last_space && w > 0) out[w++] = ' ';
            last_space = 1;
            p++;
            continue;
        }
        out[w++] = *p;
        last_space = 0;
        p++;
    }
    out[w] = '\0';
    return out;
}

/* ---------------- 实体提取 ---------------- */

/* 从文本提取手机号(1[3-9]xxxxxxxxx,允许 - 空格等分隔符);无则返回 0 */
static int extract_mobile(const char* text, char* out, size_t n)
{
    (void)n;
    const char* p = text;
    while (*p) {
        if (p[0] == '1' && p[1] >= '3' && p[1] <= '9') {
            const char* q = p;
            int digits = 0;
            int chars = 0;
            char tmp[24];
            size_t t = 0;
            while (chars < 24 && *q) {
                if (*q >= '0' && *q <= '9') {
                    digits++;
                    if (t < sizeof(tmp) - 1) tmp[t++] = *q;
                } else if (*q != '-' && *q != ' ' && *q != '(' && *q != ')') {
                    break; /* 遇到非数字且非分隔符,放弃 */
                }
                if (digits == 11) {
                    tmp[t] = '\0';
                    ls_safe_copy(out, n, tmp);
                    return 1;
                }
                q++;
                chars++;
            }
        }
        p++;
    }
    return 0;
}

/* 从文本提取固话(0xx-xxxxxxx 或 0xxxxxxxxx);无则返回 0 */
static int extract_tel(const char* text, char* out, size_t n)
{
    const char* p = text;
    while (*p) {
        if (*p == '0' && p[1] >= '0' && p[1] <= '9') {
            const char* q = p;
            int digits = 0;
            int hyphen = 0;
            char tmp[32];
            size_t t = 0;
            while ((*q >= '0' && *q <= '9') || *q == '-' || *q == ' ' || *q == '-') {
                if (t < sizeof(tmp) - 1) {
                    if (*q >= '0' && *q <= '9') {
                        tmp[t++] = *q;
                        digits++;
                    } else if (*q == '-') {
                        hyphen = 1;
                    }
                }
                q++;
                if (t > 20) break;
            }
            if (digits >= 10 && digits <= 12 && hyphen) {
                tmp[t] = '\0';
                ls_safe_copy(out, n, tmp);
                return 1;
            }
            p = q;
        } else {
            p++;
        }
    }
    return 0;
}

/* 从文本提取邮箱;无则返回 0 */
static int extract_email(const char* text, char* out, size_t n)
{
    const char* p = text;
    while (*p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9')) {
            /* 尝试匹配邮箱 */
            size_t local_len = 0;
            const char* q = p;
            while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                   (*q >= '0' && *q <= '9') || *q == '.' || *q == '_' ||
                   *q == '-' || *q == '+') {
                local_len++;
                q++;
                if (local_len > 64) break;
            }
            if (local_len > 0 && local_len < 64 && *q == '@') {
                const char* domain = q + 1;
                const char* d = domain;
                int dot = 0;
                while ((*d >= 'a' && *d <= 'z') || (*d >= 'A' && *d <= 'Z') ||
                       (*d >= '0' && *d <= '9') || *d == '.' || *d == '-') {
                    if (*d == '.') dot = 1;
                    d++;
                    if ((size_t)(d - domain) > 128) break;
                }
                if (dot && d > domain) {
                    size_t total = local_len + (size_t)(d - domain) + 1;
                    if (total < n) {
                        memcpy(out, p, total);
                        out[total] = '\0';
                        return 1;
                    }
                }
            }
            p = q;
        } else {
            p++;
        }
    }
    return 0;
}

/* 从文本提取公司名(寻找"XX有限公司"等);无则返回 0 */
static int extract_company(const char* text, char* out, size_t n)
{
    const char* p = text;
    size_t i;
    while (*p) {
        for (i = 0; COMPANY_SUFFIX[i]; i++) {
            const char* hit = strstr(p, COMPANY_SUFFIX[i]);
            if (hit) {
                /* 向前回溯最多 40 字符找公司名起点 */
                const char* s = hit;
                int budget = 40;
                while (s > p && budget-- > 0) {
                    if ((unsigned char)s[-1] >= 0x80 || isalnum((unsigned char)s[-1])) {
                        s--;
                    } else {
                        break;
                    }
                }
                size_t total = (size_t)(hit - s) + strlen(COMPANY_SUFFIX[i]);
                if (total > 0 && total < n - 1) {
                    memcpy(out, s, total);
                    out[total] = '\0';
                    return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

/* 从文本提取需求关键词(命中词库的词拼接) */
static void extract_needs(const char* text, char* out, size_t n)
{
    size_t w = 0;
    out[0] = '\0';
    size_t i;
    for (i = 0; NEED_WORDS[i]; i++) {
        if (strstr(text, NEED_WORDS[i])) {
            if (w > 0 && w + 4 < n) {
                out[w++] = ',';
            }
            size_t kw = strlen(NEED_WORDS[i]);
            if (w + kw + 1 < n) {
                memcpy(out + w, NEED_WORDS[i], kw);
                w += kw;
            }
        }
    }
    out[w] = '\0';
}

/* ---------------- 去重 ---------------- */

typedef struct {
    char** items;
    int count;
    int capacity;
} dedup_set;

static void dedup_init(dedup_set* s)
{
    s->items = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void dedup_add(dedup_set* s, const char* item)
{
    if (!item || !*item) return;
    int i;
    for (i = 0; i < s->count; i++)
        if (strcmp(s->items[i], item) == 0) return;
    if (s->count >= s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 16;
        s->items = (char**)realloc(s->items, (size_t)s->capacity * sizeof(char*));
    }
    s->items[s->count++] = ls_strdup(item);
}

static int dedup_contains(const dedup_set* s, const char* item)
{
    if (!item || !*item) return 0;
    int i;
    for (i = 0; i < s->count; i++)
        if (strcmp(s->items[i], item) == 0) return 1;
    return 0;
}

static void dedup_free(dedup_set* s)
{
    int i;
    for (i = 0; i < s->count; i++) free(s->items[i]);
    free(s->items);
}

/* ---------------- 抓取 ---------------- */

static char* fetch_seed(const ls_seed* seed, int demo_mode)
{
    if (demo_mode) {
        /* 本地 demo 页面 */
        return ls_storage_read_text(seed->url);
    }
    ls_http_resp* r = ls_http_get(seed->url, NULL);
    if (!r) return NULL;
    if (r->err || r->status_code != 200) {
        LOG_WARN("抓取失败 [%s] (%s) %s", seed->name, seed->url,
                 r->err ? r->err : "非 200 响应");
        ls_http_resp_free(r);
        return NULL;
    }
    char* body = ls_strdup(r->body ? r->body : "");
    ls_http_resp_free(r);
    return body;
}

/* ---------------- 工作线程 ---------------- */

typedef struct {
    const ls_crawler_cfg* cfg;
    LS_MUTEX lock;
    volatile int next_seed;
    dedup_set seen;          /* 已见公司名 */
    ls_json* results;        /* 产出记录数组 */
    int produced;
} crawler_ctx;

/* 将一条清洗后的线索写入结果集(含有效性过滤/去重/限流) */
static void emit_record(crawler_ctx* ctx, const ls_seed* seed,
                        const char* company, const char* phone,
                        const char* email, const char* needs_in)
{
    char needs[512];
    ls_safe_copy(needs, sizeof(needs), needs_in ? needs_in : "");

    /* 有效性过滤:必须有公司名(或至少联系方式) */
    if (!company[0] && !phone[0] && !email[0]) {
        LOG_DEBUG("信息源 [%s] 未提取到有效线索,跳过", seed->name);
        return;
    }
    if (!needs[0]) ls_safe_copy(needs, sizeof(needs), "待确认");

    /* 去重:按公司名 */
    if (company[0] && dedup_contains(&ctx->seen, company)) {
        LOG_DEBUG("线索 [%s] 已存在,跳过", company);
        return;
    }

    ls_mutex_lock(&ctx->lock);
    if (ctx->cfg->max_items > 0 && ctx->produced >= ctx->cfg->max_items) {
        ls_mutex_unlock(&ctx->lock);
        return;
    }
    if (company[0]) dedup_add(&ctx->seen, company);

    ls_json* rec = ls_json_object();
    ls_json_set(rec, "company", ls_json_string(company[0] ? company : seed->name));
    ls_json_set(rec, "contact", ls_json_string(""));
    ls_json_set(rec, "phone", ls_json_string(phone));
    ls_json_set(rec, "email", ls_json_string(email));
    ls_json_set(rec, "needs", ls_json_string(needs));
    ls_json_set(rec, "source", ls_json_string(seed->name));
    ls_json_set(rec, "url", ls_json_string(seed->url));
    /* 业态识别(与 lead 模块保持一致的判定口径) */
    {
        int hotel = 0, cater = 0, meet = 0;
        if (strstr(needs, "客房") || strstr(needs, "住宿") ||
            strstr(needs, "酒店") || strstr(needs, "套房")) hotel++;
        if (strstr(needs, "宴会") || strstr(needs, "婚宴") ||
            strstr(needs, "餐饮") || strstr(needs, "酒席")) cater++;
        if (strstr(needs, "会议") || strstr(needs, "年会") ||
            strstr(needs, "培训") || strstr(needs, "团建") ||
            strstr(needs, "发布会") || strstr(needs, "论坛")) meet++;
        const char* biz = "other";
        if (meet >= cater && meet >= hotel && meet > 0) biz = "meeting";
        else if (cater >= hotel && cater > 0) biz = "catering";
        else if (hotel > 0) biz = "hotel";
        ls_json_set(rec, "biz_type", ls_json_string(biz));
    }
    ls_json_array_add(ctx->results, rec);
    ctx->produced++;
    ls_mutex_unlock(&ctx->lock);

    LOG_INFO("产出线索: %s (电话=%s, 邮箱=%s, 需求=%s)",
             company, phone[0] ? phone : "-", email[0] ? email : "-", needs);
}

static void process_seed(crawler_ctx* ctx, int idx)
{
    const ls_seed* seed = &ctx->cfg->seeds[idx];
    if (!seed->enabled) return;

    LOG_INFO("抓取信息源 [%s] %s", seed->name, seed->url);
    char* body = fetch_seed(seed, ctx->cfg->demo_mode);
    if (!body) return;

    /* 清洗: HTML 源去标签转纯文本;JSON 源直接使用原始文本 */
    char* text;
    if (strcmp(seed->kind, "json") == 0) {
        text = body;
    } else {
        text = html_to_text(body);
        free(body);
    }

    if (strcmp(seed->kind, "json") == 0) {
        ls_json* j = ls_json_parse(text);
        if (j) {
            const char* company0 = ls_json_get_string(j, "company", "");
            if (company0[0]) {
                /* 单条结构化记录 */
                char phone[32] = {0}, email[64] = {0}, needs[512] = {0};
                ls_safe_copy(phone, sizeof(phone), ls_json_get_string(j, "phone", ""));
                ls_safe_copy(email, sizeof(email), ls_json_get_string(j, "email", ""));
                const char* title = ls_json_get_string(j, "title", "");
                const char* desc = ls_json_get_string(j, "desc", "");
                char buf[1024];
                snprintf(buf, sizeof(buf), "%s %s", title, desc);
                extract_needs(buf, needs, sizeof(needs));
                emit_record(ctx, seed, company0, phone, email, needs);
            } else {
                /* 嵌套 items 数组:每条产出独立线索 */
                ls_json* items = ls_json_get(j, "items");
                int k, cnt = ls_json_array_size(items);
                for (k = 0; k < cnt; k++) {
                    ls_json* it = ls_json_array_get(items, k);
                    char company[128] = {0}, phone[32] = {0}, email[64] = {0}, needs[512] = {0};
                    ls_safe_copy(company, sizeof(company), ls_json_get_string(it, "company", ""));
                    ls_safe_copy(phone, sizeof(phone), ls_json_get_string(it, "phone", ""));
                    ls_safe_copy(email, sizeof(email), ls_json_get_string(it, "email", ""));
                    const char* title = ls_json_get_string(it, "title", "");
                    const char* desc = ls_json_get_string(it, "desc", "");
                    char buf[1024];
                    snprintf(buf, sizeof(buf), "%s %s", title, desc);
                    extract_needs(buf, needs, sizeof(needs));
                    emit_record(ctx, seed, company, phone, email, needs);
                }
            }
            ls_json_free(j);
        } else {
            LOG_WARN("JSON 信息源解析失败: %s", seed->name);
        }
    } else {
        /* 网页源 */
        char phone[32] = {0}, email[64] = {0};
        char company[128] = {0}, needs[512] = {0};
        extract_company(text, company, sizeof(company));
        if (!extract_mobile(text, phone, sizeof(phone)))
            extract_tel(text, phone, sizeof(phone));
        extract_email(text, email, sizeof(email));
        extract_needs(text, needs, sizeof(needs));
        emit_record(ctx, seed, company, phone, email, needs);
    }
    free(text);
}

static void* crawler_worker(void* arg)
{
    crawler_ctx* ctx = (crawler_ctx*)arg;
    for (;;) {
        ls_mutex_lock(&ctx->lock);
        int idx = ctx->next_seed++;
        ls_mutex_unlock(&ctx->lock);
        if (idx >= ctx->cfg->seed_count) break;
        process_seed(ctx, idx);
        if (ctx->cfg->sleep_ms > 0) ls_sleep_ms(ctx->cfg->sleep_ms);
    }
    return NULL;
}

/* ---------------- 对外接口 ---------------- */

int ls_crawler_load_seeds(const char* path, ls_seed** out, int* count)
{
    ls_json* root = ls_storage_load(path);
    if (!root || root->type != LS_JSON_ARRAY) {
        if (root) ls_json_free(root);
        return -1;
    }
    int n = ls_json_array_size(root);
    ls_seed* arr = (ls_seed*)calloc((size_t)(n ? n : 1), sizeof(ls_seed));
    if (!arr) { ls_json_free(root); return -1; }
    int i;
    for (i = 0; i < n; i++) {
        ls_json* item = ls_json_array_get(root, i);
        ls_safe_copy(arr[i].url, sizeof(arr[i].url),
                     ls_json_get_string(item, "url", ""));
        ls_safe_copy(arr[i].name, sizeof(arr[i].name),
                     ls_json_get_string(item, "name", ""));
        ls_safe_copy(arr[i].kind, sizeof(arr[i].kind),
                     ls_json_get_string(item, "kind", "webpage"));
        arr[i].freq_min = (int)ls_json_get_number(item, "freq_min", 0);
        arr[i].enabled = ls_json_get_bool(item, "enabled", 1);
        if (!arr[i].name[0]) ls_safe_copy(arr[i].name, sizeof(arr[i].name), arr[i].url);
    }
    ls_json_free(root);
    *out = arr;
    *count = n;
    return n;
}

void ls_crawler_free_seeds(ls_seed* seeds, int count)
{
    (void)count;
    free(seeds);
}

int ls_crawler_run(const ls_crawler_cfg* cfg)
{
    if (!cfg || cfg->seed_count <= 0) return 0;

    crawler_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.next_seed = 0;
    ctx.results = ls_json_array();
    dedup_init(&ctx.seen);
    ls_mutex_init(&ctx.lock);

    int threads = cfg->thread_count > 0 ? cfg->thread_count : 4;
    if (threads > cfg->seed_count) threads = cfg->seed_count;

    LS_THREAD* tids = (LS_THREAD*)calloc((size_t)threads, sizeof(LS_THREAD));
    int i;
    LOG_INFO("爬虫启动: %d 个信息源, %d 线程, 模式=%s",
             cfg->seed_count, threads, cfg->demo_mode ? "demo(本地)" : "http");

    for (i = 0; i < threads; i++)
        ls_thread_create(&tids[i], crawler_worker, &ctx);
    for (i = 0; i < threads; i++)
        ls_thread_join(tids[i]);
    free(tids);

    /* 落盘 */
    const char* out = cfg->out_path ? cfg->out_path : "data/leads.json";
    int rc = ls_storage_save(out, ctx.results);
    if (rc != 0) LOG_ERROR("线索保存失败: %s", out);

    int produced = ctx.produced;
    LOG_INFO("爬虫结束: 共产出 %d 条线索 -> %s", produced, out);

    ls_json_free(ctx.results);
    dedup_free(&ctx.seen);
    ls_mutex_destroy(&ctx.lock);
    return produced;
}

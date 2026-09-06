/*
 * synonym.c - 同义词/近义词匹配实现
 *
 * 三级结构: 分组(section) -> 标准值(key) -> 同义词列表(words)
 * 匹配方式: 子串包含 + 否定前缀识别(避免"不含早"命中"含早")
 */
#include "synonym.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 内部结构 ---------------- */
typedef struct {
    char** w;
    int    n;
} syn_words;

typedef struct {
    char*     key;
    syn_words words;
} syn_entry;

typedef struct {
    char*      name;
    syn_entry* ents;
    int        n;
} syn_section;

static syn_section* g_secs = NULL;
static int          g_nsec = 0;

/* ---------------- 内置默认词表(配置文件缺失时生效) ---------------- */
typedef struct {
    const char* section;
    const char* key;
    const char* words;   /* '|' 分隔 */
} syn_def;

static const syn_def DEFAULTS[] = {
    /* 意图识别 */
    {"intent", "venue",
     "会场|会议|会议室|报告厅|多功能|多功能厅|宴会|宴会厅|年会|培训|培训室|展厅|展馆|"
     "活动场地|场地|场馆|礼堂|沙龙|发布会|路演|团建|拓展|开会|讲座|论坛|venue|hall|meeting"},
    {"intent", "room",
     "大床|双床|套房|客房|住房|房价|订房|开房|入住|住宿|房间|标间|标准间|单人房|双人房|"
     "商务房|亲子房|留宿|过夜|住一晚|开个房|歇一晚|睡一晚|房费|住宿费|room"},

    /* 房型 */
    {"room_type", "大床房", "大床|大床房|大床间|单人床|单人大床|一张大床|大号床|单人间|单人房|夫妻房"},
    {"room_type", "双床房", "双床|双床房|双人间|双人房|两张床|标准间|标间|双标|双标间|双床标准间"},
    {"room_type", "套房",   "套房|豪华套房|商务套房|高级套房|总统套房|套间|行政套房|家庭套房"},

    /* 楼栋 */
    {"building", "1号楼", "1号|一号|1号楼|一号楼|1栋|一栋|A栋|A座"},
    {"building", "2号楼", "2号|二号|2号楼|二号楼|2栋|二栋|B栋|B座"},
    {"building", "3号楼", "3号|三号|3号楼|三号楼|3栋|三栋|C栋|C座"},

    /* 早餐类型 */
    {"category", "含早", "含早|含早餐|包早|包早餐|带早餐|自助早餐|有早餐|送早餐|含自助|带早|双早|含双早"},
    {"category", "净房", "净房|不含早|不含早餐|无早|没早餐|不要早餐|不带早餐|免早|不含餐|无早餐|净房价"},

    /* 会场布局 */
    {"layout", "theater",     "剧院|剧院式|礼堂式|阶梯|阶梯式|报告式"},
    {"layout", "classroom",   "课桌|课桌式|培训式|课堂式|教室式"},
    {"layout", "island",      "岛屿|岛屿式|鱼骨|鱼骨式|分组式|岛式"},
    {"layout", "round_table", "圆桌|圆桌式|宴会式|圆桌宴会|中式圆桌"},

    /* 备注设备 */
    {"equip", "茶水",   "茶水|茶歇|茶水服务|饮水|茶点|矿泉水|热水"},
    {"equip", "话筒",   "话筒|麦克风|无线麦|无线话筒|手持麦|领夹麦|音响"},
    {"equip", "投影仪", "投影|投影仪|投屏|幕布|PPT|ppt|投影幕"},
    {"equip", "LED",    "LED|led|大屏|LED屏|led屏|屏幕|显示屏|大屏幕"},
    {"equip", "其他",   "其他|额外|特殊设备|别的设备|其它"},

    /* 客户类型 */
    {"cust_type", "study_travel",           "研学|旅行|旅行社|学生|学生团|游学|研学团|夏令营|冬令营"},
    {"cust_type", "government_enterprise",  "政府|企业|公司|商务|公务|机关|事业单位|国企|政企|单位|团体"},

    /* 使用时长 */
    {"duration", "full", "全天|整天|一整天|全日|一天|整日|全天场"},
    {"duration", "half", "半天|半日|半场|上午|下午|半程|半天场"},

    /* 增值项: 是 / 否 */
    {"rehearsal", "yes", "彩排|要彩排|需要彩排|含彩排|用彩排|提前彩排|走场"},
    {"rehearsal", "no",  "不要彩排|不用彩排|不含彩排|无需彩排|没彩排|不彩排|免彩排"},
    {"lighting",  "yes", "灯光|要灯光|需要灯光|含灯光|舞美|舞美灯光|灯光秀"},
    {"lighting",  "no",  "不要灯光|不用灯光|不含灯光|无需灯光|没灯光|免灯光"},
    {"screen",    "yes", "大屏|要大屏|需要大屏|含大屏|屏幕|大屏幕|用大屏"},
    {"screen",    "no",  "不要大屏|不用大屏|不含大屏|无需大屏|没大屏|免大屏|不要屏幕|不用屏幕"},

    /* 否定词(用于"无额外设备"等场景) */
    {"negation", "no", "不要|不用|不需要|无需|没有|免|别|都不|无须"},

    /* 用餐 */
    {"catering", "none", "不用餐|不需要用餐|不含餐|不要餐|免餐|无餐|不用饭|不吃饭|不包餐"},

    /* 数量单位 */
    {"unit", "nights", "晚|夜|晚上|宿|个晚上"},
    {"unit", "rooms",  "间|个房间|个房|间房"},
    {"unit", "tables", "桌|席|桌席"},
    {"unit", "hours",  "小时|钟头|个钟"},
    {"unit", "people", "人|位|个人|客人|参会|名"},

    /* 相对日期 */
    {"date", "today",      "今天|今日|今儿|当天"},
    {"date", "tomorrow",   "明天|明日|明儿|次日"},
    {"date", "day_after",  "后天|后日|后儿"},
};

/* ---------------- 内部工具 ---------------- */
static char* syn_strdup(const char* s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static syn_section* find_section(const char* name, int create)
{
    int i;
    for (i = 0; i < g_nsec; i++) {
        if (strcmp(g_secs[i].name, name) == 0) return &g_secs[i];
    }
    if (!create) return NULL;
    syn_section* tmp = (syn_section*)realloc(g_secs, (size_t)(g_nsec + 1) * sizeof(syn_section));
    if (!tmp) return NULL;
    g_secs = tmp;
    memset(&g_secs[g_nsec], 0, sizeof(syn_section));
    g_secs[g_nsec].name = syn_strdup(name);
    g_nsec++;
    return &g_secs[g_nsec - 1];
}

static syn_entry* find_entry(syn_section* sec, const char* key, int create)
{
    int i;
    for (i = 0; i < sec->n; i++) {
        if (strcmp(sec->ents[i].key, key) == 0) return &sec->ents[i];
    }
    if (!create) return NULL;
    syn_entry* tmp = (syn_entry*)realloc(sec->ents, (size_t)(sec->n + 1) * sizeof(syn_entry));
    if (!tmp) return NULL;
    sec->ents = tmp;
    memset(&sec->ents[sec->n], 0, sizeof(syn_entry));
    sec->ents[sec->n].key = syn_strdup(key);
    sec->n++;
    return &sec->ents[sec->n - 1];
}

static void add_word(syn_entry* ent, const char* w)
{
    if (!ent || !w || !*w) return;
    char** tmp = (char**)realloc(ent->words.w, (size_t)(ent->words.n + 1) * sizeof(char*));
    if (!tmp) return;
    ent->words.w = tmp;
    ent->words.w[ent->words.n] = syn_strdup(w);
    ent->words.n++;
}

/* 按 '|' 切分加入 */
static void add_words(syn_entry* ent, const char* pipe_list)
{
    if (!ent || !pipe_list) return;
    const char* p = pipe_list;
    while (*p) {
        const char* sep = strchr(p, '|');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        char buf[128];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
        add_word(ent, buf);
        if (!sep) break;
        p = sep + 1;
    }
}

/* 清空某项词表(配置文件覆盖默认表时使用) */
static void clear_entry(syn_entry* ent)
{
    int i;
    for (i = 0; i < ent->words.n; i++) free(ent->words.w[i]);
    free(ent->words.w);
    ent->words.w = NULL;
    ent->words.n = 0;
}

/* 匹配位置前是否为否定字(UTF-8 汉字占 3 字节), 避免"不含早"命中"含早" */
static int negated_before(const char* q, const char* pos)
{
    if (pos - q < 3) return 0;
    const char* prev = pos - 3;
    static const char* negs[] = { "不", "没", "无", "免", "别", "非" };
    int i;
    for (i = 0; i < (int)(sizeof(negs) / sizeof(negs[0])); i++) {
        if (strncmp(prev, negs[i], 3) == 0) return 1;
    }
    return 0;
}

static int entry_hit(const syn_entry* ent, const char* q)
{
    int i;
    for (i = 0; i < ent->words.n; i++) {
        const char* word = ent->words.w[i];
        if (!word || !*word) continue;
        size_t wl = strlen(word);
        const char* p = q;
        while ((p = strstr(p, word)) != NULL) {
            if (!negated_before(q, p)) return 1;
            p += wl;
        }
    }
    return 0;
}

/* ---------------- 对外接口 ---------------- */
void syn_init(const char* data_dir)
{
    int i;
    /* 1) 内置默认表 */
    for (i = 0; i < (int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0])); i++) {
        syn_section* s = find_section(DEFAULTS[i].section, 1);
        if (!s) continue;
        syn_entry* e = find_entry(s, DEFAULTS[i].key, 1);
        if (e) add_words(e, DEFAULTS[i].words);
    }

    /* 2) 外部配置(存在则覆盖对应分组/标准值的默认词表) */
    if (!data_dir || !*data_dir) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/config/synonyms.json", data_dir);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0 && sz < (1 << 20)) {
        char* buf = (char*)malloc((size_t)sz + 1);
        if (buf) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            buf[got] = '\0';
            ls_json* root = ls_json_parse(buf);
            if (root && root->type == LS_JSON_OBJECT) {
                int si, ki, wi;
                for (si = 0; si < root->count; si++) {
                    const char* sname = root->items[si].key;
                    ls_json* sec = root->items[si].value;
                    if (!sname || !sec || sec->type != LS_JSON_OBJECT) continue;
                    for (ki = 0; ki < sec->count; ki++) {
                        const char* kname = sec->items[ki].key;
                        ls_json* val = sec->items[ki].value;
                        if (!kname || !val) continue;
                        syn_section* s = find_section(sname, 1);
                        syn_entry* e = s ? find_entry(s, kname, 1) : NULL;
                        if (!e) continue;
                        clear_entry(e);
                        if (val->type == LS_JSON_ARRAY) {
                            for (wi = 0; wi < val->count; wi++) {
                                ls_json* wj = val->items[wi].value;
                                if (wj && wj->type == LS_JSON_STRING) add_word(e, wj->string);
                            }
                        } else if (val->type == LS_JSON_STRING) {
                            add_words(e, val->string);
                        }
                    }
                }
            }
            if (root) ls_json_free(root);
            free(buf);
        }
    }
    fclose(f);
}

void syn_shutdown(void)
{
    int i, j, k;
    for (i = 0; i < g_nsec; i++) {
        for (j = 0; j < g_secs[i].n; j++) {
            for (k = 0; k < g_secs[i].ents[j].words.n; k++) free(g_secs[i].ents[j].words.w[k]);
            free(g_secs[i].ents[j].words.w);
            free(g_secs[i].ents[j].key);
        }
        free(g_secs[i].ents);
        free(g_secs[i].name);
    }
    free(g_secs);
    g_secs = NULL;
    g_nsec = 0;
}

int syn_pick(const char* q, const char* section, char* out, size_t outsz)
{
    if (!q || !section) return 0;
    syn_section* s = find_section(section, 0);
    if (!s) return 0;
    int i;
    for (i = 0; i < s->n; i++) {
        if (entry_hit(&s->ents[i], q)) {
            if (out && outsz) snprintf(out, outsz, "%s", s->ents[i].key);
            return 1;
        }
    }
    return 0;
}

int syn_has(const char* q, const char* section, const char* key)
{
    if (!q || !section || !key) return 0;
    syn_section* s = find_section(section, 0);
    if (!s) return 0;
    syn_entry* e = find_entry(s, key, 0);
    if (!e) return 0;
    return entry_hit(e, q);
}

int syn_has_any(const char* q, const char* section)
{
    if (!q || !section) return 0;
    syn_section* s = find_section(section, 0);
    if (!s) return 0;
    int i;
    for (i = 0; i < s->n; i++) {
        if (entry_hit(&s->ents[i], q)) return 1;
    }
    return 0;
}

int syn_qty(const char* q, const char* section, const char* key)
{
    if (!q || !section || !key) return -1;
    syn_section* s = find_section(section, 0);
    if (!s) return -1;
    syn_entry* e = find_entry(s, key, 0);
    if (!e) return -1;

    int i, best = -1;
    for (i = 0; i < e->words.n; i++) {
        const char* word = e->words.w[i];
        if (!word || !*word) continue;
        size_t wl = strlen(word);
        const char* p = q;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                int v = 0;
                const char* start = p;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                const char* save = p;
                while (*p == ' ') p++;
                if (strncmp(p, word, wl) == 0 && v > best) best = v;
                p = (save > start + 1) ? save : start + 1;
                continue;
            }
            p++;
        }
    }
    return best;
}

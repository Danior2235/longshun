/*
 * main.c - 龙顺 AI 销售与客户运营助手(C 语言实现)CLI 入口
 * 覆盖需求方案中适合 C 语言实现的核心能力:
 *   模块一: 线索爬取采集 / 数据清洗 / 商机评分与沟通建议
 *   模块二: 实时数据看板 / 任务拆解分派 / 问题追溯
 *   模块三: 状态同步中间件 / 开票税务规则引擎
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "log.h"
#include "json.h"
#include "storage.h"
#include "crawler.h"
#include "lead.h"
#include "task.h"
#include "sync.h"
#include "invoice_rules.h"
#include "pricing.h"
#include "dashboard.h"
#include "ls_rpc.h"

#define DATA_DIR "data"

static void print_banner(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("   龙顺 AI 销售与客户运营助手  (C 语言实现)  v1.0\n");
    printf("   面向酒店/餐饮/会议多业态: 线索采集 | 数据协同 | 后台提效\n");
    printf("============================================================\n");
}

static void print_help(void)
{
    printf("\n可用命令:\n");
    printf("  crawl      启动多线程爬虫,采集并清洗客户线索\n");
    printf("  leads      查看结构化客户线索清单(含评分与建议)\n");
    printf("  score      按成交概率对线索重新评分排序\n");
    printf("  dispatch   需求自动拆解为子任务,分派至各业务模块执行\n");
    printf("  tasks      查看任务清单与追溯时间线\n");
    printf("  sync       演示状态同步(维修完成 -> 客房自动置为不可用)\n");
    printf("  invoice    演示开票支持(税务规则引擎推荐签约主体)\n");
    printf("  rooms      查看客房对外报价体系\n");
    printf("  venues     查看会场对外报价体系\n");
    printf("  quote      演示智能报价(客房+会场组合询价)\n");
    printf("  dashboard  启动实时数据看板服务(http://localhost:8080)\n");
    printf("  help       显示本帮助\n");
    printf("  quit       退出\n");
    printf("\n网络架构模式:\n");
    printf("  server [port]                      启动 RPC 服务端(默认端口 %d)\n", LS_RPC_DEFAULT_PORT);
    printf("  client [-h 主机] [-p 端口] <cmd>   连接服务端执行命令\n");
}

/* ---------- 命令: 爬虫 ---------- */
static int cmd_crawl(void)
{
    printf("\n[模块一] 客户线索获取: 多线程爬虫采集与清洗\n");
    ls_seed* seeds = NULL;
    int n = ls_crawler_load_seeds(DATA_DIR "/seeds.json", &seeds, &n);
    if (n < 0) {
        LOG_ERROR("信息源清单加载失败: data/seeds.json");
        printf("  (请确认 data/seeds.json 存在)\n");
        return -1;
    }
    printf("  已加载 %d 个信息源\n", n);

    ls_crawler_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seeds = seeds;
    cfg.seed_count = n;
    cfg.thread_count = 4;
    cfg.max_items = 50;
    cfg.demo_mode = 1;           /* 离线 demo:读取 data/demo_pages/ */
    cfg.sleep_ms = 50;           /* 抓取节流(爬虫合规) */
    cfg.out_path = DATA_DIR "/leads.json";

    int produced = ls_crawler_run(&cfg);
    ls_crawler_free_seeds(seeds, n);

    printf("\n  采集完成,共生成 %d 条结构化线索 -> data/leads.json\n", produced);
    return produced;
}

/* ---------- 命令: 查看线索 ---------- */
static int cmd_leads(void)
{
    printf("\n[模块一] 客户线索清单(清洗后结构化输出)\n");
    ls_json* root = ls_storage_load(DATA_DIR "/leads.json");
    if (!root) {
        printf("  线索库为空,请先执行 crawl\n");
        return -1;
    }
    int n = ls_json_array_size(root);
    printf("  共 %d 条线索\n\n", n);
    int i;
    for (i = 0; i < n; i++) {
        ls_lead lead;
        if (ls_lead_from_raw(&lead, ls_json_array_get(root, i)) == 0) {
            printf("  [%d/%d]\n", i + 1, n);
            ls_lead_print(&lead);
            printf("\n");
        }
    }
    ls_json_free(root);
    return 0;
}

/* ---------- 命令: 评分排序 ---------- */
static int cmd_score(void)
{
    printf("\n[模块一] 商机转化: 按成交概率评分排序\n");
    ls_json* root = ls_storage_load(DATA_DIR "/leads.json");
    if (!root) {
        printf("  线索库为空,请先执行 crawl\n");
        return -1;
    }
    int n = ls_json_array_size(root);
    ls_lead* leads = (ls_lead*)calloc((size_t)(n ? n : 1), sizeof(ls_lead));
    int valid = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (ls_lead_from_raw(&leads[valid], ls_json_array_get(root, i)) == 0)
            valid++;
    }
    ls_json_free(root);

    ls_lead_sort_by_score(leads, valid);

    printf("  成交概率 Top 排行(评分 = 需求命中 + 联系方式完整度 + 业态匹配):\n\n");
    for (i = 0; i < valid; i++) {
        const ls_lead* l = &leads[i];
        printf("  #%2d  %-24s 评分 %3d/100 | %s | %s\n",
               i + 1, l->company, l->score,
               ls_biz_type_name(l->biz_type), l->advice);
    }
    free(leads);
    return 0;
}

/* ---------- 命令: 任务拆解分派 ---------- */
static int cmd_dispatch(void)
{
    printf("\n[模块二] 业务协同: 客户需求自动拆解 -> 分派执行 -> 全程追溯\n");
    int rc = ls_task_process_leads(DATA_DIR "/leads.json", 3);
    if (rc < 0) printf("  请先执行 crawl 采集线索\n");
    return rc;
}

/* ---------- 命令: 查看任务 ---------- */
static int cmd_tasks(void)
{
    printf("\n[模块二] 问题追溯: 任务状态与时间线\n");
    printf("  (任务执行记录请查看日志 data/logs/, 完整追溯链随执行输出)\n");
    printf("  提示: 执行 dispatch 时每个任务的「追溯」时间线会完整打印。\n");
    return 0;
}

/* ---------- 命令: 状态同步 ---------- */
static int cmd_sync(void)
{
    printf("\n[模块三] 信息同步: 维修完成 -> 客房自动置为不可用\n");
    ls_sync_demo(DATA_DIR);

    /* 展示同步后的房态 */
    printf("\n  同步后房态:\n");
    ls_json* rooms = ls_storage_load(DATA_DIR "/rooms.json");
    if (rooms) {
        int n = ls_json_array_size(rooms);
        int i;
        for (i = 0; i < n; i++) {
            ls_json* r = ls_json_array_get(rooms, i);
            printf("    %s | %-10s | %s\n",
                   ls_json_get_string(r, "no", ""),
                   ls_json_get_string(r, "type", ""),
                   ls_json_get_string(r, "status", ""));
        }
        ls_json_free(rooms);
    }
    return 0;
}

/* ---------- 命令: 开票支持 ---------- */
static int cmd_invoice(void)
{
    printf("\n[模块三] 开票支持: 税务规则引擎推荐签约主体\n");
    ls_license* lics = NULL;
    int n = ls_invoice_load_licenses(DATA_DIR "/company_licenses.json", &lics, &n);
    if (n < 0) {
        printf("  营业执照清单加载失败\n");
        return -1;
    }

    struct { const char* service; double amount; const char* note; const char* desc; } cases[] = {
        { "meeting", 120000,  "客户要求增值税专用发票",    "客户年会 + 发布会,需开专票" },
        { "catering", 45000,  NULL,                        "大型宴会餐饮服务" },
        { "hotel",    600000, NULL,                        "酒店客房批量采购合作" },
        { "goods",    500000, NULL,                        "布草/耗材物资采购" }
    };

    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ls_invoice_advice advice;
        printf("\n  场景: %s (服务=%s, 金额=%.0f 元)\n",
               cases[i].desc, cases[i].service, cases[i].amount);
        if (ls_invoice_recommend(lics, n, cases[i].service, cases[i].amount,
                                 cases[i].note, &advice) == 0)
            ls_invoice_print_advice(&advice);
        else
            printf("  (无可推荐主体)\n");
    }
    ls_invoice_free_licenses(lics, n);
    return 0;
}

/* ---------- 命令: 客房报价 ---------- */
static int cmd_rooms(void)
{
    printf("\n[定价体系] 龙顺庄园客房对外价格\n\n");
    ls_room_price* rooms = NULL;
    int n = 0;
    if (ls_pricing_load_rooms(DATA_DIR "/room_pricing.json", &rooms, &n) < 0) {
        printf("  加载失败,请确认 data/room_pricing.json 存在\n");
        return -1;
    }
    printf("  楼栋       | 房型     | 分类   | 客户类型(早餐)          | 报价\n");
    printf("  -----------+----------+--------+-------------------------+------\n");
    int i;
    for (i = 0; i < n; i++) ls_pricing_print_room(&rooms[i]);
    printf("\n  共 %d 条报价记录\n", n);
    ls_pricing_free_rooms(rooms);
    return 0;
}

/* ---------- 命令: 会场报价 ---------- */
static int cmd_venues(void)
{
    printf("\n[定价体系] 龙顺庄园各会场价格体系\n\n");
    ls_venue_price* venues = NULL;
    int n = 0;
    if (ls_pricing_load_venues(DATA_DIR "/venue_pricing.json", &venues, &n) < 0) {
        printf("  加载失败,请确认 data/venue_pricing.json 存在\n");
        return -1;
    }
    int i;
    for (i = 0; i < n; i++) {
        ls_pricing_print_venue(&venues[i]);
        if (i < n - 1) printf("\n");
    }
    printf("\n  共 %d 个会场\n", n);
    ls_pricing_free_venues(venues);
    return 0;
}

/* ---------- 命令: 智能报价 ---------- */
static int cmd_quote(void)
{
    printf("\n[定价体系] 智能组合报价演示\n\n");
    ls_time_rules tr;
    if (ls_pricing_load_time_rules(DATA_DIR "/time_rules.json", &tr) < 0) {
        memset(&tr, 0, sizeof(tr));
        tr.half_day_hours = 4;
        tr.evening_surcharge_pct = 40;
    }

    ls_room_price* rooms = NULL;
    ls_venue_price* venues = NULL;
    int nr = 0, nv = 0;
    if (ls_pricing_load_rooms(DATA_DIR "/room_pricing.json", &rooms, &nr) < 0 ||
        ls_pricing_load_venues(DATA_DIR "/venue_pricing.json", &venues, &nv) < 0) {
        printf("  数据加载失败\n");
        free(rooms); free(venues);
        return -1;
    }

    /* 演示场景: 政府企业团队年会 */
    printf("  场景A: 政府企业团队年会(2天1晚)\n");
    printf("    客房: 商务楼大床房含早 x 20间 x 1晚\n");
    ls_room_price qrooms[8];
    int nq = ls_pricing_query_rooms(rooms, nr, "商务楼", "大床房", "含早",
                                      LS_CUST_GOV_ENTERPRISE, qrooms, 8);
    double room_cost = (nq > 0) ? ls_pricing_calc_room(&qrooms[0], 1, 20) : 0;
    printf("    客房费用: %.0f 元\n", room_cost);

    printf("    会场: 大礼堂全天 x 1天 + 舞美灯光\n");
    ls_venue_price qvenues[4];
    int nqv = ls_pricing_query_venues(venues, nv, NULL, "大礼堂", 0, qvenues, 4);
    double venue_cost = (nqv > 0) ? ls_pricing_calc_venue(&qvenues[0], 1, 0, 0,
                            1, 0, 12000, &tr) : 0;
    printf("    会场费用: %.0f 元\n", venue_cost);
    printf("    合计: %.0f 元\n\n", room_cost + venue_cost);

    /* 演示场景: 研学团队会议室 */
    printf("  场景B: 研学团队会议室(半天)\n");
    printf("    会场: 三层小厅半天 x 1\n");
    nqv = ls_pricing_query_venues(venues, nv, "三层", NULL, 0, qvenues, 4);
    if (nqv > 0) {
        venue_cost = ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 0, &tr);
        printf("    会场费用: %.0f 元\n", venue_cost);
    }

    /* 演示场景: 水晶厅满额减免 */
    printf("\n  场景C: 水晶厅会议+用餐(满15000元免场地费)\n");
    nqv = ls_pricing_query_venues(venues, nv, NULL, "水晶厅（会议/用餐）", 0, qvenues, 4);
    if (nqv > 0) {
        double before = ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 0, &tr);
        double after  = ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 15000, &tr);
        printf("    餐饮未满额: %.0f 元\n", before);
        printf("    餐饮满15000: %.0f 元(免场地费)\n", after);
    }

    printf("\n  计费规则: 半天=%d小时, 晚间加收%d%%\n",
           tr.half_day_hours, tr.evening_surcharge_pct);

    ls_pricing_free_rooms(rooms);
    ls_pricing_free_venues(venues);
    return 0;
}

/* ---------- 命令: 实时看板 ---------- */
static int cmd_dashboard(void)
{
    printf("\n[模块二] 实时数据看板(HTTP 服务,按 Ctrl+C 停止)\n");
    ls_dashboard_start(8080, DATA_DIR);
    return 0;
}

/* ============================================================
 *  网络架构: RPC 命令处理器(返回 JSON,供服务端分派给客户端)
 * ============================================================ */

static const char* rpc_data_dir(void)
{
    const char* d = ls_rpc_data_dir();
    return (d && d[0]) ? d : DATA_DIR;
}

/* 取位置参数(JSON 数组元素) */
static const char* rpc_arg(ls_json* args, int idx)
{
    if (!args || args->type != LS_JSON_ARRAY) return NULL;
    if (idx >= ls_json_array_size(args)) return NULL;
    ls_json* v = ls_json_array_get(args, idx);
    return v->type == LS_JSON_STRING ? v->string : NULL;
}

static const char* rpc_arg_or_null(const char* s)
{
    return (s && s[0]) ? s : NULL;
}

static ls_json* rpc_cmd_ping(ls_json* args)
{
    (void)args;
    ls_json* data = ls_json_object();
    ls_json_set(data, "status", ls_json_string("ok"));
    ls_json_set(data, "service", ls_json_string("longshun"));
    char ts[32];
    ls_time_str(ts, sizeof(ts));
    ls_json_set(data, "time", ls_json_string(ts));
    return ls_rpc_ok(data, "pong");
}

static ls_json* rpc_cmd_help(ls_json* args)
{
    (void)args;
    ls_json* arr = ls_json_array();
    static const char* cmds[] = {
        "ping    健康检查",
        "crawl   采集并清洗客户线索",
        "leads   查看线索清单",
        "score   商机评分排行",
        "dispatch 需求拆解分派执行",
        "tasks   任务追溯",
        "sync    状态同步演示",
        "invoice 开票支持(税务规则)",
        "rooms   客房报价(可筛选)",
        "venues  会场报价(可筛选)",
        "quote   智能组合报价",
        "help    帮助"
    };
    size_t i;
    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ls_json_array_add(arr, ls_json_string(cmds[i]));
    ls_json* data = ls_json_object();
    ls_json_set(data, "commands", arr);
    return ls_rpc_ok(data, "支持的命令");
}

static ls_json* rpc_cmd_crawl(ls_json* args)
{
    (void)args;
    char seed_path[512], out_path[512];
    snprintf(seed_path, sizeof(seed_path), "%s/seeds.json", rpc_data_dir());
    snprintf(out_path, sizeof(out_path), "%s/leads.json", rpc_data_dir());
    ls_seed* seeds = NULL;
    int n = ls_crawler_load_seeds(seed_path, &seeds, &n);
    if (n < 0) return ls_rpc_err("信息源清单加载失败: %s", seed_path);
    ls_crawler_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seeds = seeds;
    cfg.seed_count = n;
    cfg.thread_count = 4;
    cfg.max_items = 50;
    cfg.demo_mode = 1;
    cfg.sleep_ms = 50;
    cfg.out_path = out_path;
    int produced = ls_crawler_run(&cfg);
    ls_crawler_free_seeds(seeds, n);
    ls_json* data = ls_json_object();
    ls_json_set(data, "produced", ls_json_number(produced));
    ls_json_set(data, "output", ls_json_string(out_path));
    return ls_rpc_ok(data, "采集完成,共生成 %d 条结构化线索", produced);
}

static ls_json* rpc_cmd_leads(ls_json* args)
{
    (void)args;
    char path[512];
    snprintf(path, sizeof(path), "%s/leads.json", rpc_data_dir());
    ls_json* root = ls_storage_load(path);
    if (!root) return ls_rpc_err("线索库为空,请先执行 crawl");
    int n = ls_json_array_size(root);
    ls_json* arr = ls_json_array();
    int i;
    for (i = 0; i < n; i++) {
        ls_lead lead;
        if (ls_lead_from_raw(&lead, ls_json_array_get(root, i)) == 0)
            ls_json_array_add(arr, ls_lead_to_json(&lead));
    }
    ls_json_free(root);
    ls_json* data = ls_json_object();
    ls_json_set(data, "count", ls_json_number(ls_json_array_size(arr)));
    ls_json_set(data, "leads", arr);
    return ls_rpc_ok(data, "共 %d 条线索", ls_json_array_size(arr));
}

static ls_json* rpc_cmd_score(ls_json* args)
{
    (void)args;
    char path[512];
    snprintf(path, sizeof(path), "%s/leads.json", rpc_data_dir());
    ls_json* root = ls_storage_load(path);
    if (!root) return ls_rpc_err("线索库为空,请先执行 crawl");
    int n = ls_json_array_size(root);
    ls_lead* leads = (ls_lead*)calloc((size_t)(n ? n : 1), sizeof(ls_lead));
    int valid = 0, i;
    for (i = 0; i < n; i++) {
        if (ls_lead_from_raw(&leads[valid], ls_json_array_get(root, i)) == 0)
            valid++;
    }
    ls_json_free(root);
    ls_lead_sort_by_score(leads, valid);
    ls_json* arr = ls_json_array();
    for (i = 0; i < valid; i++) {
        ls_json* it = ls_json_object();
        ls_json_set(it, "rank", ls_json_number(i + 1));
        ls_json_set(it, "company", ls_json_string(leads[i].company));
        ls_json_set(it, "score", ls_json_number(leads[i].score));
        ls_json_set(it, "biz_type", ls_json_string(ls_biz_type_name(leads[i].biz_type)));
        ls_json_set(it, "advice", ls_json_string(leads[i].advice));
        ls_json_array_add(arr, it);
    }
    free(leads);
    ls_json* data = ls_json_object();
    ls_json_set(data, "count", ls_json_number(valid));
    ls_json_set(data, "rank", arr);
    return ls_rpc_ok(data, "评分排行共 %d 条", valid);
}

static ls_json* rpc_cmd_dispatch(ls_json* args)
{
    (void)args;
    char path[512];
    snprintf(path, sizeof(path), "%s/leads.json", rpc_data_dir());
    int rc = ls_task_process_leads(path, 3);
    if (rc < 0) return ls_rpc_err("任务分派失败,请先执行 crawl");
    ls_json* data = ls_json_object();
    ls_json_set(data, "tasks", ls_json_number(rc));
    return ls_rpc_ok(data, "已拆解并执行 %d 个子任务(全程追溯)", rc);
}

static ls_json* rpc_cmd_tasks(ls_json* args)
{
    (void)args;
    char path[512];
    snprintf(path, sizeof(path), "%s/tasks.json", rpc_data_dir());
    ls_json* root = ls_storage_load(path);
    int cnt = root ? ls_json_array_size(root) : 0;
    if (root) ls_json_free(root);
    ls_json* data = ls_json_object();
    ls_json_set(data, "count", ls_json_number(cnt));
    ls_json_set(data, "note", ls_json_string("完整追溯时间线请查看服务端日志 data/logs/"));
    return ls_rpc_ok(data, "业务协同任务 %d 个", cnt);
}

static ls_json* rpc_cmd_sync(ls_json* args)
{
    (void)args;
    ls_sync_demo(rpc_data_dir());
    char path[512];
    snprintf(path, sizeof(path), "%s/rooms.json", rpc_data_dir());
    ls_json* rooms = ls_storage_load(path);
    ls_json* arr = ls_json_array();
    if (rooms) {
        int n = ls_json_array_size(rooms), i;
        for (i = 0; i < n; i++) {
            ls_json* r = ls_json_array_get(rooms, i);
            ls_json* it = ls_json_object();
            ls_json_set(it, "no", ls_json_string(ls_json_get_string(r, "no", "")));
            ls_json_set(it, "type", ls_json_string(ls_json_get_string(r, "type", "")));
            ls_json_set(it, "status", ls_json_string(ls_json_get_string(r, "status", "")));
            ls_json_array_add(arr, it);
        }
        ls_json_free(rooms);
    }
    ls_json* data = ls_json_object();
    ls_json_set(data, "rooms", arr);
    return ls_rpc_ok(data, "状态同步完成(维修->客房不可用,采购->入库)");
}

static ls_json* rpc_cmd_invoice(ls_json* args)
{
    (void)args;
    char path[512];
    snprintf(path, sizeof(path), "%s/company_licenses.json", rpc_data_dir());
    ls_license* lics = NULL;
    int n = ls_invoice_load_licenses(path, &lics, &n);
    if (n < 0) return ls_rpc_err("营业执照清单加载失败");
    struct { const char* service; double amount; const char* note; const char* desc; } cases[] = {
        { "meeting", 120000,  "客户要求增值税专用发票", "客户年会 + 发布会,需开专票" },
        { "catering", 45000,  NULL,                     "大型宴会餐饮服务" },
        { "hotel",    600000, NULL,                     "酒店客房批量采购合作" },
        { "goods",    500000, NULL,                     "布草/耗材物资采购" }
    };
    ls_json* arr = ls_json_array();
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ls_json* it = ls_json_object();
        ls_json_set(it, "scenario", ls_json_string(cases[i].desc));
        ls_json_set(it, "service", ls_json_string(cases[i].service));
        ls_json_set(it, "amount", ls_json_number(cases[i].amount));
        ls_invoice_advice advice;
        if (ls_invoice_recommend(lics, n, cases[i].service, cases[i].amount,
                                 cases[i].note, &advice) == 0) {
            ls_json_set(it, "company", ls_json_string(advice.license->company));
            ls_json_set(it, "tax_rate", ls_json_number(advice.rate));
            ls_json_set(it, "tax_amount", ls_json_number(advice.tax_amount));
            ls_json_set(it, "reason", ls_json_string(advice.reason));
            ls_json_set(it, "risk", ls_json_string(advice.risk));
        } else {
            ls_json_set(it, "company", ls_json_string("(无可推荐主体)"));
        }
        ls_json_array_add(arr, it);
    }
    ls_invoice_free_licenses(lics, n);
    ls_json* data = ls_json_object();
    ls_json_set(data, "recommendations", arr);
    return ls_rpc_ok(data, "开票建议 %d 个场景", (int)(sizeof(cases) / sizeof(cases[0])));
}

static ls_json* rpc_cmd_rooms(ls_json* args)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/room_pricing.json", rpc_data_dir());
    ls_room_price* rooms = NULL;
    int n = 0;
    if (ls_pricing_load_rooms(path, &rooms, &n) < 0)
        return ls_rpc_err("客房数据加载失败: %s", path);

    const char* b  = rpc_arg_or_null(rpc_arg(args, 0));
    const char* rt = rpc_arg_or_null(rpc_arg(args, 1));
    const char* cat = rpc_arg_or_null(rpc_arg(args, 2));
    ls_customer_type ct = (ls_customer_type)-1;
    const char* cts = rpc_arg(args, 3);
    if (cts && cts[0])
        ct = strcmp(cts, "study_travel") == 0 ? LS_CUST_STUDY_TRAVEL : LS_CUST_GOV_ENTERPRISE;

    ls_room_price* out = (ls_room_price*)calloc((size_t)(n ? n : 1), sizeof(ls_room_price));
    int cnt = ls_pricing_query_rooms(rooms, n, b, rt, cat, ct, out, n);
    ls_json* arr = ls_json_array();
    int i;
    for (i = 0; i < cnt; i++) ls_json_array_add(arr, ls_pricing_room_to_json(&out[i]));
    free(out);
    ls_pricing_free_rooms(rooms);

    ls_json* data = ls_json_object();
    ls_json_set(data, "count", ls_json_number(cnt));
    ls_json_set(data, "rooms", arr);
    return ls_rpc_ok(data, "共 %d 条客房报价", cnt);
}

static ls_json* rpc_cmd_venues(ls_json* args)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/venue_pricing.json", rpc_data_dir());
    ls_venue_price* venues = NULL;
    int n = 0;
    if (ls_pricing_load_venues(path, &venues, &n) < 0)
        return ls_rpc_err("会场数据加载失败: %s", path);

    const char* area = rpc_arg_or_null(rpc_arg(args, 0));
    const char* name = rpc_arg_or_null(rpc_arg(args, 1));
    int min_cap = 0;
    const char* mc = rpc_arg(args, 2);
    if (mc && mc[0]) min_cap = atoi(mc);

    ls_venue_price* out = (ls_venue_price*)calloc((size_t)(n ? n : 1), sizeof(ls_venue_price));
    int cnt = ls_pricing_query_venues(venues, n, area, name, min_cap, out, n);
    ls_json* arr = ls_json_array();
    int i;
    for (i = 0; i < cnt; i++) ls_json_array_add(arr, ls_pricing_venue_to_json(&out[i]));
    free(out);
    ls_pricing_free_venues(venues);

    ls_json* data = ls_json_object();
    ls_json_set(data, "count", ls_json_number(cnt));
    ls_json_set(data, "venues", arr);
    return ls_rpc_ok(data, "共 %d 个会场", cnt);
}

static ls_json* rpc_cmd_quote(ls_json* args)
{
    (void)args;
    char path[512];
    ls_time_rules tr;
    snprintf(path, sizeof(path), "%s/time_rules.json", rpc_data_dir());
    if (ls_pricing_load_time_rules(path, &tr) < 0) {
        memset(&tr, 0, sizeof(tr));
        tr.half_day_hours = 4;
        tr.evening_surcharge_pct = 40;
    }
    ls_room_price* rooms = NULL;
    ls_venue_price* venues = NULL;
    int nr = 0, nv = 0;
    snprintf(path, sizeof(path), "%s/room_pricing.json", rpc_data_dir());
    int ok1 = ls_pricing_load_rooms(path, &rooms, &nr);
    snprintf(path, sizeof(path), "%s/venue_pricing.json", rpc_data_dir());
    int ok2 = ls_pricing_load_venues(path, &venues, &nv);
    if (ok1 < 0 || ok2 < 0) {
        free(rooms); free(venues);
        return ls_rpc_err("报价数据加载失败");
    }

    ls_json* scen = ls_json_array();

    /* 场景A: 政府企业团队年会 */
    ls_room_price qrooms[8];
    int nq = ls_pricing_query_rooms(rooms, nr, "商务楼", "大床房", "含早",
                                      LS_CUST_GOV_ENTERPRISE, qrooms, 8);
    double room_cost = (nq > 0) ? ls_pricing_calc_room(&qrooms[0], 1, 20) : 0;
    ls_venue_price qvenues[4];
    int nqv = ls_pricing_query_venues(venues, nv, NULL, "大礼堂", 0, qvenues, 4);
    double venue_cost = (nqv > 0) ? ls_pricing_calc_venue(&qvenues[0], 1, 0, 0,
                            1, 0, 12000, &tr) : 0;
    ls_json* a = ls_json_object();
    ls_json_set(a, "name", ls_json_string("A. 政府企业团队年会"));
    ls_json_set(a, "room_cost", ls_json_number(room_cost));
    ls_json_set(a, "venue_cost", ls_json_number(venue_cost));
    ls_json_set(a, "total", ls_json_number(room_cost + venue_cost));
    ls_json_array_add(scen, a);

    /* 场景B: 研学团队会议室 */
    nqv = ls_pricing_query_venues(venues, nv, "三层", NULL, 0, qvenues, 4);
    double b_cost = (nqv > 0) ? ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 0, &tr) : 0;
    a = ls_json_object();
    ls_json_set(a, "name", ls_json_string("B. 研学团队会议室(半天)"));
    ls_json_set(a, "venue_cost", ls_json_number(b_cost));
    ls_json_array_add(scen, a);

    /* 场景C: 水晶厅满额减免 */
    nqv = ls_pricing_query_venues(venues, nv, NULL, "水晶厅（会议/用餐）", 0, qvenues, 4);
    a = ls_json_object();
    ls_json_set(a, "name", ls_json_string("C. 水晶厅会议+用餐(满15000免场地)"));
    if (nqv > 0) {
        double before = ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 0, &tr);
        double after  = ls_pricing_calc_venue(&qvenues[0], 0, 0, 0, 0, 0, 15000, &tr);
        ls_json_set(a, "before_discount", ls_json_number(before));
        ls_json_set(a, "after_discount", ls_json_number(after));
    }
    ls_json_array_add(scen, a);

    ls_pricing_free_rooms(rooms);
    ls_pricing_free_venues(venues);

    ls_json* data = ls_json_object();
    ls_json_set(data, "scenarios", scen);
    ls_json* trj = ls_json_object();
    ls_json_set(trj, "half_day_hours", ls_json_number(tr.half_day_hours));
    ls_json_set(trj, "evening_surcharge_pct", ls_json_number(tr.evening_surcharge_pct));
    ls_json_set(data, "time_rules", trj);
    return ls_rpc_ok(data, "智能报价完成(3 个场景)");
}

static void register_rpc_handlers(void)
{
    ls_rpc_register("ping", rpc_cmd_ping);
    ls_rpc_register("help", rpc_cmd_help);
    ls_rpc_register("crawl", rpc_cmd_crawl);
    ls_rpc_register("leads", rpc_cmd_leads);
    ls_rpc_register("score", rpc_cmd_score);
    ls_rpc_register("dispatch", rpc_cmd_dispatch);
    ls_rpc_register("tasks", rpc_cmd_tasks);
    ls_rpc_register("sync", rpc_cmd_sync);
    ls_rpc_register("invoice", rpc_cmd_invoice);
    ls_rpc_register("rooms", rpc_cmd_rooms);
    ls_rpc_register("venues", rpc_cmd_venues);
    ls_rpc_register("quote", rpc_cmd_quote);
}

/* 服务器模式下后台启动 HTTP 看板 */
static void* dashboard_thread_fn(void* arg)
{
    (void)arg;
    ls_dashboard_start(8080, DATA_DIR);
    return NULL;
}

static int run_command(const char* line)
{
    if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0 ||
        strcmp(line, "?") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(line, "crawl") == 0)      { cmd_crawl();     return 0; }
    if (strcmp(line, "leads") == 0)      { cmd_leads();     return 0; }
    if (strcmp(line, "score") == 0)      { cmd_score();     return 0; }
    if (strcmp(line, "dispatch") == 0)   { cmd_dispatch();  return 0; }
    if (strcmp(line, "tasks") == 0)      { cmd_tasks();     return 0; }
    if (strcmp(line, "sync") == 0)       { cmd_sync();      return 0; }
    if (strcmp(line, "invoice") == 0)    { cmd_invoice();   return 0; }
    if (strcmp(line, "rooms") == 0)      { cmd_rooms();     return 0; }
    if (strcmp(line, "venues") == 0)     { cmd_venues();    return 0; }
    if (strcmp(line, "quote") == 0)      { cmd_quote();     return 0; }
    if (strcmp(line, "dashboard") == 0)  { cmd_dashboard(); return 0; }
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 ||
        strcmp(line, "q") == 0) {
        return 1;
    }
    printf("  未知命令: %s (输入 help 查看帮助)\n", line);
    return 0;
}

int main(int argc, char** argv)
{
    srand((unsigned)time(NULL) ^ (unsigned)ls_now_ms());
    ls_log_init(DATA_DIR "/logs", LS_LOG_INFO);

    /* 网络服务器模式: longshun server [port] */
    if (argc >= 2 && strcmp(argv[1], "server") == 0) {
        register_rpc_handlers();
        int port = (argc >= 3) ? atoi(argv[2]) : LS_RPC_DEFAULT_PORT;
        if (port <= 0 || port > 65535) port = LS_RPC_DEFAULT_PORT;
        /* 后台线程启动 HTTP 看板(端口 8080) */
        LS_THREAD dash_t;
        ls_thread_create(&dash_t, dashboard_thread_fn, NULL);
        return ls_rpc_server_start(port, DATA_DIR);
    }

    /* 网络客户端模式: longshun client [-h 主机] [-p 端口] <cmd> [args...] */
    if (argc >= 2 && strcmp(argv[1], "client") == 0) {
        const char* host = "127.0.0.1";
        int port = LS_RPC_DEFAULT_PORT;
        const char* cmd = NULL;
        ls_json* args = ls_json_array();
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
                host = argv[++i];
            } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            } else if (!cmd) {
                cmd = argv[i];
            } else {
                ls_json_array_add(args, ls_json_string(argv[i]));
            }
        }
        if (!cmd) {
            printf("用法: longshun client [-h 主机] [-p 端口] <命令> [参数...]\n");
            printf("示例: longshun client rooms 商务楼\n");
            printf("      longshun client -h 192.168.1.5 -p 9000 quote\n");
            return -1;
        }
        return ls_rpc_client_run(host, port, cmd, args);
    }

    /* 命令行参数模式: longshun crawl / leads / score / ... */
    if (argc > 1) {
        return run_command(argv[1]);
    }

    print_banner();
    print_help();

    char line[256];
    for (;;) {
        printf("\nls> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        ls_trim(line);
        if (line[0] == '\0') continue;
        if (run_command(line) == 1) break;
    }
    printf("\n再见!\n");
    return 0;
}

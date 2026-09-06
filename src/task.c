/*
 * task.c - 业务协同任务分派引擎实现
 * 状态机: PENDING -> RUNNING -> DONE
 *                        \-> FAILED -> (retry) RUNNING
 */
#include "task.h"
#include "log.h"
#include "storage.h"
#include "json.h"

const char* ls_task_status_name(ls_task_status s)
{
    switch (s) {
        case TASK_STATUS_PENDING: return "待处理";
        case TASK_STATUS_RUNNING: return "处理中";
        case TASK_STATUS_DONE:    return "已完成";
        case TASK_STATUS_FAILED:  return "失败";
        case TASK_STATUS_RETRY:   return "重试中";
        default: return "未知";
    }
}

const char* ls_module_name(ls_biz_module m)
{
    switch (m) {
        case MODULE_HOTEL:       return "酒店部";
        case MODULE_CATERING:    return "餐饮部";
        case MODULE_MEETING:     return "会议部";
        case MODULE_MAINTENANCE: return "维修工程部";
        case MODULE_PURCHASE:    return "采购部";
        case MODULE_FINANCE:     return "财务部";
        default: return "未知";
    }
}

/* ---------------- 任务队列 ---------------- */

void ls_task_queue_init(ls_task_queue* q)
{
    q->head = NULL;
    q->count = 0;
    ls_mutex_init(&q->lock);
}

void ls_task_queue_push(ls_task_queue* q, ls_task* t)
{
    if (!t) return;
    ls_mutex_lock(&q->lock);
    t->next = NULL;
    /* 按优先级降序插入,同优先级追加到尾部(先进先出) */
    ls_task** pp = &q->head;
    while (*pp && (*pp)->priority >= t->priority) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
    q->count++;
    ls_mutex_unlock(&q->lock);
}

ls_task* ls_task_queue_pop(ls_task_queue* q)
{
    ls_task* t = NULL;
    ls_mutex_lock(&q->lock);
    if (q->head) {
        t = q->head;
        q->head = t->next;
        q->count--;
    }
    ls_mutex_unlock(&q->lock);
    return t;
}

int ls_task_queue_count(ls_task_queue* q)
{
    int n;
    ls_mutex_lock(&q->lock);
    n = q->count;
    ls_mutex_unlock(&q->lock);
    return n;
}

void ls_task_queue_destroy(ls_task_queue* q)
{
    ls_task* t = ls_task_queue_pop(q);
    while (t) {
        ls_task* next = t->next;
        ls_task_free(t);
        t = next;
    }
    ls_mutex_destroy(&q->lock);
}

/* ---------------- 任务对象 ---------------- */

ls_task* ls_task_new(const char* customer, const char* desc,
                     ls_biz_module module, int priority, int max_retry)
{
    ls_task* t = (ls_task*)calloc(1, sizeof(ls_task));
    if (!t) return NULL;
    ls_gen_id(t->id, sizeof(t->id));
    ls_safe_copy(t->customer, sizeof(t->customer), customer ? customer : "");
    ls_safe_copy(t->desc, sizeof(t->desc), desc ? desc : "");
    t->module = module;
    t->priority = priority;
    t->status = TASK_STATUS_PENDING;
    t->max_retry = max_retry;
    t->created_ms = ls_now_ms();
    t->updated_ms = t->created_ms;
    t->deadline_ms = t->created_ms + 24 * 3600 * 1000LL; /* 默认 24h */
    ls_task_trace_add(t, "任务创建,分派至 %s", ls_module_name(module));
    return t;
}

void ls_task_free(ls_task* t)
{
    free(t);
}

void ls_task_trace_add(ls_task* t, const char* fmt, ...)
{
    char note[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(note, sizeof(note), fmt, ap);
    va_end(ap);

    char ts[32];
    ls_time_str_ms(ls_now_ms(), ts, sizeof(ts));
    char line[600];
    snprintf(line, sizeof(line), "%s | %s", ts, note);

    size_t cur = strlen(t->trace);
    size_t add = strlen(line);
    if (cur + add + 3 < sizeof(t->trace)) {
        if (cur > 0) t->trace[cur++] = '\n';
        memcpy(t->trace + cur, line, add + 1);
    }
    t->updated_ms = ls_now_ms();
}

void ls_task_advance(ls_task* t, ls_task_status next, const char* note)
{
    t->status = next;
    if (note)
        ls_task_trace_add(t, "状态 -> %s (%s)", ls_task_status_name(next), note);
    else
        ls_task_trace_add(t, "状态 -> %s", ls_task_status_name(next));
}

void ls_task_print(const ls_task* t)
{
    printf("  任务[%s] 客户: %s | 模块: %s | 优先级: %d | 状态: %s\n",
           t->id, t->customer, ls_module_name(t->module), t->priority,
           ls_task_status_name(t->status));
    printf("    描述: %s\n", t->desc);
    printf("    追溯:\n");
    char* copy = ls_strdup(t->trace);
    if (copy) {
        char* line = strtok(copy, "\n");
        while (line) {
            printf("      %s\n", line);
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
}

/* ---------------- 需求拆解 ---------------- */

ls_task** ls_task_breakdown(const char* customer, const char* needs, int* out_count)
{
    ls_task** tasks = (ls_task**)calloc(16, sizeof(ls_task*));
    if (!tasks) { *out_count = 0; return NULL; }
    int n = 0;

    /* 规则表: [关键词列表, 模块, 任务模板, 权重] */
    struct {
        const char* words[8];
        ls_biz_module module;
        const char* desc;
        int weight;
    } rules[] = {
        { {"会议","年会","培训","发布会","论坛","团建","会展","研讨"}, MODULE_MEETING,
          "会议场地预订与接待方案支持", 5 },
        { {"宴会","婚宴","酒席","包厢","菜单","菜品","餐饮","堂食"}, MODULE_CATERING,
          "宴会菜单定制与餐饮排期", 4 },
        { {"客房","住宿","套房","房间","民宿"}, MODULE_HOTEL,
          "客房预订确认与房型匹配", 4 },
        { {"装修","改造","翻新","设备","维修","保养"}, MODULE_MAINTENANCE,
          "工程/设备现场评估与方案", 3 },
        { {"采购","招标","投标","预算","系统","信息化","数字化"}, MODULE_PURCHASE,
          "报价方案生成与采购询价", 3 },
        { {"开票","发票","税务","报销"}, MODULE_FINANCE,
          "开票资质与税务规则核验", 2 },
        { {"营销","推广","广告","物料","官网","小程序"}, MODULE_PURCHASE,
          "营销服务方案与物料询价", 2 }
    };

    size_t i, k;
    for (i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        int hit = 0;
        for (k = 0; k < 8 && rules[i].words[k]; k++) {
            if (needs && strstr(needs, rules[i].words[k])) hit++;
        }
        if (hit > 0 && n < 16) {
            tasks[n] = ls_task_new(customer, rules[i].desc, rules[i].module,
                                   hit + rules[i].weight, 2);
            if (tasks[n]) n++;
        }
    }

    /* 兜底: 通用商机跟进 */
    if (n == 0) {
        tasks[0] = ls_task_new(customer, "客户需求跟进与方案沟通",
                               MODULE_MEETING, 2, 2);
        if (tasks[0]) n = 1;
    }

    *out_count = n;
    return tasks;
}

/* ---------------- 执行引擎 ---------------- */

static void execute_task(ls_engine* e, ls_task* t)
{
    ls_mutex_lock(&e->stat_lock);
    e->active_count++;
    ls_mutex_unlock(&e->stat_lock);

    ls_task_advance(t, TASK_STATUS_RUNNING, "开始执行");
    LOG_INFO("[%s] 执行任务: %s (模块=%s)", t->id, t->desc, ls_module_name(t->module));

    /* 模拟任务耗时 */
    ls_sleep_ms(ls_rand_int(300, 1200));

    /* 模拟 82% 成功率;失败进入重试逻辑 */
    int success = ls_rand_int(1, 100) <= 82;

    if (success) {
        ls_task_advance(t, TASK_STATUS_DONE, "执行完成,结果已回写业务系统");
        LOG_INFO("[%s] 任务完成: %s", t->id, t->desc);
        ls_mutex_lock(&e->stat_lock);
        e->done_count++;
        ls_mutex_unlock(&e->stat_lock);
        ls_task_free(t);
    } else {
        if (t->retry_count < t->max_retry) {
            t->retry_count++;
            ls_task_advance(t, TASK_STATUS_RETRY, "执行失败,重新入队");
            LOG_WARN("[%s] 任务失败(第 %d 次),准备重试: %s",
                     t->id, t->retry_count, t->desc);
            ls_mutex_lock(&e->stat_lock);
            e->retry_count++;
            ls_mutex_unlock(&e->stat_lock);
            ls_task_queue_push(&e->queue, t);
        } else {
            ls_task_advance(t, TASK_STATUS_FAILED, "超过最大重试次数,转人工处理");
            LOG_ERROR("[%s] 任务失败转人工: %s", t->id, t->desc);
            ls_mutex_lock(&e->stat_lock);
            e->failed_count++;
            ls_mutex_unlock(&e->stat_lock);
            ls_task_free(t);
        }
    }

    ls_mutex_lock(&e->stat_lock);
    e->active_count--;
    ls_mutex_unlock(&e->stat_lock);
}

static void* task_worker(void* arg)
{
    ls_engine* e = (ls_engine*)arg;
    while (e->running) {
        ls_task* t = ls_task_queue_pop(&e->queue);
        if (!t) {
            if (ls_task_queue_count(&e->queue) == 0 && e->active_count == 0)
                break;
            ls_sleep_ms(30);
            continue;
        }
        execute_task(e, t);
    }
    return NULL;
}

int ls_engine_init(ls_engine* e, int workers)
{
    memset(e, 0, sizeof(*e));
    ls_task_queue_init(&e->queue);
    e->workers = workers > 0 ? workers : 4;
    ls_mutex_init(&e->stat_lock);
    return 0;
}

void ls_engine_submit(ls_engine* e, ls_task* t)
{
    if (t) ls_task_queue_push(&e->queue, t);
}

void ls_engine_run(ls_engine* e)
{
    e->running = 1;
    LS_THREAD* tids = (LS_THREAD*)calloc((size_t)e->workers, sizeof(LS_THREAD));
    int i;
    for (i = 0; i < e->workers; i++)
        ls_thread_create(&tids[i], task_worker, e);
    for (i = 0; i < e->workers; i++)
        ls_thread_join(tids[i]);
    free(tids);
}

void ls_engine_shutdown(ls_engine* e)
{
    e->running = 0;
    ls_task_queue_destroy(&e->queue);
    ls_mutex_destroy(&e->stat_lock);
}

int ls_task_process_leads(const char* leads_path, int workers)
{
    ls_json* root = ls_storage_load(leads_path ? leads_path : "data/leads.json");
    if (!root) {
        LOG_ERROR("线索文件不存在或解析失败: %s", leads_path ? leads_path : "data/leads.json");
        return -1;
    }
    int n = ls_json_array_size(root);
    if (n == 0) {
        LOG_WARN("线索列表为空,无需拆解任务");
        ls_json_free(root);
        return 0;
    }

    ls_engine engine;
    ls_engine_init(&engine, workers);

    int total = 0;
    int i;
    for (i = 0; i < n; i++) {
        ls_json* item = ls_json_array_get(root, i);
        const char* company = ls_json_get_string(item, "company", "未知客户");
        const char* needs = ls_json_get_string(item, "needs", "");
        int cnt = 0;
        ls_task** tasks = ls_task_breakdown(company, needs, &cnt);
        int k;
        for (k = 0; k < cnt; k++) {
            ls_engine_submit(&engine, tasks[k]);
            total++;
        }
        free(tasks);
        if (company && needs[0]) {
            LOG_INFO("客户 [%s] 需求拆解为 %d 个子任务", company, cnt);
        }
    }
    ls_json_free(root);

    LOG_INFO("任务引擎启动: 共 %d 个子任务, %d 个执行线程", total, engine.workers);
    ls_engine_run(&engine);

    printf("\n===== 任务执行统计 =====\n");
    printf("  任务总数:   %d\n", total);
    printf("  完成:       %d\n", engine.done_count);
    printf("  失败(转人工): %d\n", engine.failed_count);
    printf("  自动重试:   %d 次\n", engine.retry_count);
    printf("========================\n");

    int rc = engine.failed_count;
    ls_engine_shutdown(&engine);
    return rc;
}

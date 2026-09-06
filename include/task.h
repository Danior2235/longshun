/*
 * task.h - 业务协同任务分派引擎
 * 对应需求: 模块二「业务协同」—— 客户需求自动拆解为子任务,分派至对应业务模块;
 *          模块二「问题追溯」—— 任务状态机与时间线追溯。
 */
#ifndef LS_TASK_H
#define LS_TASK_H

#include "common.h"

typedef enum {
    TASK_STATUS_PENDING = 0,
    TASK_STATUS_RUNNING,
    TASK_STATUS_DONE,
    TASK_STATUS_FAILED,
    TASK_STATUS_RETRY
} ls_task_status;

const char* ls_task_status_name(ls_task_status s);

typedef enum {
    MODULE_HOTEL = 0,      /* 酒店部 */
    MODULE_CATERING,       /* 餐饮部 */
    MODULE_MEETING,        /* 会议部 */
    MODULE_MAINTENANCE,    /* 维修工程部 */
    MODULE_PURCHASE,       /* 采购部 */
    MODULE_FINANCE,        /* 财务部 */
    MODULE_COUNT
} ls_biz_module;

const char* ls_module_name(ls_biz_module m);

typedef struct ls_task {
    char id[32];
    char customer[64];       /* 关联客户 */
    char desc[256];          /* 任务描述 */
    ls_biz_module module;    /* 分派模块 */
    int priority;            /* 1-5,越大越优先 */
    ls_task_status status;
    int retry_count;
    int max_retry;
    int64_t deadline_ms;     /* 截止时间 */
    int64_t created_ms;
    int64_t updated_ms;
    char trace[4096];        /* 追溯时间线 */
    struct ls_task* next;
} ls_task;

/* ---------- 线程安全任务队列 ---------- */
typedef struct {
    ls_task* head;
    LS_MUTEX lock;
    int count;
} ls_task_queue;

void ls_task_queue_init(ls_task_queue* q);
void ls_task_queue_push(ls_task_queue* q, ls_task* t);
/* 按优先级弹出(priority 高的先出,先到先得) */
ls_task* ls_task_queue_pop(ls_task_queue* q);
int ls_task_queue_count(ls_task_queue* q);
void ls_task_queue_destroy(ls_task_queue* q);

/* ---------- 任务对象 ---------- */
ls_task* ls_task_new(const char* customer, const char* desc,
                     ls_biz_module module, int priority, int max_retry);
void ls_task_free(ls_task* t);
/* 追加追溯记录(带时间戳,printf 风格格式化) */
void ls_task_trace_add(ls_task* t, const char* fmt, ...);
/* 状态迁移并记录追溯 */
void ls_task_advance(ls_task* t, ls_task_status next, const char* note);
void ls_task_print(const ls_task* t);

/* ---------- 需求拆解 ---------- */
/* 根据客户需求文本拆解为子任务列表(自动匹配业务模块与优先级) */
ls_task** ls_task_breakdown(const char* customer, const char* needs, int* out_count);

/* ---------- 执行引擎 ---------- */
typedef struct {
    ls_task_queue queue;
    int workers;
    volatile int running;
    volatile int active_count;
    volatile int done_count;
    volatile int failed_count;
    volatile int retry_count;
    LS_MUTEX stat_lock;
} ls_engine;

int ls_engine_init(ls_engine* e, int workers);
void ls_engine_submit(ls_engine* e, ls_task* t);
/* 运行直到队列清空且所有任务结束,返回统计 */
void ls_engine_run(ls_engine* e);
void ls_engine_shutdown(ls_engine* e);

/* 便捷入口:从线索 JSON 文件拆解并执行全部任务 */
int ls_task_process_leads(const char* leads_path, int workers);

#endif /* LS_TASK_H */

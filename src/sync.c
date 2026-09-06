/*
 * sync.c - 状态同步中间件实现
 * 轻量事件总线: 业务模块发布状态变更事件,订阅方自动回写业务系统。
 */
#include "sync.h"
#include "log.h"
#include "storage.h"

typedef struct {
    char topic[64];
    ls_sync_cb cb;
    void* user;
} subscriber;

#define MAX_SUBSCRIBERS 32

static subscriber g_subs[MAX_SUBSCRIBERS];
static int g_sub_count = 0;
static LS_MUTEX g_lock;
static int g_inited = 0;

void ls_sync_init(void)
{
    if (!g_inited) {
        ls_mutex_init(&g_lock);
        g_inited = 1;
        ls_mutex_lock(&g_lock);
        memset(g_subs, 0, sizeof(g_subs));
        g_sub_count = 0;
        ls_mutex_unlock(&g_lock);
    }
}

int ls_sync_subscribe(const char* topic, ls_sync_cb cb, void* user)
{
    ls_sync_init();
    if (!cb) return -1;
    int rc = -1;
    ls_mutex_lock(&g_lock);
    if (g_sub_count < MAX_SUBSCRIBERS) {
        ls_safe_copy(g_subs[g_sub_count].topic, sizeof(g_subs[g_sub_count].topic),
                     topic ? topic : "");
        g_subs[g_sub_count].cb = cb;
        g_subs[g_sub_count].user = user;
        g_sub_count++;
        rc = 0;
    }
    ls_mutex_unlock(&g_lock);
    return rc;
}

void ls_sync_publish(const char* topic, ls_json* payload)
{
    ls_sync_init();
    ls_mutex_lock(&g_lock);
    int i;
    for (i = 0; i < g_sub_count; i++) {
        if (g_subs[i].topic[0] == '\0' ||
            strcmp(g_subs[i].topic, topic) == 0) {
            ls_sync_cb cb = g_subs[i].cb;
            void* user = g_subs[i].user;
            ls_mutex_unlock(&g_lock);
            cb(topic, payload, user);
            ls_mutex_lock(&g_lock);
        }
    }
    ls_mutex_unlock(&g_lock);
}

/* ---------------- 内置场景 ---------------- */

/* 更新 rooms.json 中指定房间的状态 */
static int update_room_status(const char* data_dir, const char* room_no,
                              const char* status)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/rooms.json", data_dir);
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;

    int n = ls_json_array_size(root);
    int found = 0;
    int i;
    for (i = 0; i < n; i++) {
        ls_json* r = ls_json_array_get(root, i);
        const char* no = ls_json_get_string(r, "no", "");
        if (no && strcmp(no, room_no) == 0) {
            ls_json_set(r, "status", ls_json_string(status));
            ls_json_set(r, "updated_at", ls_json_string("sync"));
            found = 1;
        }
    }
    if (!found) {
        ls_json_free(root);
        return -1;
    }
    int rc = ls_storage_save(path, root);
    ls_json_free(root);
    return rc;
}

/* 更新 inventory.json 中的库存数量 */
static int update_inventory(const char* data_dir, const char* item, int qty)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/inventory.json", data_dir);
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;

    int n = ls_json_array_size(root);
    int found = 0;
    int i;
    for (i = 0; i < n; i++) {
        ls_json* r = ls_json_array_get(root, i);
        const char* name = ls_json_get_string(r, "name", "");
        if (name && strcmp(name, item) == 0) {
            double cur = ls_json_get_number(r, "qty", 0);
            ls_json_set(r, "qty", ls_json_number(cur + qty));
            ls_json_set(r, "updated_at", ls_json_string("sync"));
            found = 1;
        }
    }
    if (!found) {
        ls_json* it = ls_json_object();
        ls_json_set(it, "name", ls_json_string(item));
        ls_json_set(it, "qty", ls_json_number(qty));
        ls_json_set(it, "updated_at", ls_json_string("sync"));
        ls_json_array_add(root, it);
    }
    int rc = ls_storage_save(path, root);
    ls_json_free(root);
    return rc;
}

void ls_sync_maintenance_handler(const char* topic, ls_json* payload, void* user)
{
    const char* room_no = ls_json_get_string(payload, "room_no", "");
    if (!room_no[0]) return;
    const char* data_dir = (const char*)user;
    if (!data_dir) data_dir = "data";

    if (strcmp(topic, "maintenance.completed") == 0) {
        int rc = update_room_status(data_dir, room_no, "maintenance");
        if (rc == 0)
            LOG_INFO("[sync] 维修完成,客房 %s 已同步置为「不可用」", room_no);
        else
            LOG_WARN("[sync] 客房 %s 未找到,同步失败", room_no);
    } else if (strcmp(topic, "maintenance.finished_repair") == 0) {
        int rc = update_room_status(data_dir, room_no, "available");
        if (rc == 0)
            LOG_INFO("[sync] 维修验收完成,客房 %s 已恢复「可售」", room_no);
        else
            LOG_WARN("[sync] 客房 %s 未找到,恢复失败", room_no);
    }
}

void ls_sync_purchase_handler(const char* topic, ls_json* payload, void* user)
{
    (void)topic;
    const char* item = ls_json_get_string(payload, "item", "");
    double qty = ls_json_get_number(payload, "qty", 0);
    if (!item[0]) return;
    const char* data_dir = (const char*)user;
    if (!data_dir) data_dir = "data";

    int rc = update_inventory(data_dir, item, (int)qty);
    if (rc == 0)
        LOG_INFO("[sync] 采购到货 [%s] +%.0f,库存已同步", item, qty);
    else
        LOG_WARN("[sync] 库存文件缺失,采购入库 %s 失败(已记录)", item);
}

/* ---------------- 演示入口 ---------------- */

int ls_sync_demo(const char* data_dir)
{
    ls_sync_init();
    ls_sync_subscribe("maintenance.completed", ls_sync_maintenance_handler, (void*)data_dir);
    ls_sync_subscribe("maintenance.finished_repair", ls_sync_maintenance_handler, (void*)data_dir);
    ls_sync_subscribe("purchase.arrived", ls_sync_purchase_handler, (void*)data_dir);

    char path[640];
    snprintf(path, sizeof(path), "%s/room_maintenance.json", data_dir);
    ls_json* root = ls_storage_load(path);
    if (!root) {
        LOG_ERROR("维修单文件不存在: %s", path);
        return -1;
    }
    int n = ls_json_array_size(root);
    int i;
    printf("\n===== 状态同步演示(事件总线) =====\n");
    for (i = 0; i < n; i++) {
        ls_json* item = ls_json_array_get(root, i);
        const char* room_no = ls_json_get_string(item, "room_no", "");
        const char* fault = ls_json_get_string(item, "fault", "");
        printf("  维修单[%s]: %s\n", room_no, fault);

        /* 模拟维修完成事件: 客房自动置为「不可用」 */
        ls_json* ev = ls_json_object();
        ls_json_set(ev, "room_no", ls_json_string(room_no));
        ls_sync_publish("maintenance.completed", ev);
        ls_json_free(ev);
    }

    /* 采购到货事件 */
    ls_json* ev = ls_json_object();
    ls_json_set(ev, "item", ls_json_string("布草四件套"));
    ls_json_set(ev, "qty", ls_json_number(50));
    ls_sync_publish("purchase.arrived", ev);
    ls_json_free(ev);

    ls_json_free(root);
    printf("==============================\n");
    return 0;
}

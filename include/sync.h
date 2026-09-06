/*
 * sync.h - 状态同步中间件(事件总线)
 * 对应需求: 模块三「信息同步」—— 维修、采购等状态实时同步到业务系统,
 * 例如客房维修完成后自动置为"不可用"。
 */
#ifndef LS_SYNC_H
#define LS_SYNC_H

#include "common.h"
#include "json.h"

/* 事件回调 */
typedef void (*ls_sync_cb)(const char* topic, ls_json* payload, void* user);

/* 初始化事件总线 */
void ls_sync_init(void);
/* 发布事件 */
void ls_sync_publish(const char* topic, ls_json* payload);
/* 订阅事件(topic 支持精确匹配,空串表示全部) */
int ls_sync_subscribe(const char* topic, ls_sync_cb cb, void* user);

/* 内置场景处理器 */
/* 维修完成 -> 客房置为不可用。payload: {"room_no":"201"} */
void ls_sync_maintenance_handler(const char* topic, ls_json* payload, void* user);
/* 采购到货 -> 库存更新(演示)。payload: {"item":"...","qty":n} */
void ls_sync_purchase_handler(const char* topic, ls_json* payload, void* user);

/* 演示入口:从 data/room_maintenance.json 读取维修单,模拟完成并同步房态 */
int ls_sync_demo(const char* data_dir);

#endif /* LS_SYNC_H */

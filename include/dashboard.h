/*
 * dashboard.h - 实时数据看板后端服务
 * 对应需求: 模块二「实时数据看板」—— 跨业态、跨渠道统一实时看板,
 * 覆盖订单、房态、利润等关键指标,并提供 HTTP JSON 接口与自然语言查询。
 */
#ifndef LS_DASHBOARD_H
#define LS_DASHBOARD_H

#include "common.h"

/* 启动看板服务(阻塞)。data_dir 为业务数据目录,port 为监听端口 */
int ls_dashboard_start(int port, const char* data_dir);

#endif /* LS_DASHBOARD_H */

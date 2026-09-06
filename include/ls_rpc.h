/*
 * ls_rpc.h - 网络 RPC 传输层(服务器 + 客户端)
 * 将原有单机 CLI 能力封装为「网络服务器 + 客户端」架构:
 *   - 服务器: 监听 TCP 端口, 接收 JSON 行协议请求, 分派命令处理器, 返回 JSON 响应
 *   - 客户端: 连接服务器, 将命令行参数打包为请求, 接收并展示结果
 *
 * 注意: 命名为 ls_rpc.h 而非 rpc.h, 避免与 MinGW 系统头文件 rpc.h 冲突。
 *
 * 协议(单行 JSON):
 *   请求  {"cmd":"rooms","args":["商务楼"]}
 *   响应  {"code":0,"msg":"共 12 条客房报价","data":{...}}
 */
#ifndef LS_RPC_H
#define LS_RPC_H

#include "common.h"
#include "json.h"

#define LS_RPC_DEFAULT_PORT 9000

/* 命令处理器: 入参为位置参数数组(JSON Array, 可为空), 返回标准响应对象 */
typedef ls_json* (*ls_rpc_handler)(ls_json* args);

/* 注册命令处理器(建议在服务器启动前调用) */
void ls_rpc_register(const char* name, ls_rpc_handler h);

/* 服务器: 阻塞监听 port, 处理 RPC 请求(每连接一线程)。正常退出返回 0 */
int ls_rpc_server_start(int port, const char* data_dir);

/* 客户端: 连接 host:port, 发送 cmd + args, 打印并返回结果。成功返回 0 */
int ls_rpc_client_run(const char* host, int port, const char* cmd, ls_json* args);

/* 服务器端数据目录(命令处理器内部读取业务数据用) */
const char* ls_rpc_data_dir(void);

/* ---------- 标准响应构造 ---------- */
/* {code:0, msg:"...", data:{...}} */
ls_json* ls_rpc_ok(ls_json* data, const char* fmt, ...);
/* {code:-1, msg:"...", data:null} */
ls_json* ls_rpc_err(const char* fmt, ...);

#endif /* LS_RPC_H */

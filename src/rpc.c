/*
 * rpc.c - 网络 RPC 传输层实现(服务器 + 客户端)
 * 复用 dashboard.c 的 socket 抽象, 实现 JSON 行协议通信。
 */
#include "ls_rpc.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if LS_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define LS_SOCK SOCKET
    #define LS_INV INVALID_SOCKET
    #define ls_sclose(s) closesocket(s)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <unistd.h>
    typedef int LS_SOCK;
    #define LS_INV (-1)
    #define ls_sclose(s) close(s)
#endif

#define LS_RPC_MAX_LINE 65536

/* ---------- 编码工具(Windows 命令行参数为 GBK, 统一转为 UTF-8) ---------- */

static char* ls_strdup_self(const char* s)
{
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int is_valid_utf8(const char* s)
{
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        int len;
        if ((*p & 0xE0) == 0xC0) len = 2;
        else if ((*p & 0xF0) == 0xE0) len = 3;
        else if ((*p & 0xF8) == 0xF0) len = 4;
        else return 0;
        int i;
        for (i = 1; i < len; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        p += len;
    }
    return 1;
}

#if LS_WINDOWS
/* 系统代码页(GBK) -> UTF-8 */
static char* acp_to_utf8(const char* in)
{
    int wlen = MultiByteToWideChar(CP_ACP, 0, in, -1, NULL, 0);
    if (wlen <= 0) return ls_strdup_self(in);
    wchar_t* w = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!w) return ls_strdup_self(in);
    MultiByteToWideChar(CP_ACP, 0, in, -1, w, wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* out = (char*)malloc((size_t)ulen);
    if (out)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, ulen, NULL, NULL);
    free(w);
    return out ? out : ls_strdup_self(in);
}
#else
static char* acp_to_utf8(const char* in) { return ls_strdup_self(in); }
#endif

/* 将位置参数数组中的字符串统一转为 UTF-8 */
static void normalize_args_encoding(ls_json* args)
{
    if (!args || args->type != LS_JSON_ARRAY) return;
    int i;
    for (i = 0; i < args->count; i++) {
        ls_json* v = args->items[i].value;
        if (!v || v->type != LS_JSON_STRING || !v->string) continue;
        if (is_valid_utf8(v->string)) continue;
        char* u8 = acp_to_utf8(v->string);
        if (u8) {
            free(v->string);
            v->string = u8;
        }
    }
}

/* ---------- 全局状态 ---------- */

#define LS_RPC_MAX_HANDLERS 32

typedef struct {
    char name[64];
    ls_rpc_handler fn;
} handler_entry;

static handler_entry g_handlers[LS_RPC_MAX_HANDLERS];
static int g_handler_count = 0;
static char g_data_dir[512] = {0};

const char* ls_rpc_data_dir(void) { return g_data_dir; }

void ls_rpc_register(const char* name, ls_rpc_handler h)
{
    /* 注册在服务器启动前单线程完成, 无需加锁 */
    if (!name || !h || g_handler_count >= LS_RPC_MAX_HANDLERS) return;
    ls_safe_copy(g_handlers[g_handler_count].name, sizeof(g_handlers[g_handler_count].name), name);
    g_handlers[g_handler_count].fn = h;
    g_handler_count++;
}

static ls_rpc_handler find_handler(const char* name)
{
    int i;
    for (i = 0; i < g_handler_count; i++) {
        if (strcmp(g_handlers[i].name, name) == 0) return g_handlers[i].fn;
    }
    return NULL;
}

/* ---------- 标准响应 ---------- */

ls_json* ls_rpc_ok(ls_json* data, const char* fmt, ...)
{
    ls_json* j = ls_json_object();
    ls_json_set(j, "code", ls_json_number(0));
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "OK", ap);
    va_end(ap);
    ls_json_set(j, "msg", ls_json_string(msg));
    ls_json_set(j, "data", data ? data : ls_json_null());
    return j;
}

ls_json* ls_rpc_err(const char* fmt, ...)
{
    ls_json* j = ls_json_object();
    ls_json_set(j, "code", ls_json_number(-1));
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "ERROR", ap);
    va_end(ap);
    ls_json_set(j, "msg", ls_json_string(msg));
    ls_json_set(j, "data", ls_json_null());
    return j;
}

/* ---------- socket 工具 ---------- */

static int socket_lib_ready(void)
{
#if LS_WINDOWS
    static int inited = 0;
    if (!inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
        inited = 1;
    }
#endif
    return 1;
}

static int send_all_bytes(LS_SOCK sock, const char* data, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int rc = (int)send(sock, data + sent, (int)(n - sent), 0);
        if (rc <= 0) return -1;
        sent += (size_t)rc;
    }
    return 0;
}

/* 读取一行(以 \n 结尾), 返回字节数(不含 \n), 失败返回 -1 */
static int recv_line(LS_SOCK sock, char* buf, size_t cap)
{
    size_t len = 0;
    while (len + 1 < cap) {
        int rc = (int)recv(sock, buf + len, 1, 0);
        if (rc <= 0) break;
        if (buf[len] == '\n') {
            buf[len] = '\0';
            return (int)len;
        }
        len += (size_t)rc;
    }
    buf[len] = '\0';
    return len > 0 ? (int)len : -1;
}

/* ---------- 命令执行 ---------- */

static ls_json* dispatch(const char* cmd, ls_json* args)
{
    ls_rpc_handler h = find_handler(cmd);
    if (!h) return ls_rpc_err("未知命令: %s (可用 help 查看)", cmd);
    return h(args);
}

/* ---------- 服务器 ---------- */

static void* server_client_thread(void* arg)
{
    LS_SOCK sock = (LS_SOCK)(intptr_t)arg;
    char req[LS_RPC_MAX_LINE];
    int n = recv_line(sock, req, sizeof(req));
    if (n > 0) {
        ls_json* reqj = ls_json_parse(req);
        ls_json* resp = NULL;
        if (!reqj) {
            resp = ls_rpc_err("请求解析失败(应为单行 JSON)");
        } else {
            const char* cmd = ls_json_get_string(reqj, "cmd", "");
            ls_json* args = ls_json_get(reqj, "args");
            /* 服务器端: 统一参数编码为 UTF-8(兼容 GBK 客户端) */
            normalize_args_encoding(args);
            resp = dispatch(cmd, args);
            ls_json_free(reqj);
        }
        char* text = ls_json_dump(resp);
        if (text) {
            size_t tl = strlen(text);
            char* frame = (char*)malloc(tl + 2);
            if (frame) {
                memcpy(frame, text, tl);
                frame[tl] = '\n';
                frame[tl + 1] = '\0';
                send_all_bytes(sock, frame, tl + 1);
                free(frame);
            }
            free(text);
        }
        ls_json_free(resp);
    }
    ls_sclose(sock);
    return NULL;
}

int ls_rpc_server_start(int port, const char* data_dir)
{
    if (!socket_lib_ready()) {
        LOG_ERROR("socket 初始化失败");
        return -1;
    }
    ls_safe_copy(g_data_dir, sizeof(g_data_dir), data_dir ? data_dir : "data");

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(NULL, port_str, &hints, &res) != 0 || !res) {
        LOG_ERROR("getaddrinfo 失败");
        return -1;
    }
    LS_SOCK listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd == LS_INV) {
        freeaddrinfo(res);
        LOG_ERROR("创建 socket 失败");
        return -1;
    }
    {
        int one = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    }
    if (bind(listen_fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ls_sclose(listen_fd);
        LOG_ERROR("绑定端口 %d 失败(可能被占用)", port);
        return -1;
    }
    freeaddrinfo(res);
    if (listen(listen_fd, 16) != 0) {
        ls_sclose(listen_fd);
        LOG_ERROR("listen 失败");
        return -1;
    }

    printf("\n");
    printf("============================================================\n");
    printf("   龙顺 RPC 服务端已启动\n");
    printf("   TCP 端口:  %d\n", port);
    printf("   数据目录:  %s\n", g_data_dir);
    printf("   已注册 %d 个命令处理器\n", g_handler_count);
    printf("   客户端:    longshun client -h 127.0.0.1 -p %d <cmd>\n", port);
    printf("   (按 Ctrl+C 停止)\n");
    printf("============================================================\n\n");
    fflush(stdout);
    LOG_INFO("RPC 服务端启动: 端口 %d, 数据目录 %s", port, g_data_dir);

    for (;;) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        LS_SOCK cfd = accept(listen_fd, (struct sockaddr*)&client, &clen);
        if (cfd == LS_INV) {
            ls_sleep_ms(10);
            continue;
        }
        LS_THREAD t;
        ls_thread_create(&t, server_client_thread, (void*)(intptr_t)cfd);
        ls_thread_join(t);
    }

    ls_sclose(listen_fd);
    return 0;
}

/* ---------- 客户端 ---------- */

int ls_rpc_client_run(const char* host, int port, const char* cmd, ls_json* args)
{
    if (!socket_lib_ready()) {
        fprintf(stderr, "socket 初始化失败\n");
        return -1;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        fprintf(stderr, "无法解析服务器地址: %s\n", host);
        return -1;
    }
    LS_SOCK sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == LS_INV) {
        freeaddrinfo(res);
        fprintf(stderr, "创建 socket 失败\n");
        return -1;
    }
    if (connect(sock, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ls_sclose(sock);
        fprintf(stderr, "连接服务器 %s:%d 失败(请确认服务端已启动)\n", host, port);
        return -1;
    }
    freeaddrinfo(res);

    /* 客户端: 将命令行参数统一转为 UTF-8 再打包 */
    if (args) normalize_args_encoding(args);

    /* 构造请求 */
    ls_json* req = ls_json_object();
    ls_json_set(req, "cmd", ls_json_string(cmd));
    ls_json_set(req, "args", args ? args : ls_json_array());
    char* text = ls_json_dump(req);
    ls_json_free(req);
    if (!text) {
        ls_sclose(sock);
        return -1;
    }

    /* 发送 + \n */
    size_t tl = strlen(text);
    char* frame = (char*)malloc(tl + 2);
    if (!frame) { free(text); ls_sclose(sock); return -1; }
    memcpy(frame, text, tl);
    frame[tl] = '\n';
    frame[tl + 1] = '\0';
    free(text);

    int rc = send_all_bytes(sock, frame, tl + 1);
    free(frame);
    if (rc != 0) {
        ls_sclose(sock);
        fprintf(stderr, "发送请求失败\n");
        return -1;
    }

    /* 接收响应 */
    char resp[LS_RPC_MAX_LINE];
    if (recv_line(sock, resp, sizeof(resp)) <= 0) {
        ls_sclose(sock);
        fprintf(stderr, "未收到服务器响应\n");
        return -1;
    }
    ls_sclose(sock);

    /* 展示 */
    ls_json* rj = ls_json_parse(resp);
    int ret = 0;
    if (!rj) {
        printf("  (原始响应) %s\n", resp);
        return -1;
    }
    double code = ls_json_get_number(rj, "code", -1);
    const char* msg = ls_json_get_string(rj, "msg", "");
    ls_json* data = ls_json_get(rj, "data");
    if (code != 0) {
        printf("[失败] %s\n", msg);
        ret = -1;
    } else {
        printf("[OK] %s\n", msg);
        if (data && data->type != LS_JSON_NULL) {
            char* pretty = ls_json_dump_pretty(data);
            if (pretty) {
                printf("%s\n", pretty);
                free(pretty);
            }
        }
    }
    ls_json_free(rj);
    return ret;
}

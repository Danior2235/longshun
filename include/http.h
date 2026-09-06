/*
 * http.h - 零依赖 HTTP/1.1 客户端(爬虫采集的传输层)
 * 基于原生 socket 实现:Windows 使用 WinSock2,其他平台使用 POSIX socket。
 * 支持超时控制与重定向。HTTPS(443)需接入 TLS 层,当前版本返回明确错误,
 * 可扩展 OpenSSL/Windows SSPI 支持。
 */
#ifndef LS_HTTP_H
#define LS_HTTP_H

#include "common.h"

typedef struct {
    int status_code;      /* 200/404/... ,0 表示网络错误 */
    char* body;           /* 响应体(不含 chunked 分帧标记) */
    size_t body_len;
    char* err;            /* 非 NULL 表示出错信息 */
} ls_http_resp;

typedef struct {
    int timeout_ms;       /* 连接/读取超时,默认 5000 */
    int max_redirects;    /* 最大重定向次数,默认 3 */
    const char* user_agent;
    const char* extra_headers; /* 可选,形如 "X-A: 1\r\nX-B: 2\r\n",或 NULL */
} ls_http_opts;

void ls_http_resp_free(ls_http_resp* r);

/* GET 请求,返回堆上对象,调用方负责释放 */
ls_http_resp* ls_http_get(const char* url, const ls_http_opts* opts);
/* POST 请求,body 可为 NULL */
ls_http_resp* ls_http_post(const char* url, const char* body, const ls_http_opts* opts);
/* POST 请求,支持二进制 body(带长度, 用于上传音频等) */
ls_http_resp* ls_http_post_bytes(const char* url, const char* body, size_t body_len, const ls_http_opts* opts);

#endif /* LS_HTTP_H */

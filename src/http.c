/*
 * http.c - 零依赖 HTTP/1.1 客户端实现
 * 支持: URL 解析、IPv4/IPv6(经 getaddrinfo)、连接/读取超时、重定向、chunked 解码。
 * 说明: HTTPS 需要 TLS 层,此处给出错误提示;生产环境可接入 OpenSSL。
 */
#include "http.h"
#include "log.h"

#if LS_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <winhttp.h>
    typedef int socklen_t;
    #define LS_SOCKET SOCKET
    #define LS_INVALID_SOCKET INVALID_SOCKET
    #define ls_sock_close(s) closesocket(s)
    #define ls_sock_last_error() WSAGetLastError()
    #define LS_EWOULDBLOCK WSAEWOULDBLOCK
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <poll.h>
    typedef int LS_SOCKET;
    #define LS_INVALID_SOCKET (-1)
    #define ls_sock_close(s) close(s)
    #define ls_sock_last_error() errno
    #define LS_EWOULDBLOCK EWOULDBLOCK
#endif

/* 全局 socket 库初始化(进程级一次,WSAStartup 引用计数幂等) */
static int g_wsa_init = 0;

static void ensure_socket_lib(void)
{
#if LS_WINDOWS
    if (!g_wsa_init) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) g_wsa_init = 1;
    }
#else
    g_wsa_init = 1;
#endif
}

void ls_http_resp_free(ls_http_resp* r)
{
    if (!r) return;
    free(r->body);
    free(r->err);
    free(r);
}

typedef struct {
    char scheme[16];
    char host[256];
    char port[8];
    char path[1024];
} parsed_url;

/* 解析 URL,返回 0 成功 */
static int parse_url(const char* url, parsed_url* pu)
{
    memset(pu, 0, sizeof(*pu));
    ls_safe_copy(pu->scheme, sizeof(pu->scheme), "http");
    const char* rest = url;

    const char* scheme_sep = strstr(url, "://");
    if (scheme_sep) {
        if ((size_t)(scheme_sep - url) >= sizeof(pu->scheme)) return -1;
        memcpy(pu->scheme, url, (size_t)(scheme_sep - url));
        pu->scheme[scheme_sep - url] = '\0';
        rest = scheme_sep + 3;
    }
    if (strcmp(pu->scheme, "http") != 0 && strcmp(pu->scheme, "https") != 0)
        return -1;

    /* host[:port] 直到 / 或结束 */
    const char* host_end = strchr(rest, '/');
    const char* port_sep = strchr(rest, ':');

    if (!host_end) {
        /* 只有 host[:port] */
        if (port_sep) {
            size_t hl = (size_t)(port_sep - rest);
            if (hl >= sizeof(pu->host)) return -1;
            memcpy(pu->host, rest, hl);
            pu->host[hl] = '\0';
            ls_safe_copy(pu->port, sizeof(pu->port), port_sep + 1);
            ls_safe_copy(pu->path, sizeof(pu->path), "/");
        } else {
            ls_safe_copy(pu->host, sizeof(pu->host), rest);
            ls_safe_copy(pu->path, sizeof(pu->path), "/");
        }
    } else {
        size_t hl = (size_t)(host_end - rest);
        if (hl >= sizeof(pu->host)) return -1;
        memcpy(pu->host, rest, hl);
        pu->host[hl] = '\0';
        ls_safe_copy(pu->path, sizeof(pu->path), host_end);
        /* 去掉 host 中的端口 */
        char* psep = strchr(pu->host, ':');
        if (psep) {
            ls_safe_copy(pu->port, sizeof(pu->port), psep + 1);
            *psep = '\0';
        }
    }

    /* 处理 IPv6 字面地址 [::1] */
    if (pu->host[0] == '[') {
        char* close_b = strchr(pu->host, ']');
        if (!close_b) return -1;
        size_t hl = (size_t)(close_b - pu->host - 1);
        char tmp[256];
        memcpy(tmp, pu->host + 1, hl);
        tmp[hl] = '\0';
        ls_safe_copy(pu->host, sizeof(pu->host), tmp);
        if (close_b[1] == ':') ls_safe_copy(pu->port, sizeof(pu->port), close_b + 2);
    }

    if (pu->port[0] == '\0') {
        if (strcmp(pu->scheme, "https") == 0) ls_safe_copy(pu->port, sizeof(pu->port), "443");
        else ls_safe_copy(pu->port, sizeof(pu->port), "80");
    }
    if (pu->path[0] == '\0') ls_safe_copy(pu->path, sizeof(pu->path), "/");
    return 0;
}

/* 带超时的连接 */
static LS_SOCKET connect_with_timeout(const parsed_url* pu, int timeout_ms)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(pu->host, pu->port, &hints, &res) != 0 || !res)
        return LS_INVALID_SOCKET;

    LS_SOCKET sock = LS_INVALID_SOCKET;
    struct addrinfo* it;
    for (it = res; it; it = it->ai_next) {
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock == LS_INVALID_SOCKET) continue;

        /* 非阻塞连接 */
#if LS_WINDOWS
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        int rc = connect(sock, it->ai_addr, (socklen_t)it->ai_addrlen);
        if (rc != 0) {
#if LS_WINDOWS
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                ls_sock_close(sock);
                sock = LS_INVALID_SOCKET;
                continue;
            }
#else
            if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
                ls_sock_close(sock);
                sock = LS_INVALID_SOCKET;
                continue;
            }
#endif
            /* 等待可写 */
#if LS_WINDOWS
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            rc = select(0, NULL, &wfds, NULL, &tv);
#else
            struct pollfd pfd;
            pfd.fd = sock;
            pfd.events = POLLOUT;
            rc = poll(&pfd, 1, timeout_ms);
#endif
            if (rc <= 0) {
                ls_sock_close(sock);
                sock = LS_INVALID_SOCKET;
                continue;
            }
            /* 检查连接结果 */
            int soerr = 0;
            socklen_t sl = sizeof(soerr);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
            if (soerr != 0) {
                ls_sock_close(sock);
                sock = LS_INVALID_SOCKET;
                continue;
            }
        }
        /* 恢复阻塞模式并设置 TCP_NODELAY */
#if LS_WINDOWS
        u_long mode0 = 0;
        ioctlsocket(sock, FIONBIO, &mode0);
#else
        int fl = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, fl & ~O_NONBLOCK);
#endif
        {
            int one = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

/* 单次读写超时的 send/recv 封装(阻塞模式 + SO_RCVTIMEO/SO_SNDTIMEO) */
static int set_io_timeouts(LS_SOCKET sock, int timeout_ms)
{
#if LS_WINDOWS
    DWORD t = (DWORD)timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
}

static int send_all(LS_SOCKET sock, const char* data, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int rc = (int)send(sock, data + sent, (int)(n - sent), 0);
        if (rc <= 0) return -1;
        sent += (size_t)rc;
    }
    return 0;
}

/* 读取一行(直到 \n),返回行长(不含 \r\n),-1 出错,0 为读到空行 */
static int read_line(LS_SOCKET sock, char* buf, size_t cap)
{
    size_t len = 0;
    char c;
    for (;;) {
        int rc = (int)recv(sock, &c, 1, 0);
        if (rc <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r' && len + 1 < cap) buf[len++] = c;
    }
    buf[len] = '\0';
    return (int)len;
}

/* 从响应体中解析 header(小写 key),返回 headers 文本 */
static int parse_headers(LS_SOCKET sock, char* first_line, size_t fl_cap,
                         char* headers_out, size_t headers_cap)
{
    if (read_line(sock, first_line, fl_cap) < 0) return -1;
    size_t used = 0;
    for (;;) {
        char line[1024];
        int n = read_line(sock, line, sizeof(line));
        if (n < 0) return -1;
        if (n == 0) break; /* 空行,header 结束 */
        if (used + (size_t)n + 3 < headers_cap) {
            memcpy(headers_out + used, line, (size_t)n);
            used += (size_t)n;
            headers_out[used++] = '\n';
        }
    }
    headers_out[used] = '\0';
    return 0;
}

static const char* header_find(const char* headers, const char* name)
{
    size_t nl = strlen(name);
    const char* p = headers;
    while (*p) {
        const char* eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
        if (line_len > nl && ls_strnicmp(p, name, nl) == 0 && p[nl] == ':')
            return p + nl + 1;
        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

/* chunked 解码,返回新分配 body */
static int decode_chunked(LS_SOCKET sock, char** out, size_t* out_len)
{
    size_t cap = 8192, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return -1;

    for (;;) {
        char line[64];
        if (read_line(sock, line, sizeof(line)) < 0) { free(buf); return -1; }
        /* 去掉可能的 ;扩展 */
        char* semi = strchr(line, ';');
        if (semi) *semi = '\0';
        long chunk = strtol(line, NULL, 16);
        if (chunk == 0) {
            /* 读取 trailer 直到空行 */
            char tline[1024];
            while (read_line(sock, tline, sizeof(tline)) > 0) { /* ignore */ }
            break;
        }
        /* 读取 chunk 数据 */
        size_t remaining = (size_t)chunk;
        while (remaining > 0) {
            if (len + remaining + 1 > cap) {
                while (cap < len + remaining + 1) cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); return -1; }
                buf = nb;
            }
            int rc = (int)recv(sock, buf + len, (int)remaining, 0);
            if (rc <= 0) { free(buf); return -1; }
            len += (size_t)rc;
            remaining -= (size_t)rc;
        }
        /* 读 chunk 结束 CRLF */
        read_line(sock, line, sizeof(line));
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return 0;
}

/* 读取 content-length 定义的 body */
static int read_body(LS_SOCKET sock, size_t content_len, char** out, size_t* out_len)
{
    size_t cap = content_len ? content_len : 8192;
    if (cap < 1024) cap = 1024;
    char* buf = (char*)malloc(cap + 1);
    if (!buf) return -1;
    size_t len = 0;
    while (len < content_len) {
        size_t want = content_len - len;
        int rc = (int)recv(sock, buf + len, (int)want, 0);
        if (rc <= 0) break;
        len += (size_t)rc;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return 0;
}

static ls_http_resp* request(const char* url, const char* method,
                             const char* body, const ls_http_opts* opts)
{
    ensure_socket_lib();

    ls_http_opts def;
    if (!opts) {
        memset(&def, 0, sizeof(def));
        def.timeout_ms = 5000;
        def.max_redirects = 3;
        def.user_agent = "LongShun-AI-Assistant/1.0";
        opts = &def;
    }

    parsed_url pu;
    if (parse_url(url, &pu) != 0) {
        ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
        r->err = ls_strdup("非法 URL");
        return r;
    }

    if (strcmp(pu.scheme, "https") == 0) {
        ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
        r->err = ls_strdup("当前构建未启用 TLS,无法访问 HTTPS;请接入 OpenSSL 或改用 HTTP");
        return r;
    }

    LS_SOCKET sock = connect_with_timeout(&pu, opts->timeout_ms);
    if (sock == LS_INVALID_SOCKET) {
        ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
        r->err = ls_strdup("连接失败(无法解析主机或连接超时)");
        return r;
    }
    set_io_timeouts(sock, opts->timeout_ms);

    char req[4096];
    size_t req_len = 0;
    req_len += (size_t)snprintf(req + req_len, sizeof(req) - req_len,
        "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\nAccept: */*\r\n",
        method, pu.path, pu.host, opts->user_agent ? opts->user_agent : "LongShun-AI/1.0");
    if (body) {
        req_len += (size_t)snprintf(req + req_len, sizeof(req) - req_len,
            "Content-Type: application/json\r\nContent-Length: %u\r\n",
            (unsigned)strlen(body));
    }
    if (opts->extra_headers)
        req_len += (size_t)snprintf(req + req_len, sizeof(req) - req_len, "%s", opts->extra_headers);
    req_len += (size_t)snprintf(req + req_len, sizeof(req) - req_len, "\r\n");
    if (body)
        req_len += (size_t)snprintf(req + req_len, sizeof(req) - req_len, "%s", body);

    if (send_all(sock, req, req_len) != 0) {
        ls_sock_close(sock);
        ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
        r->err = ls_strdup("发送请求失败");
        return r;
    }

    char first_line[1024];
    char headers[8192];
    if (parse_headers(sock, first_line, sizeof(first_line), headers, sizeof(headers)) != 0) {
        ls_sock_close(sock);
        ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
        r->err = ls_strdup("读取响应头失败");
        return r;
    }

    int status = 0;
    if (sscanf(first_line, "HTTP/%*s %d", &status) != 1) status = 0;

    ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
    r->status_code = status;

    const char* loc = header_find(headers, "location");
    int redirecting = 0;
    if (status >= 300 && status < 400 && loc) {
        int remaining = opts->max_redirects;
        if (remaining > 0) {
            char new_url[1024];
            if (loc[0] == '/') {
                snprintf(new_url, sizeof(new_url), "%s://%s%s",
                         pu.scheme, pu.host, loc);
            } else {
                ls_safe_copy(new_url, sizeof(new_url), loc);
            }
            redirecting = 1;
            free(r->err);
            r->err = ls_strdup(new_url); /* 借用 err 传递重定向地址 */
            LOG_DEBUG("重定向 %d -> %s", status, new_url);
        }
    }

    if (redirecting) {
        ls_sock_close(sock);
        /* 重新发起请求(最多 max_redirects 次由调用方循环) */
        return r;
    }

    /* 解析 chunked 或 content-length */
    const char* te = header_find(headers, "transfer-encoding");
    const char* cl = header_find(headers, "content-length");
    if (te && strstr(te, "chunked")) {
        if (decode_chunked(sock, &r->body, &r->body_len) != 0)
            r->err = ls_strdup("chunked 解码失败");
    } else if (cl) {
        size_t len = (size_t)atol(cl);
        if (read_body(sock, len, &r->body, &r->body_len) != 0)
            r->err = ls_strdup("读取响应体失败");
    } else {
        /* 读到连接关闭 */
        size_t cap = 8192, len = 0;
        char* buf = (char*)malloc(cap);
        if (buf) {
            for (;;) {
                if (len + 1025 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                int rc = (int)recv(sock, buf + len, 1024, 0);
                if (rc <= 0) break;
                len += (size_t)rc;
            }
            buf[len] = '\0';
            r->body = buf;
            r->body_len = len;
        }
    }

    ls_sock_close(sock);
    return r;
}

/* ============================================================
 *  HTTPS 支持(WinHTTP): 用于调用云端语音识别等 TLS 接口
 * ============================================================ */

/* 使用 Windows WinHTTP 发起 HTTPS/HTTP 请求(GET/POST, 可带原始 body)
 * 返回堆上 ls_http_resp, 调用方负责 ls_http_resp_free */
static ls_http_resp* winhttp_request(const char* url, const char* method,
                                     const char* body, size_t body_len,
                                     const ls_http_opts* opts)
{
    ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
    if (!r) return NULL;
#if !LS_WINDOWS
    r->err = ls_strdup("HTTPS 仅在 Windows 平台支持(WinHTTP)");
    return r;
#else
    char url_copy[2048];
    ls_safe_copy(url_copy, sizeof(url_copy), url);
    char* scheme_sep = strstr(url_copy, "://");
    if (!scheme_sep) { r->err = ls_strdup("非法 URL"); return r; }
    int use_https = (strncmp(url_copy, "https", 5) == 0);
    const char* rest = scheme_sep + 3;
    const char* path = strchr(rest, '/');
    char host[512] = "";
    char port[16] = "";
    if (path) {
        size_t hl = (size_t)(path - rest);
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        memcpy(host, rest, hl); host[hl] = '\0';
    } else {
        ls_safe_copy(host, sizeof(host), rest);
    }
    /* 分离 host 中的端口 */
    char* psep = strchr(host, ':');
    if (psep) { ls_safe_copy(port, sizeof(port), psep + 1); *psep = '\0'; }
    if (port[0] == '\0') ls_safe_copy(port, sizeof(port),
        use_https ? "443" : "80");

    int timeout_ms = opts ? opts->timeout_ms : 5000;

    HINTERNET hSession = WinHttpOpen(L"LongShun-AI-Assistant/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { r->err = ls_strdup("WinHttpOpen 失败"); return r; }

    /* host/port 转宽字符 */
    WCHAR whost[512], wport[16];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 512);
    MultiByteToWideChar(CP_UTF8, 0, port, -1, wport, 16);

    HINTERNET hConnect = WinHttpConnect(hSession, whost,
        (INTERNET_PORT)_wtoi(wport), 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); r->err = ls_strdup("WinHttpConnect 失败"); return r; }

    /* 拼路径(含 query) */
    const char* p = path ? path : "/";
    WCHAR wpath[2048];
    int wl = MultiByteToWideChar(CP_UTF8, 0, p, -1, wpath, 2048);
    if (wl <= 0) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); r->err = ls_strdup("URL 转码失败"); return r; }

    const wchar_t* wmethod = (strcmp(method, "GET") == 0) ? L"GET" : L"POST";

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod, wpath,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        use_https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        r->err = ls_strdup("WinHttpOpenRequest 失败");
        return r;
    }
    if (timeout_ms > 0) {
        DWORD t = (DWORD)timeout_ms;
        WinHttpSetTimeouts(hRequest, t, t, t, t);
    }

    /* 设置 Content-Type */
    if (body) {
        WCHAR wctype[160];
        MultiByteToWideChar(CP_UTF8, 0, "Content-Type: application/json", -1, wctype, 160);
        WinHttpAddRequestHeaders(hRequest, wctype, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!WinHttpSendRequest(hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
            body ? (DWORD)body_len : 0,
            body ? (DWORD)body_len : 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        r->err = ls_strdup("WinHttpSendRequest 失败");
        return r;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        r->err = ls_strdup("WinHttpReceiveResponse 失败");
        return r;
    }

    /* 读取状态码 */
    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
    r->status_code = (int)status;

    /* 读取 body */
    size_t cap = 8192, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); r->err = ls_strdup("内存不足"); return r; }
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        if (len + avail + 1 > cap) {
            while (cap < len + avail + 1) cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); r->err = ls_strdup("内存不足"); return r; }
            buf = nb;
        }
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, buf + len, avail, &got)) break;
        len += (size_t)got;
    }
    buf[len] = '\0';
    r->body = buf;
    r->body_len = len;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return r;
#endif
}

ls_http_resp* ls_http_get(const char* url, const ls_http_opts* opts)
{
    if (url && (strncmp(url, "https://", 8) == 0))
        return winhttp_request(url, "GET", NULL, 0, opts);

    ls_http_resp* r = request(url, "GET", NULL, opts);
    /* 处理重定向链 */
    int hops = 0;
    while (r && r->status_code >= 300 && r->status_code < 400 && r->err &&
           !r->body) {
        const char* target = r->err;
        char buf[1024];
        ls_safe_copy(buf, sizeof(buf), target);
        ls_http_resp_free(r);
        if (++hops > 5) {
            r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
            r->err = ls_strdup("重定向次数过多");
            break;
        }
        r = request(buf, "GET", NULL, opts);
    }
    return r;
}

ls_http_resp* ls_http_post(const char* url, const char* body, const ls_http_opts* opts)
{
    if (url && (strncmp(url, "https://", 8) == 0))
        return winhttp_request(url, "POST", body, body ? strlen(body) : 0, opts);
    return request(url, "POST", body, opts);
}

/* 带二进制长度的 POST(用于上传音频等, 支持 https) */
ls_http_resp* ls_http_post_bytes(const char* url, const char* body, size_t body_len, const ls_http_opts* opts)
{
    if (url && (strncmp(url, "https://", 8) == 0))
        return winhttp_request(url, "POST", body, body_len, opts);
    /* 非 HTTPS 分支: 现有 socket 实现不支持二进制长度, 直接拒绝 */
    ls_http_resp* r = (ls_http_resp*)calloc(1, sizeof(ls_http_resp));
    if (r) r->err = ls_strdup("二进制 POST 仅支持 HTTPS 路径");
    return r;
}

/*
 * asr.c - 语音识别模块(百度短语音识别 API)
 * 依赖: http.h(WinHTTP HTTPS) + json.h + storage.h
 */
#include "asr.h"
#include "http.h"
#include "json.h"
#include "storage.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- base64 编码 ---------------- */
static const char b64_tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char* in, size_t in_len, char* out)
{
    size_t i = 0, o = 0;
    while (i + 3 <= in_len) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i+1] << 8) | in[i+2];
        out[o++] = b64_tab[(v >> 18) & 63];
        out[o++] = b64_tab[(v >> 12) & 63];
        out[o++] = b64_tab[(v >> 6) & 63];
        out[o++] = b64_tab[v & 63];
        i += 3;
    }
    size_t rem = in_len - i;
    if (rem == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++] = b64_tab[(v >> 18) & 63];
        out[o++] = b64_tab[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i+1] << 8);
        out[o++] = b64_tab[(v >> 18) & 63];
        out[o++] = b64_tab[(v >> 12) & 63];
        out[o++] = b64_tab[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
}

/* 从 JSON 文本中提取字符串字段(简易实现, 返回静态缓存指针) */
static const char* json_field_str(const char* body, const char* key)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(body, pat);
    if (!k) return "";
    const char* colon = strchr(k + strlen(pat), ':');
    if (!colon) return "";
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    if (*colon == '"') {
        colon++;
        static char buf[1024];
        size_t n = 0;
        while (*colon && *colon != '"' && n + 1 < sizeof(buf)) buf[n++] = *colon++;
        buf[n] = '\0';
        return buf;
    }
    return "";
}

int ls_asr_load_config(const char* path, ls_asr_config* cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    ls_safe_copy(cfg->cuid, sizeof(cfg->cuid), "longshun-assistant");
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;
    ls_safe_copy(cfg->api_key, sizeof(cfg->api_key), ls_json_get_string(root, "api_key", ""));
    ls_safe_copy(cfg->secret_key, sizeof(cfg->secret_key), ls_json_get_string(root, "secret_key", ""));
    ls_safe_copy(cfg->cuid, sizeof(cfg->cuid), ls_json_get_string(root, "cuid", "longshun-assistant"));
    ls_json_free(root);
    return 0;
}

/* 获取百度 access_token */
static int get_access_token(const ls_asr_config* cfg, char* token, size_t token_n)
{
    if (!cfg->api_key[0] || !cfg->secret_key[0]) return -1;
    char url[1024];
    snprintf(url, sizeof(url),
        "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials"
        "&client_id=%s&client_secret=%s",
        cfg->api_key, cfg->secret_key);
    ls_http_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout_ms = 8000;
    ls_http_resp* r = ls_http_post(url, "", &opts);
    if (!r) return -1;
    int ok = 0;
    if (r->status_code == 200 && r->body) {
        const char* t = json_field_str(r->body, "access_token");
        if (t[0]) { ls_safe_copy(token, token_n, t); ok = 1; }
    }
    if (!ok) LOG_ERROR("获取语音识别 token 失败: http=%d body=%.200s",
                       r->status_code, r->body ? r->body : "");
    ls_http_resp_free(r);
    return ok ? 0 : -1;
}

char* ls_asr_recognize(const char* data_dir, const unsigned char* pcm, int pcm_len,
                       char* err, size_t err_n)
{
    if (err && err_n) err[0] = '\0';
    /* 加载配置 */
    ls_asr_config cfg;
    char cfgpath[640];
    snprintf(cfgpath, sizeof(cfgpath), "%s/asr_config.json", data_dir);
    if (ls_asr_load_config(cfgpath, &cfg) != 0) {
        if (err && err_n) snprintf(err, err_n, "未找到语音识别配置文件 data/asr_config.json");
        return NULL;
    }
    if (!cfg.api_key[0] || !cfg.secret_key[0]) {
        if (err && err_n) snprintf(err, err_n, "未配置语音识别密钥，请在 data/asr_config.json 填写百度智能云的 api_key 与 secret_key");
        return NULL;
    }
    if (!pcm || pcm_len <= 0) {
        if (err && err_n) snprintf(err, err_n, "音频数据为空");
        return NULL;
    }

    /* 获取 token */
    char token[512];
    if (get_access_token(&cfg, token, sizeof(token)) != 0) {
        if (err && err_n) snprintf(err, err_n, "获取百度语音识别 access_token 失败（请检查网络与密钥）");
        return NULL;
    }

    /* base64 编码 PCM */
    size_t b64_cap = (size_t)pcm_len * 4 / 3 + 8;
    char* b64 = (char*)malloc(b64_cap);
    if (!b64) { if (err && err_n) snprintf(err, err_n, "内存不足"); return NULL; }
    base64_encode(pcm, (size_t)pcm_len, b64);

    /* 构造请求 JSON */
    size_t body_cap = b64_cap + 512;
    char* body = (char*)malloc(body_cap);
    if (!body) { free(b64); if (err && err_n) snprintf(err, err_n, "内存不足"); return NULL; }
    snprintf(body, body_cap,
        "{\"format\":\"pcm\",\"rate\":16000,\"channel\":1,\"cuid\":\"%s\","
        "\"token\":\"%s\",\"speech\":\"%s\",\"len\":%d}",
        cfg.cuid, token, b64, pcm_len);
    free(b64);

    ls_http_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout_ms = 12000;
    ls_http_resp* r = ls_http_post("https://vop.baidu.com/server_api", body, &opts);
    free(body);
    if (!r) {
        if (err && err_n) snprintf(err, err_n, "语音识别网络请求失败");
        return NULL;
    }

    char* result = NULL;
    if (r->status_code == 200 && r->body) {
        /* 解析 result 数组的第一个字符串 */
        const char* rk = strstr(r->body, "\"result\"");
        const char* colon = rk ? strchr(rk, ':') : NULL;
        const char* arr = colon ? strchr(colon, '[') : NULL;
        const char* q = arr ? strchr(arr, '"') : NULL;
        if (q) {
            const char* end = strchr(q + 1, '"');
            if (end) {
                size_t n = (size_t)(end - (q + 1));
                result = (char*)malloc(n + 1);
                if (result) { memcpy(result, q + 1, n); result[n] = '\0'; }
            }
        }
        if (!result) {
            const char* err_no = json_field_str(r->body, "err_msg");
            if (err && err_n) snprintf(err, err_n, "语音识别失败: %s", err_no[0] ? err_no : r->body);
        }
    } else {
        if (err && err_n) snprintf(err, err_n, "语音识别 HTTP 错误 %d", r->status_code);
    }
    ls_http_resp_free(r);
    return result;
}

/* ---------------- base64 解码 ---------------- */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* 解码 base64, 输出到 out(需足够大), 返回解码字节数 */
static int base64_decode(const char* in, unsigned char* out)
{
    int o = 0, val = 0, bits = 0;
    for (; *in; in++) {
        if (*in == '=' || *in == '\n' || *in == '\r') break;
        int v = b64_val(*in);
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((val >> bits) & 0xFF);
        }
    }
    return o;
}

char* ls_asr_recognize_base64(const char* data_dir, const char* b64, char* err, size_t err_n)
{
    if (!b64 || !b64[0]) {
        if (err && err_n) snprintf(err, err_n, "音频数据为空");
        return NULL;
    }
    size_t in_len = strlen(b64);
    unsigned char* pcm = (unsigned char*)malloc(in_len / 2 + 8);
    if (!pcm) { if (err && err_n) snprintf(err, err_n, "内存不足"); return NULL; }
    int pcm_len = base64_decode(b64, pcm);
    if (pcm_len <= 0) {
        free(pcm);
        if (err && err_n) snprintf(err, err_n, "音频数据解码失败");
        return NULL;
    }
    char* text = ls_asr_recognize(data_dir, pcm, pcm_len, err, err_n);
    free(pcm);
    return text;
}

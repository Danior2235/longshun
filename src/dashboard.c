/*
 * dashboard.c - 实时数据看板后端服务(HTTP JSON 接口 + 多轮对话引导报价)
 * 对应需求: 模块二「实时数据看板」+ 语音助手多轮对话引导报价方案
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "log.h"
#include "json.h"
#include "storage.h"
#include "pricing.h"
#include "asr.h"
#include "dashboard.h"
#include "synonym.h"

/* socket 平台抽象(与 rpc.c 保持一致) */
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

/* 数据目录(由 ls_dashboard_start 设置) */
static const char* g_data_dir = "data";

/* ============================================================
 *  小工具
 * ============================================================ */

/* 拼接数据目录下的文件路径, 返回 malloc 字符串(调用方 free) */
static char* join_path(const char* dir, const char* name)
{
    size_t n = strlen(dir) + strlen(name) + 2;
    char* p = (char*)malloc(n);
    if (!p) return NULL;
    snprintf(p, n, "%s/%s", dir, name);
    return p;
}

/* 客户类型中文名 */
static const char* cust_cn(const char* t)
{
    if (t && strcmp(t, "study_travel") == 0) return "研学及旅行社";
    return "政府及企业团队";
}

/* 生成一张报价卡片(title 主标题, sub 副标题, price 价格) */
static ls_json* mk_card(const char* title, const char* sub, const char* price)
{
    ls_json* c = ls_json_object();
    ls_json_set(c, "title", ls_json_string(title));
    ls_json_set(c, "sub", ls_json_string(sub));
    ls_json_set(c, "price", ls_json_string(price));
    return c;
}

/* 把卡片数组挂到响应对象 */
static void set_cards(ls_json* j, ls_json* cards)
{
    ls_json_set(j, "cards", cards);
}

/* 把表格(列头 + 行)挂到响应对象 */
static void set_table(ls_json* j, const char** cols, int ncol, ls_json* rows)
{
    ls_json* c = ls_json_array();
    int i;
    for (i = 0; i < ncol; i++) ls_json_array_add(c, ls_json_string(cols[i]));
    ls_json_set(j, "columns", c);
    ls_json_set(j, "rows", rows);
}

/* 发送 JSON 响应 */
static void send_json(LS_SOCK sock, int code, const char* status, const char* body);

/* HTTP 请求分发(定义见文件后部, 由 ls_dashboard_start 调用) */
static void handle_request(LS_SOCK sock, const char* method, const char* path, const char* body);

static void send_json(LS_SOCK sock, int code, const char* status, const char* body)
{
    char header[512];
    int blen = (int)strlen(body);
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, status, blen);
    send(sock, header, n, 0);
    send(sock, body, blen, 0);
}

/* 发送任意 Content-Type 的响应(用于静态页面托管) */
static void send_raw(LS_SOCK sock, int code, const char* status, const char* ctype, const char* body)
{
    char header[512];
    int blen = (int)strlen(body);
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, status, ctype, blen);
    send(sock, header, n, 0);
    send(sock, body, blen, 0);
}

/* 发送二进制响应(音频等): 指定字节长度, 不经过 strlen */
static void send_binary(LS_SOCK sock, int code, const char* status, const char* ctype,
                        const char* data, size_t len)
{
    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, status, ctype, len);
    send(sock, header, n, 0);
    if (len) send(sock, data, (int)len, 0);
}

/* URL 解码(原地) */
static int hexv(char c){
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return 0;
}
static void url_decode(char* s){
    char* o=s; const char* p=s;
    while(*p){
        if(*p=='+'){*o++=' ';p++;}
        else if(*p=='%' && p[1] && p[2]){ *o++=(char)(hexv(p[1])*16+hexv(p[2])); p+=3; }
        else *o++=*p++;
    }
    *o='\0';
}

/* GET /api/tts - 文本转语音(后端调用 edge-tts, 返回 mp3 二进制) */
static void api_tts(LS_SOCK sock, const char* q)
{
    const char* t = q ? strstr(q, "text=") : NULL;
    if (!t) { send_json(sock, 400, "Bad Request", "{\"code\":1,\"msg\":\"缺少 text 参数\"}"); return; }
    t += 5;
    char text[2048];
    size_t i = 0;
    while (t[i] && t[i] != '&' && i < sizeof(text)-1) { text[i] = t[i]; i++; }
    text[i] = '\0';
    url_decode(text);
    if (text[0] == '\0') { send_json(sock, 400, "Bad Request", "{\"code\":1,\"msg\":\"text 为空\"}"); return; }
    if (strlen(text) > 300) text[300] = '\0';

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/ls_tts_%d_%ld.txt", (int)getpid(), (long)time(NULL));
    FILE* f = fopen(tmp, "wb");
    if (!f) { send_json(sock, 500, "Server Error", "{\"code\":1,\"msg\":\"临时文件创建失败\"}"); return; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "edge-tts --voice zh-CN-XiaoxiaoNeural --file \"%s\" --write-media - 2>/dev/null", tmp);
    FILE* pp = popen(cmd, "r");
    if (!pp) { remove(tmp); send_json(sock, 500, "Server Error", "{\"code\":1,\"msg\":\"edge-tts 调用失败\"}"); return; }

    size_t cap = 65536, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { pclose(pp); remove(tmp); send_json(sock, 500, "Server Error", "{\"code\":1,\"msg\":\"内存不足\"}"); return; }
    char chunk[8192];
    size_t r;
    while ((r = fread(chunk, 1, sizeof(chunk), pp)) > 0) {
        if (len + r + 1 > cap) {
            while (cap < len + r + 1) cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); pclose(pp); remove(tmp); send_json(sock, 500, "Server Error", "{\"code\":1,\"msg\":\"内存不足\"}"); return; }
            buf = nb;
        }
        memcpy(buf + len, chunk, r);
        len += r;
    }
    int rc = pclose(pp);
    (void)rc;
    remove(tmp);
    if (len == 0) {
        free(buf);
        send_json(sock, 502, "Bad Gateway", "{\"code\":1,\"msg\":\"edge-tts 未返回音频,可能未安装 edge-tts 或无外网\"}");
        return;
    }
    send_binary(sock, 200, "OK", "audio/mpeg", buf, len);
    free(buf);
}

/* 发送纯文本/JSON 失败响应 */
static ls_json* err_json(const char* msg)
{
    ls_json* j = ls_json_object();
    ls_json_set(j, "code", ls_json_number(1));
    ls_json_set(j, "msg", ls_json_string(msg));
    return j;
}

/* ============================================================
 *  看板数据 API(供 data/web/index.html 使用)
 * ============================================================ */

/* 加载 data 目录下 JSON 数组, 失败返回 NULL */
static ls_json* load_arr(const char* name)
{
    char* p = join_path(g_data_dir, name);
    ls_json* j = ls_storage_load(p);
    free(p);
    return j;
}

/* 数组元素数量(非数组返回 0) */
static int arr_count(ls_json* j)
{
    return (j && j->type == LS_JSON_ARRAY) ? j->count : 0;
}

/* GET /api/dashboard - 看板汇总统计 */
static ls_json* api_dashboard(void)
{
    ls_json* j = ls_json_object();
    ls_json* o = ls_json_object();
    ls_json* r = ls_json_object();

    /* 订单汇总(orders.json) */
    ls_json* orders = load_arr("orders.json");
    double total_amount = 0, total_cost = 0;
    int on = arr_count(orders), i;
    if (orders) {
        for (i = 0; i < orders->count; i++) {
            ls_json* e = ls_json_array_get(orders, i);
            total_amount += ls_json_get_number(e, "amount", 0);
            total_cost += ls_json_get_number(e, "cost", 0);
        }
    }
    ls_json_set(o, "count", ls_json_number(on));
    ls_json_set(o, "total_amount", ls_json_number(total_amount));
    ls_json_set(o, "total_cost", ls_json_number(total_cost));
    ls_json_set(o, "profit", ls_json_number(total_amount - total_cost));
    if (orders) ls_json_free(orders);

    /* 房态汇总(rooms.json) */
    ls_json* rooms = load_arr("rooms.json");
    int total = 0, occupied = 0, maint = 0;
    if (rooms) {
        for (i = 0; i < rooms->count; i++) {
            ls_json* e = ls_json_array_get(rooms, i);
            const char* st = ls_json_get_string(e, "status", "");
            total++;
            if (strcmp(st, "occupied") == 0) occupied++;
            else if (strcmp(st, "maintenance") == 0) maint++;
        }
    }
    ls_json_set(r, "total", ls_json_number(total));
    ls_json_set(r, "occupied", ls_json_number(occupied));
    ls_json_set(r, "maintenance", ls_json_number(maint));
    ls_json_set(r, "occupancy_rate", ls_json_number(total > 0 ? (double)(occupied + maint) * 100.0 / total : 0.0));
    if (rooms) ls_json_free(rooms);

    ls_json* leads = load_arr("leads.json");
    ls_json* rp = load_arr("room_pricing.json");
    ls_json* vp = load_arr("venue_pricing.json");

    ls_json_set(j, "orders", o);
    ls_json_set(j, "rooms", r);
    ls_json_set(j, "leads_count", ls_json_number(arr_count(leads)));
    ls_json_set(j, "room_pricing_count", ls_json_number(arr_count(rp)));
    ls_json_set(j, "venue_pricing_count", ls_json_number(arr_count(vp)));
    char ts[64]; ls_time_str(ts, sizeof(ts));
    ls_json_set(j, "generated_at", ls_json_string(ts));

    if (leads) ls_json_free(leads);
    if (rp) ls_json_free(rp);
    if (vp) ls_json_free(vp);
    return j;
}

/* GET /api/orders - 订单数组(按日期倒序) */
static ls_json* api_orders(void)
{
    ls_json* src = load_arr("orders.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "id", ls_json_string(ls_json_get_string(e, "id", "")));
            ls_json_set(o, "customer", ls_json_string(ls_json_get_string(e, "customer", "")));
            ls_json_set(o, "amount", ls_json_number(ls_json_get_number(e, "amount", 0)));
            ls_json_set(o, "status", ls_json_string(ls_json_get_string(e, "status", "pending")));
            ls_json_set(o, "date", ls_json_string(ls_json_get_string(e, "date", "")));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/rooms - 房态数组 */
static ls_json* api_rooms(void)
{
    ls_json* src = load_arr("rooms.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "no", ls_json_string(ls_json_get_string(e, "no", "")));
            ls_json_set(o, "type", ls_json_string(ls_json_get_string(e, "type", "")));
            ls_json_set(o, "status", ls_json_string(ls_json_get_string(e, "status", "")));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/rooms/pricing - 客房报价数组 */
static ls_json* api_rooms_pricing(void)
{
    char* p = join_path(g_data_dir, "room_pricing.json");
    ls_room_price* arr = NULL; int n = 0;
    ls_json* j = ls_json_array();
    if (ls_pricing_load_rooms(p, &arr, &n) == 0) {
        int i;
        for (i = 0; i < n; i++) ls_json_array_add(j, ls_pricing_room_to_json(&arr[i]));
        ls_pricing_free_rooms(arr);
    }
    free(p);
    return j;
}

/* GET /api/venues/pricing - 会场报价数组 */
static ls_json* api_venues_pricing(void)
{
    char* p = join_path(g_data_dir, "venue_pricing.json");
    ls_venue_price* arr = NULL; int n = 0;
    ls_json* j = ls_json_array();
    if (ls_pricing_load_venues(p, &arr, &n) == 0) {
        int i;
        for (i = 0; i < n; i++) ls_json_array_add(j, ls_pricing_venue_to_json(&arr[i]));
        ls_pricing_free_venues(arr);
    }
    free(p);
    return j;
}

/* GET /api/bookings - 预订清单数组 */
static ls_json* api_bookings(void)
{
    ls_json* src = load_arr("bookings.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "id", ls_json_string(ls_json_get_string(e, "id", "")));
            ls_json_set(o, "customer", ls_json_string(ls_json_get_string(e, "customer", "")));
            ls_json_set(o, "type", ls_json_string(ls_json_get_string(e, "type", "room")));
            ls_json_set(o, "room_type", ls_json_string(ls_json_get_string(e, "venue", ls_json_get_string(e, "room_type", ""))));
            ls_json_set(o, "amount", ls_json_number(ls_json_get_number(e, "amount", 0)));
            ls_json_set(o, "status", ls_json_string(ls_json_get_string(e, "status", "pending")));
            ls_json_set(o, "created_at", ls_json_string(ls_json_get_string(e, "created_at", "")));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/pay/orders - 支付单数组 */
static ls_json* api_pay_orders(void)
{
    ls_json* src = load_arr("payments.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "id", ls_json_string(ls_json_get_string(e, "id", "")));
            ls_json_set(o, "order_id", ls_json_string(ls_json_get_string(e, "order_id", "")));
            ls_json_set(o, "subject", ls_json_string(ls_json_get_string(e, "subject", "")));
            ls_json_set(o, "amount", ls_json_number(ls_json_get_number(e, "amount", 0)));
            ls_json_set(o, "status", ls_json_string(ls_json_get_string(e, "status", "pending")));
            ls_json_set(o, "created_at", ls_json_string(ls_json_get_string(e, "created_at", "")));
            ls_json_set(o, "paid_at", ls_json_string(ls_json_get_string(e, "paid_at", "")));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/leads - 客户线索数组 */
static ls_json* api_leads(void)
{
    ls_json* src = load_arr("leads.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "company", ls_json_string(ls_json_get_string(e, "company", "")));
            ls_json_set(o, "contact", ls_json_string(ls_json_get_string(e, "contact", "")));
            ls_json_set(o, "phone", ls_json_string(ls_json_get_string(e, "phone", "")));
            ls_json_set(o, "biz_type", ls_json_string(ls_json_get_string(e, "biz_type", "other")));
            ls_json_set(o, "source", ls_json_string(ls_json_get_string(e, "source", "")));
            ls_json_set(o, "suggest", ls_json_string(ls_json_get_string(e, "suggest", "")));
            ls_json_set(o, "industry", ls_json_string(ls_json_get_string(e, "industry", "")));
            ls_json_set(o, "priority", ls_json_string(ls_json_get_string(e, "priority", "")));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/tasks - 协同任务数组(无文件时返回空) */
static ls_json* api_tasks(void)
{
    ls_json* src = load_arr("tasks.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            const char* title = ls_json_get_string(e, "title", "");
            if (!title[0]) title = ls_json_get_string(e, "name", "");
            ls_json_set(o, "title", ls_json_string(title));
            ls_json_set(o, "module", ls_json_string(ls_json_get_string(e, "module", "")));
            ls_json_set(o, "assignee", ls_json_string(ls_json_get_string(e, "assignee", "")));
            ls_json_set(o, "priority", ls_json_string(ls_json_get_string(e, "priority", "")));
            ls_json_set(o, "status", ls_json_string(ls_json_get_string(e, "status", "")));
            const char* due = ls_json_get_string(e, "due", "");
            if (!due[0]) due = ls_json_get_string(e, "deadline", "");
            ls_json_set(o, "due", ls_json_string(due));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* GET /api/sync - 维修工单 + 库存物资 */
static ls_json* api_sync(void)
{
    ls_json* j = ls_json_object();
    /* 维修工单 */
    ls_json* mt = load_arr("room_maintenance.json");
    ls_json* mtarr = ls_json_array();
    if (mt) {
        int i;
        for (i = 0; i < mt->count; i++) {
            ls_json* e = ls_json_array_get(mt, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "room_no", ls_json_string(ls_json_get_string(e, "room_no", "")));
            ls_json_set(o, "fault", ls_json_string(ls_json_get_string(e, "fault", "")));
            ls_json_set(o, "submit_date", ls_json_string(ls_json_get_string(e, "submit_date", "")));
            ls_json_array_add(mtarr, o);
        }
        ls_json_free(mt);
    }
    ls_json_set(j, "maintenance", mtarr);
    /* 库存物资 */
    ls_json* inv = load_arr("inventory.json");
    ls_json* invarr = ls_json_array();
    if (inv) {
        int i;
        for (i = 0; i < inv->count; i++) {
            ls_json* e = ls_json_array_get(inv, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "name", ls_json_string(ls_json_get_string(e, "name", "")));
            ls_json_set(o, "qty", ls_json_number(ls_json_get_number(e, "qty", 0)));
            ls_json_set(o, "unit", ls_json_string(ls_json_get_string(e, "unit", "")));
            ls_json_set(o, "updated_at", ls_json_string(ls_json_get_string(e, "updated_at", "")));
            ls_json_array_add(invarr, o);
        }
        ls_json_free(inv);
    }
    ls_json_set(j, "inventory", invarr);
    return j;
}

/* GET /api/invoice - 开票主体数组 */
static ls_json* api_invoice(void)
{
    ls_json* src = load_arr("company_licenses.json");
    ls_json* j = ls_json_array();
    if (src) {
        int i;
        for (i = 0; i < src->count; i++) {
            ls_json* e = ls_json_array_get(src, i);
            ls_json* o = ls_json_object();
            ls_json_set(o, "company", ls_json_string(ls_json_get_string(e, "company", "")));
            ls_json_set(o, "taxpayer_type", ls_json_string(ls_json_get_string(e, "taxpayer_type", "")));
            ls_json_set(o, "industry", ls_json_string(ls_json_get_string(e, "industry", "")));
            ls_json_set(o, "rate", ls_json_number(ls_json_get_number(e, "rate", 0)));
            ls_json_set(o, "annual_limit", ls_json_number(ls_json_get_number(e, "annual_limit", 0)));
            ls_json_set(o, "enabled", ls_json_bool(ls_json_get_bool(e, "enabled", 1)));
            ls_json_array_add(j, o);
        }
        ls_json_free(src);
    }
    return j;
}

/* 保存数组到 data 目录 JSON 文件(供写操作 API 复用) */
static int save_arr(const char* name, ls_json* j)
{
    char* p = join_path(g_data_dir, name);
    int rc = ls_storage_save(p, j);
    free(p);
    return rc;
}

/* POST /api/asr - 语音识别(接收 base64 编码的 PCM, 返回识别文字) */
static ls_json* api_asr(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req) return err_json("请求体无效");
    const char* b64 = ls_json_get_string(req, "audio_base64", "");
    ls_json_free(req);
    if (!b64[0]) return err_json("缺少 audio_base64 音频数据");

    char err[512];
    char* text = ls_asr_recognize_base64(g_data_dir, b64, err, sizeof(err));
    if (text) {
        ls_json_set(j, "code", ls_json_number(0));
        ls_json_set(j, "text", ls_json_string(text));
        free(text);
    } else {
        ls_json_set(j, "code", ls_json_number(1));
        ls_json_set(j, "msg", ls_json_string(err[0] ? err : "语音识别失败"));
    }
    return j;
}

/* 生成当前时间字符串 */
static const char* now_str(void)
{
    static char buf[32];
    ls_time_str(buf, sizeof(buf));
    return buf;
}

/* 在 JSON 数组中新增一个对象并落盘 */
static int append_obj(const char* name, ls_json* item)
{
    ls_json* root = load_arr(name);
    if (!root) root = ls_json_array();
    ls_json_array_add(root, item);
    int rc = save_arr(name, root);
    ls_json_free(root);
    return rc;
}

/* POST /api/booking - 新建预订 */
static ls_json* api_booking(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req) return err_json("请求体无效");
    const char* customer = ls_json_get_string(req, "customer", "");
    if (!customer[0]) { ls_json_free(req); return err_json("请填写客户姓名"); }

    ls_json* o = ls_json_object();
    char id[40]; ls_gen_id(id, sizeof(id));
    ls_json_set(o, "id", ls_json_string(id));
    ls_json_set(o, "customer", ls_json_string(customer));
    ls_json_set(o, "phone", ls_json_string(ls_json_get_string(req, "phone", "")));
    ls_json_set(o, "type", ls_json_string(ls_json_get_string(req, "type", "room")));
    ls_json_set(o, "status", ls_json_string("pending"));
    ls_json_set(o, "created_at", ls_json_string(now_str()));
    ls_json_set(o, "amount", ls_json_number(ls_json_get_number(req, "amount", 0)));
    ls_json_set(o, "date", ls_json_string(ls_json_get_string(req, "date", ls_json_get_string(req, "check_in", ""))));
    ls_json_set(o, "room_type", ls_json_string(ls_json_get_string(req, "room_type", "")));
    ls_json_set(o, "venue", ls_json_string(ls_json_get_string(req, "venue", "")));
    ls_json_set(o, "remark", ls_json_string(ls_json_get_string(req, "remark", "")));

    append_obj("bookings.json", o);

    ls_json* order = ls_json_object();
    ls_json_set(order, "id", ls_json_string(id));
    ls_json_set(order, "amount", ls_json_number(ls_json_get_number(req, "amount", 0)));
    ls_json_set(j, "code", ls_json_number(0));
    ls_json_set(j, "order", order);
    ls_json_free(req);
    return j;
}

/* POST /api/booking/batch - 批量组合预订 */
static ls_json* api_booking_batch(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req) return err_json("请求体无效");
    const char* customer = ls_json_get_string(req, "customer", "");
    if (!customer[0]) { ls_json_free(req); return err_json("请填写客户姓名"); }

    ls_json* items = ls_json_get(req, "items");
    int cnt = (items && items->type == LS_JSON_ARRAY) ? items->count : 0;
    int i;
    for (i = 0; i < cnt; i++) {
        ls_json* it = ls_json_array_get(items, i);
        if (!it) continue;
        ls_json* o = ls_json_object();
        char id[40]; ls_gen_id(id, sizeof(id));
        ls_json_set(o, "id", ls_json_string(id));
        ls_json_set(o, "customer", ls_json_string(customer));
        ls_json_set(o, "phone", ls_json_string(ls_json_get_string(req, "phone", "")));
        ls_json_set(o, "type", ls_json_string(ls_json_get_string(it, "type", "room")));
        ls_json_set(o, "status", ls_json_string("pending"));
        ls_json_set(o, "created_at", ls_json_string(now_str()));
        ls_json_set(o, "amount", ls_json_number(ls_json_get_number(it, "amount", 0)));
        ls_json_set(o, "date", ls_json_string(ls_json_get_string(req, "date", "")));
        ls_json_set(o, "room_type", ls_json_string(ls_json_get_string(it, "room_type", "")));
        ls_json_set(o, "building", ls_json_string(ls_json_get_string(it, "building", "")));
        ls_json_set(o, "category", ls_json_string(ls_json_get_string(it, "category", "")));
        ls_json_set(o, "cust_type", ls_json_string(ls_json_get_string(it, "cust_type", "")));
        ls_json_set(o, "venue", ls_json_string(ls_json_get_string(it, "venue", "")));
        ls_json_set(o, "session", ls_json_string(ls_json_get_string(it, "session", "")));
        ls_json_set(o, "nights", ls_json_number(ls_json_get_number(it, "nights", 0)));
        ls_json_set(o, "rooms", ls_json_number(ls_json_get_number(it, "rooms", 0)));
        ls_json_set(o, "check_in", ls_json_string(ls_json_get_string(it, "check_in", "")));
        ls_json_set(o, "check_out", ls_json_string(ls_json_get_string(it, "check_out", "")));
        ls_json_set(o, "remark", ls_json_string(ls_json_get_string(req, "remark", "")));
        append_obj("bookings.json", o);
    }
    ls_json_set(j, "code", ls_json_number(0));
    ls_json_set(j, "count", ls_json_number(cnt));
    ls_json_free(req);
    return j;
}

/* POST /api/pay/create - 创建支付单 */
static ls_json* api_pay_create(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req) return err_json("请求体无效");
    const char* order_id = ls_json_get_string(req, "order_id", "");
    if (!order_id[0]) { ls_json_free(req); return err_json("缺少订单号"); }

    ls_json* p = ls_json_object();
    char id[40]; ls_gen_id(id, sizeof(id));
    ls_json_set(p, "id", ls_json_string(id));
    ls_json_set(p, "order_id", ls_json_string(order_id));
    ls_json_set(p, "subject", ls_json_string(ls_json_get_string(req, "subject", "龙顺庄园预订")));
    ls_json_set(p, "amount", ls_json_number(ls_json_get_number(req, "amount", 0)));
    ls_json_set(p, "status", ls_json_string("pending"));
    ls_json_set(p, "created_at", ls_json_string(now_str()));

    append_obj("payments.json", p);

    ls_json* payment = ls_json_object();
    ls_json_set(payment, "payment_id", ls_json_string(id));
    ls_json_set(payment, "order_id", ls_json_string(order_id));
    ls_json_set(payment, "amount", ls_json_number(ls_json_get_number(req, "amount", 0)));
    ls_json_set(payment, "subject", ls_json_string(ls_json_get_string(req, "subject", "")));
    ls_json_set(payment, "status", ls_json_string("pending"));
    ls_json_set(j, "code", ls_json_number(0));
    ls_json_set(j, "payment", payment);
    ls_json_free(req);
    return j;
}

/* POST /api/pay/mock - 模拟支付成功(联动预订与订单状态) */
static ls_json* api_pay_mock(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req) return err_json("请求体无效");
    const char* order_id = ls_json_get_string(req, "order_id", "");
    if (!order_id[0]) { ls_json_free(req); return err_json("缺少订单号"); }

    /* 支付单置为已支付 */
    ls_json* pays = load_arr("payments.json");
    int found = 0, i;
    if (pays) {
        for (i = 0; i < pays->count && !found; i++) {
            ls_json* e = ls_json_array_get(pays, i);
            if (strcmp(ls_json_get_string(e, "order_id", ""), order_id) == 0) {
                ls_json_set(e, "status", ls_json_string("paid"));
                ls_json_set(e, "paid_at", ls_json_string(now_str()));
                found = 1;
            }
        }
        if (found) save_arr("payments.json", pays);
        ls_json_free(pays);
    }
    /* 预订状态置为已支付 */
    ls_json* bks = load_arr("bookings.json");
    if (bks) {
        for (i = 0; i < bks->count; i++) {
            ls_json* e = ls_json_array_get(bks, i);
            if (strcmp(ls_json_get_string(e, "id", ""), order_id) == 0) {
                ls_json_set(e, "status", ls_json_string("paid"));
                save_arr("bookings.json", bks);
                break;
            }
        }
        ls_json_free(bks);
    }
    ls_json_set(j, "code", ls_json_number(0));
    ls_json_set(j, "msg", ls_json_string("支付成功"));
    ls_json_free(req);
    return j;
}

/* ============================================================
 *  参数解析辅助(供 /api/conv 使用)
 * ============================================================ */

/* 中文日期 -> YYYY-MM-DD; 支持: 今天/明天/后天/X月X日/X月X号/X日/X号 */
static int parse_date_cn(const char* q, char* out, size_t n)
{
    time_t now = time(NULL);
    struct tm tm;
#if LS_WINDOWS
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    int add = 0;
    int rel = 0; /* 是否匹配到相对日期关键词(今天/明天/后天) */
    if (syn_has(q, "date", "tomorrow")) { add = 1; rel = 1; }
    else if (syn_has(q, "date", "day_after")) { add = 2; rel = 1; }
    else if (syn_has(q, "date", "today")) { add = 0; rel = 1; }
    int mon = -1, day = -1;
    /* X月X日 / X月X号 */
    const char* p = q;
    while (*p) {
        if ((*p >= '0' && *p <= '9')) {
            int a = 0;
            while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
            if (strchr("月", *(p))) { mon = a; }
            else if (strchr("日号", *(p))) { day = a; }
            else if (day < 0 && mon >= 0) { day = a; }
            else if (mon < 0 && day < 0) { day = a; }
            else { /* 仅数字串: 当作 月日 连写概率低, 忽略 */ }
            p++;
            continue;
        }
        p++;
    }
    if (mon > 0 && day > 0) {
        tm.tm_mon = mon - 1; tm.tm_mday = day;
    } else {
        if (!rel && mon < 0 && day < 0) return 0; /* 无日期信息 */
        tm.tm_mday += add;
    }
    mktime(&tm);
    snprintf(out, n, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return 1;
}

/* 客户类型识别 */
static int pick_cust_type_cn(const char* q, char* out, size_t n)
{
    /* 走同义词表: 研学/旅行/学生团 -> study_travel; 政府/企业/公司/公务 -> government_enterprise */
    return syn_pick(q, "cust_type", out, n);
}

/* ============================================================
 *  报价计算核心(被 /api/quote 与 /api/conv 共用)
 * ============================================================ */

static ls_json* calc_quote(ls_json* req)
{
    ls_json* j = ls_json_object();
    const char* type = ls_json_get_string(req, "type", "");
    if (strcmp(type, "room") == 0) {
        const char* building = ls_json_get_string(req, "building", "");
        const char* room_type = ls_json_get_string(req, "room_type", "");
        const char* category = ls_json_get_string(req, "category", "");
        const char* cust = ls_json_get_string(req, "cust_type", "");
        int nights = (int)ls_json_get_number(req, "nights", 1);
        int rooms = (int)ls_json_get_number(req, "rooms", 1);
        ls_customer_type ct = strcmp(cust, "study_travel") == 0 ? LS_CUST_STUDY_TRAVEL : LS_CUST_GOV_ENTERPRISE;
        char* path = join_path(g_data_dir, "room_pricing.json");
        ls_room_price* arr = NULL; int n = 0;
        if (ls_pricing_load_rooms(path, &arr, &n) != 0) {
            free(path);
            ls_json_set(j, "code", ls_json_number(1));
            ls_json_set(j, "msg", ls_json_string("加载客房定价失败"));
            return j;
        }
        ls_room_price out[8];
        int m = ls_pricing_query_rooms(arr, n, building[0] ? building : NULL,
            room_type[0] ? room_type : NULL, category[0] ? category : NULL,
            ct, out, 8);
        ls_pricing_free_rooms(arr);
        free(path);
        if (m <= 0) {
            ls_json_set(j, "code", ls_json_number(1));
            ls_json_set(j, "msg", ls_json_string("未找到匹配房型"));
            return j;
        }
        double total = 0;
        ls_json* detail = ls_json_array();
        int i;
        for (i = 0; i < m; i++) {
            double fee = ls_pricing_calc_room(&out[i], nights, rooms);
            total += fee;
            ls_json* it = ls_json_object();
            char label[96];
            snprintf(label, sizeof(label), "%s · %s（%s）", out[i].building, out[i].room_type, out[i].category);
            ls_json_set(it, "label", ls_json_string(label));
            ls_json_set(it, "amount", ls_json_number(fee));
            ls_json_array_add(detail, it);
        }
        ls_json_set(j, "code", ls_json_number(0));
        ls_json_set(j, "total", ls_json_number(total));
        ls_json_set(j, "detail", detail);
        return j;
    } else if (strcmp(type, "venue") == 0) {
        int vid = (int)ls_json_get_number(req, "venue_id", 0);
        int is_full_day = (int)ls_json_get_number(req, "is_full_day", 0);
        int hours = (int)ls_json_get_number(req, "hours", 0);
        int is_evening = (int)ls_json_get_number(req, "is_evening", 0);
        int need_lighting = (int)ls_json_get_number(req, "need_lighting", 0);
        int need_screen = (int)ls_json_get_number(req, "need_screen", 0);
        double catering = ls_json_get_number(req, "catering_amount", 0);
        char* path = join_path(g_data_dir, "venue_pricing.json");
        ls_venue_price* arr = NULL; int n = 0;
        if (ls_pricing_load_venues(path, &arr, &n) != 0) {
            free(path);
            ls_json_set(j, "code", ls_json_number(1));
            ls_json_set(j, "msg", ls_json_string("加载会场定价失败"));
            return j;
        }
        ls_venue_price qv[4];
        int m = ls_pricing_query_venues(arr, n, NULL, NULL, 0, qv, 4);
        ls_pricing_free_venues(arr);
        free(path);
        /* 按 id 查找 */
        const ls_venue_price* v = NULL;
        int i;
        for (i = 0; i < m; i++) {
            if (vid > 0) {
                if (qv[i].id == vid) { v = &qv[i]; break; }
            } else {
                /* 无 id 时取第一个 */
                v = &qv[i]; break;
            }
        }
        if (!v) {
            ls_json_set(j, "code", ls_json_number(1));
            ls_json_set(j, "msg", ls_json_string("未找到对应会场"));
            return j;
        }
        char* trp = join_path(g_data_dir, "time_rules.json");
        ls_time_rules tr;
        memset(&tr, 0, sizeof(tr));
        ls_pricing_load_time_rules(trp, &tr);
        free(trp);
        ls_json* detail = ls_json_array();
        double total = ls_pricing_calc_venue(v, is_full_day, hours, is_evening,
            need_lighting, need_screen, catering, &tr);
        ls_json* it = ls_json_object();
        char label[96];
        snprintf(label, sizeof(label), "%s（%s）", v->name, v->area);
        ls_json_set(it, "label", ls_json_string(label));
        ls_json_set(it, "amount", ls_json_number(total));
        ls_json_array_add(detail, it);
        ls_json_set(j, "code", ls_json_number(0));
        ls_json_set(j, "total", ls_json_number(total));
        ls_json_set(j, "detail", detail);
        return j;
    }
    ls_json_set(j, "code", ls_json_number(1));
    ls_json_set(j, "msg", ls_json_string("未知报价类型"));
    return j;
}

/* ============================================================
 *  /api/query 自然语言查询(看板指标/表格)
 * ============================================================ */

static ls_json* handle_query(const char* q)
{
    ls_json* j = ls_json_object();
    ls_json_set(j, "code", ls_json_number(0));
    /* 待支付订单 */
    if (strstr(q, "待支付") || strstr(q, "支付") || strstr(q, "收款")) {
        char* p = join_path(g_data_dir, "bookings.json");
        ls_json* arr = ls_storage_load(p);
        free(p);
        ls_json* rows = ls_json_array();
        int pays = 0;
        if (arr && arr->type == LS_JSON_ARRAY) {
            int i;
            for (i = 0; i < arr->count; i++) {
                ls_json* o = ls_json_array_get(arr, i);
                const char* status = ls_json_get_string(o, "status", "");
                if (strcmp(status, "paid") == 0) continue;
                const char* id = ls_json_get_string(o, "id", "");
                double amt = ls_json_get_number(o, "amount", 0);
                const char* t = ls_json_get_string(o, "created_at", "");
                ls_json* row = ls_json_array();
                ls_json_array_add(row, ls_json_string(id));
                ls_json_array_add(row, ls_json_string(amt >= 0 ? "" : ""));
                /* amount */
                char buf[32]; snprintf(buf, sizeof(buf), "%.0f", amt);
                ls_json_array_add(row, ls_json_string(buf));
                ls_json_array_add(row, ls_json_string(status));
                ls_json_array_add(row, ls_json_string(t));
                ls_json_array_add(rows, row);
                pays++;
            }
        }
        if (arr) ls_json_free(arr);
        const char* cols[] = { "订单号", "金额", "状态", "时间" };
        set_table(j, cols, 4, rows);
        ls_json_set(j, "table_key", ls_json_string("pay"));
        ls_json_free(rows);
        char ans[128];
        snprintf(ans, sizeof(ans), "当前有 %d 笔待支付订单。", pays);
        ls_json_set(j, "answer", ls_json_string(ans));
        ls_json_set(j, "goto", ls_json_string("finance"));
        return j;
    }
    /* 房态 */
    if (strstr(q, "房态") || strstr(q, "空房") || strstr(q, "入住率")) {
        char* p = join_path(g_data_dir, "rooms.json");
        ls_json* arr = ls_storage_load(p);
        free(p);
        int total = 0, occupied = 0;
        if (arr && arr->type == LS_JSON_ARRAY) {
            int i;
            for (i = 0; i < arr->count; i++) {
                ls_json* o = ls_json_array_get(arr, i);
                total++;
                if (strcmp(ls_json_get_string(o, "status", ""), "occupied") == 0) occupied++;
            }
        }
        if (arr) ls_json_free(arr);
        char ans[128];
        snprintf(ans, sizeof(ans), "当前客房 %d 间，入住 %d 间，空置 %d 间。", total, occupied, total - occupied);
        ls_json_set(j, "answer", ls_json_string(ans));
        ls_json_set(j, "goto", ls_json_string("rooms"));
        return j;
    }
    /* 订单数 */
    {
        char* p = join_path(g_data_dir, "bookings.json");
        ls_json* arr = ls_storage_load(p);
        free(p);
        int n = (arr && arr->type == LS_JSON_ARRAY) ? arr->count : 0;
        if (arr) ls_json_free(arr);
        char ans[128];
        snprintf(ans, sizeof(ans), "当前共有 %d 笔订单。", n);
        ls_json_set(j, "answer", ls_json_string(ans));
        ls_json_set(j, "goto", ls_json_string("dashboard"));
        return j;
    }
}

/* ============================================================
 *  /api/conv 多轮对话参数引导(增量式: 每轮同步返回当前方案)
 * ============================================================ */

static int build_partial_room(ls_json* j, ls_json* v, const char* date)
{
    /* 依据已收集参数生成当前可用的房型方案, 直接写入 j */
    const char* cust = ls_json_get_string(v, "cust_type", "");
    const char* rt = ls_json_get_string(v, "room_type", "");
    const char* cat = ls_json_get_string(v, "category", "");
    int nights = (int)ls_json_get_number(v, "nights", 0);
    int rooms = (int)ls_json_get_number(v, "rooms", 0);
    if (!cust[0] || !rt[0] || !cat[0] || nights <= 0 || rooms <= 0) return 0;
    char* p = join_path(g_data_dir, "room_pricing.json");
    ls_room_price* arr = NULL; int n = 0;
    if (ls_pricing_load_rooms(p, &arr, &n) != 0) { free(p); return 0; }
    ls_customer_type ct = strcmp(cust, "study_travel") == 0 ? LS_CUST_STUDY_TRAVEL : LS_CUST_GOV_ENTERPRISE;
    ls_room_price out[8];
    int m = ls_pricing_query_rooms(arr, n, NULL, rt[0] ? rt : NULL, cat[0] ? cat : NULL, ct, out, 8);
    ls_pricing_free_rooms(arr);
    free(p);
    if (m <= 0) return 0;
    ls_json* items = ls_json_array();
    int i;
    for (i = 0; i < m; i++) {
        double fee = ls_pricing_calc_room(&out[i], nights, rooms);
        char title[96], price[64];
        snprintf(title, sizeof(title), "%s · %s（%s）", out[i].building, out[i].room_type, out[i].category);
        snprintf(price, sizeof(price), "¥%.0f / %d晚%d间", fee, nights, rooms);
        ls_json_array_add(items, mk_card(title, date, price));
    }
    set_cards(j, items);
    return 1;
}

static int build_partial_venue(ls_json* j, ls_json* v, const char* date)
{
    int vid = (int)ls_json_get_number(v, "venue_id", 0);
    int is_full_day = (int)ls_json_get_number(v, "is_full_day", -1);
    int hours = (int)ls_json_get_number(v, "hours", 0);
    if (vid <= 0 || (is_full_day < 0 && hours <= 0)) return 0;
    ls_json* q2 = ls_json_object();
    ls_json_set(q2, "type", ls_json_string("venue"));
    ls_json_set(q2, "venue_id", ls_json_number(vid));
    ls_json_set(q2, "is_full_day", ls_json_number(is_full_day < 0 ? 0 : is_full_day));
    ls_json_set(q2, "hours", ls_json_number(hours));
    ls_json_set(q2, "is_evening", ls_json_number(0));
    ls_json_set(q2, "need_lighting", ls_json_number((int)ls_json_get_number(v, "need_lighting", 0) > 0 ? 1 : 0));
    ls_json_set(q2, "need_screen", ls_json_number((int)ls_json_get_number(v, "need_screen", 0) > 0 ? 1 : 0));
    ls_json_set(q2, "catering_amount", ls_json_number(ls_json_get_number(v, "catering_amount", -1) < 0 ? 0 : ls_json_get_number(v, "catering_amount", 0)));
    ls_json_set(q2, "rehearsal", ls_json_number((int)ls_json_get_number(v, "need_rehearsal", -1) > 0 ? 1 : 0));
    ls_json* r = calc_quote(q2);
    ls_json_free(q2);
    if (ls_json_get_number(r, "code", 1) != 0) { ls_json_free(r); return 0; }
    double total = ls_json_get_number(r, "total", 0);
    ls_json* detail = ls_json_get(r, "detail");
    char dur[32];
    if (hours > 0) snprintf(dur, sizeof(dur), "%d 小时", hours);
    else if (is_full_day > 0) snprintf(dur, sizeof(dur), "全天");
    else snprintf(dur, sizeof(dur), "半天");
    ls_json* items = ls_json_array();
    if (detail && detail->type == LS_JSON_ARRAY) {
        int i;
        for (i = 0; i < detail->count; i++) {
            ls_json* it = ls_json_array_get(detail, i);
            char title[96], sub[128], price[64];
            ls_safe_copy(title, sizeof(title), ls_json_get_string(it, "label", ""));
            double amt = ls_json_get_number(it, "amount", total);
            int people = (int)ls_json_get_number(v, "people", 0);
            const char* layout = ls_json_get_string(v, "layout", "");
            const char* lc = "未知";
            if (strcmp(layout, "theater") == 0) lc = "剧院式";
            else if (strcmp(layout, "classroom") == 0) lc = "课桌式";
            else if (strcmp(layout, "island") == 0) lc = "岛屿式";
            else if (strcmp(layout, "round_table") == 0) lc = "圆桌式";
            const char* equip = ls_json_get_string(v, "need_equip", "");
            if (equip[0])
                snprintf(sub, sizeof(sub), "%s · %s · %s · %d人 · 设备:%s", date, dur, lc, people, equip);
            else
                snprintf(sub, sizeof(sub), "%s · %s · %s · %d人", date, dur, lc, people);
            snprintf(price, sizeof(price), "¥%.0f", amt);
            ls_json_array_add(items, mk_card(title, sub, price));
        }
    }
    set_cards(j, items);
    ls_json_free(r);
    return 1;
}

static ls_json* handle_conv(const char* body)
{
    ls_json* j = ls_json_object();
    ls_json* req = body ? ls_json_parse(body) : NULL;
    if (!req || req->type != LS_JSON_OBJECT) {
        if (req) ls_json_free(req);
        ls_json_set(j, "code", ls_json_number(1));
        ls_json_set(j, "msg", ls_json_string("请求体应为 JSON 对象"));
        return j;
    }
    const char* q = ls_json_get_string(req, "q", "");
    char intent[16];
    ls_safe_copy(intent, sizeof(intent), ls_json_get_string(req, "intent", ""));
    if (!intent[0]) {
        if (syn_has(q, "intent", "venue")) {
            ls_safe_copy(intent, sizeof(intent), "venue");
        } else if (syn_has(q, "intent", "room")) {
            ls_safe_copy(intent, sizeof(intent), "room");
        } else {
            ls_json_set(j, "code", ls_json_number(0));
            ls_json_set(j, "conv", ls_json_number(0));
            ls_json_free(req);
            return j;
        }
    }

    /* 合并会话参数(服务器侧暂存 + 前端回传) */
    ls_json* v = ls_json_object();
    const char* date = ls_json_get_string(req, "date", "");
    const char* cust_type = ls_json_get_string(req, "cust_type", "");
    const char* building = ls_json_get_string(req, "building", "");
    const char* room_type = ls_json_get_string(req, "room_type", "");
    const char* category = ls_json_get_string(req, "category", "");
    const char* layout = ls_json_get_string(req, "layout", "");
    const char* need_equip = ls_json_get_string(req, "need_equip", "");
    ls_json_set(v, "intent", ls_json_string(intent));
    ls_json_set(v, "date", ls_json_string(date));
    ls_json_set(v, "cust_type", ls_json_string(cust_type));
    ls_json_set(v, "building", ls_json_string(building));
    ls_json_set(v, "room_type", ls_json_string(room_type));
    ls_json_set(v, "category", ls_json_string(category));
    ls_json_set(v, "layout", ls_json_string(layout));
    ls_json_set(v, "need_equip", ls_json_string(need_equip));
    /* 归一化前端回传的展示值 -> 内部值(布局: 剧院式->theater 等; 早餐: 含早（自助早餐）->含早) */
    if (strcmp(intent, "venue") == 0) {
        if (strstr(layout, "剧院")) ls_json_set(v, "layout", ls_json_string("theater"));
        else if (strstr(layout, "课桌")) ls_json_set(v, "layout", ls_json_string("classroom"));
        else if (strstr(layout, "岛屿")) ls_json_set(v, "layout", ls_json_string("island"));
        else if (strstr(layout, "圆桌")) ls_json_set(v, "layout", ls_json_string("round_table"));
    }
    if (strcmp(intent, "room") == 0) {
        if (strstr(category, "含早")) ls_json_set(v, "category", ls_json_string("含早"));
        else if (strstr(category, "净房") || strstr(category, "不含早")) ls_json_set(v, "category", ls_json_string("净房"));
    }
    ls_json_set(v, "nights", ls_json_number((int)ls_json_get_number(req, "nights", 0)));
    ls_json_set(v, "rooms", ls_json_number((int)ls_json_get_number(req, "rooms", 0)));
    ls_json_set(v, "venue_id", ls_json_number((int)ls_json_get_number(req, "venue_id", 0)));
    ls_json_set(v, "people", ls_json_number((int)ls_json_get_number(req, "people", 0)));
    ls_json_set(v, "is_full_day", ls_json_number((int)ls_json_get_number(req, "is_full_day", -1)));
    ls_json_set(v, "hours", ls_json_number((int)ls_json_get_number(req, "hours", 0)));
    ls_json_set(v, "need_rehearsal", ls_json_number((int)ls_json_get_number(req, "need_rehearsal", -1)));
    ls_json_set(v, "need_lighting", ls_json_number((int)ls_json_get_number(req, "need_lighting", -1)));
    ls_json_set(v, "need_screen", ls_json_number((int)ls_json_get_number(req, "need_screen", -1)));
    ls_json_set(v, "catering_amount", ls_json_number(ls_json_get_number(req, "catering_amount", -1)));

    /* 从本轮回答提取参数 */
    char tmp[64];
    if (!date[0] && parse_date_cn(q, tmp, sizeof(tmp)))
        ls_json_set(v, "date", ls_json_string(tmp));
    if (!cust_type[0] && pick_cust_type_cn(q, tmp, sizeof(tmp)))
        ls_json_set(v, "cust_type", ls_json_string(tmp));
    if (!room_type[0] && strcmp(intent, "room") == 0) {
        if (syn_pick(q, "room_type", tmp, sizeof(tmp)))
            ls_json_set(v, "room_type", ls_json_string(tmp));
    }
    if (!building[0] && strcmp(intent, "room") == 0) {
        if (syn_pick(q, "building", tmp, sizeof(tmp)))
            ls_json_set(v, "building", ls_json_string(tmp));
    }
    if (!category[0] && strcmp(intent, "room") == 0) {
        if (syn_pick(q, "category", tmp, sizeof(tmp)))
            ls_json_set(v, "category", ls_json_string(tmp));
    }
    int nights = (int)ls_json_get_number(v, "nights", 0);
    if (nights <= 0) { int x = syn_qty(q, "unit", "nights"); if (x > 0) ls_json_set(v, "nights", ls_json_number(x)); }
    int rooms = (int)ls_json_get_number(v, "rooms", 0);
    if (rooms <= 0) { int x = syn_qty(q, "unit", "rooms"); if (x > 0) ls_json_set(v, "rooms", ls_json_number(x)); }
    int people = (int)ls_json_get_number(v, "people", 0);
    if (people <= 0 && strcmp(intent, "venue") == 0) {
        int x = syn_qty(q, "unit", "people");
        if (x > 0) ls_json_set(v, "people", ls_json_number(x));
    }
    if (!layout[0] && strcmp(intent, "venue") == 0) {
        if (syn_pick(q, "layout", tmp, sizeof(tmp)))
            ls_json_set(v, "layout", ls_json_string(tmp));
    }
    int is_full_day = (int)ls_json_get_number(v, "is_full_day", -1);
    if (is_full_day < 0) {
        if (syn_has(q, "duration", "full")) ls_json_set(v, "is_full_day", ls_json_number(1));
        else if (syn_has(q, "duration", "half")) ls_json_set(v, "is_full_day", ls_json_number(0));
    }
    int hours = (int)ls_json_get_number(v, "hours", 0);
    if (hours <= 0) { int x = syn_qty(q, "unit", "hours"); if (x > 0) ls_json_set(v, "hours", ls_json_number(x)); }
    int nr = (int)ls_json_get_number(v, "need_rehearsal", -1);
    if (nr < 0 && strcmp(intent, "venue") == 0) {
        if (syn_has(q, "rehearsal", "no")) ls_json_set(v, "need_rehearsal", ls_json_number(0));
        else if (syn_has(q, "rehearsal", "yes")) ls_json_set(v, "need_rehearsal", ls_json_number(1));
    }
    int nl = (int)ls_json_get_number(v, "need_lighting", -1);
    if (nl < 0 && strcmp(intent, "venue") == 0) {
        if (syn_has(q, "lighting", "no")) ls_json_set(v, "need_lighting", ls_json_number(0));
        else if (syn_has(q, "lighting", "yes")) ls_json_set(v, "need_lighting", ls_json_number(1));
    }
    int ns = (int)ls_json_get_number(v, "need_screen", -1);
    if (ns < 0 && strcmp(intent, "venue") == 0) {
        if (syn_has(q, "screen", "no")) ls_json_set(v, "need_screen", ls_json_number(0));
        else if (syn_has(q, "screen", "yes")) ls_json_set(v, "need_screen", ls_json_number(1));
    }
    /* 备注设备: 从客户描述的设备需求中提取(茶水/话筒/投影仪/LED/其他) */
    if (!need_equip[0] && strcmp(intent, "venue") == 0) {
        if (syn_has_any(q, "negation")) {
            ls_json_set(v, "need_equip", ls_json_string("(无)"));
        } else {
            char buf[128]; buf[0] = '\0';
            if (syn_has(q, "equip", "茶水"))   strncat(buf, "茶水;",   sizeof(buf) - strlen(buf) - 1);
            if (syn_has(q, "equip", "话筒"))   strncat(buf, "话筒;",   sizeof(buf) - strlen(buf) - 1);
            if (syn_has(q, "equip", "投影仪")) strncat(buf, "投影仪;", sizeof(buf) - strlen(buf) - 1);
            if (syn_has(q, "equip", "LED"))    strncat(buf, "LED;",    sizeof(buf) - strlen(buf) - 1);
            if (syn_has(q, "equip", "其他"))   strncat(buf, "其他;",   sizeof(buf) - strlen(buf) - 1);
            if (buf[0]) ls_json_set(v, "need_equip", ls_json_string(buf));
        }
    }
    double catering = ls_json_get_number(v, "catering_amount", -1);
    if (catering < 0 && strcmp(intent, "venue") == 0) {
        if (syn_has(q, "catering", "none")) ls_json_set(v, "catering_amount", ls_json_number(0));
        else { int x = syn_qty(q, "unit", "tables"); if (x > 0) ls_json_set(v, "catering_amount", ls_json_number(x)); }
    }
    /* 会场别名: 可在 synonyms.json 的 venue 分组配置 "会场id": ["别名1","别名2"] */
    if ((int)ls_json_get_number(v, "venue_id", 0) <= 0 && strcmp(intent, "venue") == 0) {
        char vbuf[32];
        if (syn_pick(q, "venue", vbuf, sizeof(vbuf))) {
            int vid = atoi(vbuf);
            if (vid > 0) ls_json_set(v, "venue_id", ls_json_number(vid));
        }
    }
    int venue_id = (int)ls_json_get_number(v, "venue_id", 0);
    if (venue_id <= 0 && strcmp(intent, "venue") == 0) {
        char* vpp = join_path(g_data_dir, "venue_pricing.json");
        ls_venue_price* vps = NULL; int vpn = 0;
        if (ls_pricing_load_venues(vpp, &vps, &vpn) == 0) {
            int i;
            for (i = 0; i < vpn; i++) {
                if (strstr(q, vps[i].name) || strstr(vps[i].name, q)) {
                    ls_json_set(v, "venue_id", ls_json_number(vps[i].id));
                    break;
                }
            }
            ls_pricing_free_venues(vps);
        }
        free(vpp);
    }

    ls_json_set(j, "conv", ls_json_number(1));
    ls_json_set(j, "intent", ls_json_string(intent));

    /* 同步返回当前临时方案(每轮更新) */
    if (strcmp(intent, "room") == 0) build_partial_room(j, v, ls_json_get_string(v, "date", ""));
    else build_partial_venue(j, v, ls_json_get_string(v, "date", ""));

    /* 缺参则生成提问 */
    const char* question = NULL;
    const char* param = NULL;
    ls_json* opts = NULL;

    if (strcmp(intent, "room") == 0) {
        if (!ls_json_get_string(v, "date", "")[0]) {
            param = "date";
            question = "好的，请问入住日期是哪天？可以说“今天”“明天”，或如“8月25日”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("今天"));
            ls_json_array_add(opts, ls_json_string("明天"));
            ls_json_array_add(opts, ls_json_string("后天"));
        } else if (!ls_json_get_string(v, "cust_type", "")[0]) {
            param = "cust_type";
            question = "请问客户类型是？政府及企业团队，还是研学及旅行社？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("政府及企业团队"));
            ls_json_array_add(opts, ls_json_string("研学及旅行社"));
        } else if (!ls_json_get_string(v, "room_type", "")[0]) {
            param = "room_type";
            question = "【房间种类】请问需要什么房间种类？大床房、双床房，还是套房？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("大床房"));
            ls_json_array_add(opts, ls_json_string("双床房"));
            ls_json_array_add(opts, ls_json_string("套房"));
        } else if (!ls_json_get_string(v, "category", "")[0]) {
            param = "category";
            question = "【早餐类型】请问早餐类型？含早（自助早餐），还是净房（不含早餐）？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("含早（自助早餐）"));
            ls_json_array_add(opts, ls_json_string("净房（不含早餐）"));
        } else if (!ls_json_get_string(v, "building", "")[0]) {
            param = "building";
            question = "请问希望入住哪栋楼？1号楼、2号楼，还是3号楼？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("1号楼"));
            ls_json_array_add(opts, ls_json_string("2号楼"));
            ls_json_array_add(opts, ls_json_string("3号楼"));
        } else if ((int)ls_json_get_number(v, "nights", 0) <= 0) {
            param = "nights";
            question = "请问入住几晚？例如“2晚”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("1晚"));
            ls_json_array_add(opts, ls_json_string("2晚"));
            ls_json_array_add(opts, ls_json_string("3晚"));
        } else if ((int)ls_json_get_number(v, "rooms", 0) <= 0) {
            param = "rooms";
            question = "请问需要几间房？例如“2间”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("1间"));
            ls_json_array_add(opts, ls_json_string("2间"));
            ls_json_array_add(opts, ls_json_string("3间"));
        }
    } else {
        if (!ls_json_get_string(v, "date", "")[0]) {
            param = "date";
            question = "好的，请问使用日期是哪天？可以说“今天”“明天”，或如“8月25日”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("今天"));
            ls_json_array_add(opts, ls_json_string("明天"));
            ls_json_array_add(opts, ls_json_string("后天"));
        } else if (!ls_json_get_string(v, "cust_type", "")[0]) {
            param = "cust_type";
            question = "请问客户类型是？政府及企业团队，还是研学及旅行社？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("政府及企业团队"));
            ls_json_array_add(opts, ls_json_string("研学及旅行社"));
        } else if ((int)ls_json_get_number(v, "venue_id", 0) <= 0) {
            param = "venue_id";
            question = "请问选择哪个会场？可说出名称，例如“一楼A厅”";
            opts = ls_json_array();
            {
                char* vpp = join_path(g_data_dir, "venue_pricing.json");
                ls_venue_price* vps = NULL; int vpn = 0;
                if (ls_pricing_load_venues(vpp, &vps, &vpn) == 0) {
                    int i, cap = vpn < 8 ? vpn : 8;
                    for (i = 0; i < cap; i++) ls_json_array_add(opts, ls_json_string(vps[i].name));
                    ls_pricing_free_venues(vps);
                }
                free(vpp);
            }
        } else if ((int)ls_json_get_number(v, "people", 0) <= 0) {
            param = "people";
            question = "【人数】请问预计参会人数？例如“50人”“100位”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("30人"));
            ls_json_array_add(opts, ls_json_string("50人"));
            ls_json_array_add(opts, ls_json_string("100人"));
            ls_json_array_add(opts, ls_json_string("200人"));
        } else if (!ls_json_get_string(v, "layout", "")[0]) {
            param = "layout";
            question = "【布局】请问会场布局？剧院式、课桌式、岛屿式，还是圆桌式？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("剧院式"));
            ls_json_array_add(opts, ls_json_string("课桌式"));
            ls_json_array_add(opts, ls_json_string("岛屿式"));
            ls_json_array_add(opts, ls_json_string("圆桌式"));
        } else if (!ls_json_get_string(v, "need_equip", "")[0]) {
            param = "need_equip";
            question = "【备注设备】请问还需要哪些设备/备注？如茶水服务、话筒、投影仪、LED屏，或其他设备（可多项，如“茶水话筒投影仪”）；若无额外需求请说“不需要”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("茶水服务"));
            ls_json_array_add(opts, ls_json_string("话筒"));
            ls_json_array_add(opts, ls_json_string("投影仪"));
            ls_json_array_add(opts, ls_json_string("不需要额外设备"));
        } else if ((int)ls_json_get_number(v, "is_full_day", -1) < 0 && (int)ls_json_get_number(v, "hours", 0) <= 0) {
            param = "duration";
            question = "请问会场使用时长？半天、全天，还是几小时？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("半天"));
            ls_json_array_add(opts, ls_json_string("全天"));
            ls_json_array_add(opts, ls_json_string("3小时"));
            ls_json_array_add(opts, ls_json_string("4小时"));
        } else if ((int)ls_json_get_number(v, "need_rehearsal", -1) < 0) {
            param = "need_rehearsal";
            question = "是否需要当日彩排？（部分会场含当日彩排，超时需额外收费）";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("需要彩排"));
            ls_json_array_add(opts, ls_json_string("不需要彩排"));
        } else if ((int)ls_json_get_number(v, "need_lighting", -1) < 0) {
            param = "need_lighting";
            question = "是否需要舞美灯光？（如需灯光秀或特殊灯光效果需额外收费）";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("需要灯光"));
            ls_json_array_add(opts, ls_json_string("不需要灯光"));
        } else if ((int)ls_json_get_number(v, "need_screen", -1) < 0) {
            param = "need_screen";
            question = "是否需要使用大屏或LED屏幕？";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("需要大屏"));
            ls_json_array_add(opts, ls_json_string("不需要大屏"));
        } else if (ls_json_get_number(v, "catering_amount", -1) < 0) {
            param = "catering_amount";
            question = "是否需要用餐？如果需要，请说桌数（如“10桌”），或说“不需要用餐”";
            opts = ls_json_array();
            ls_json_array_add(opts, ls_json_string("不需要用餐"));
            ls_json_array_add(opts, ls_json_string("5桌"));
            ls_json_array_add(opts, ls_json_string("10桌"));
            ls_json_array_add(opts, ls_json_string("20桌"));
        }
    }

    if (param) {
        /* 关键: 回传已收集的会话参数, 供前端回填会话状态, 避免多轮丢失参数导致死循环 */
        const char* pn[8];
        const char* ss[8] = { "date", "cust_type", "room_type", "category", "building", "layout", "need_equip", NULL };
        int si;
        for (si = 0; ss[si]; si++) {
            const char* val = ls_json_get_string(v, ss[si], "");
            if (val[0]) ls_json_set(j, ss[si], ls_json_string(val));
        }
        pn[0] = "nights"; pn[1] = "rooms"; pn[2] = "venue_id"; pn[3] = "people";
        pn[4] = "is_full_day"; pn[5] = "hours"; pn[6] = "need_rehearsal"; pn[7] = NULL;
        for (si = 0; pn[si]; si++) {
            double num = ls_json_get_number(v, pn[si], 0);
            if (num != 0) ls_json_set(j, pn[si], ls_json_number((int)num));
        }
        double catering = ls_json_get_number(v, "catering_amount", -1);
        if (catering >= 0) ls_json_set(j, "catering_amount", ls_json_number(catering));
        double nl = ls_json_get_number(v, "need_lighting", -1);
        if (nl >= 0) ls_json_set(j, "need_lighting", ls_json_number((int)nl));
        double ns = ls_json_get_number(v, "need_screen", -1);
        if (ns >= 0) ls_json_set(j, "need_screen", ls_json_number((int)ns));

        ls_json_set(j, "done", ls_json_number(0));
        ls_json_set(j, "param", ls_json_string(param));
        ls_json_set(j, "question", ls_json_string(question));
        if (opts) ls_json_set(j, "options", opts);
        else ls_json_set(j, "options", ls_json_array());
        ls_json_free(req);
        ls_json_free(v);
        return j;
    }
    if (opts) ls_json_free(opts);

    /* 参数齐全 */
    ls_json_set(j, "done", ls_json_number(1));
    char ans[512];
    if (strcmp(intent, "room") == 0) {
        snprintf(ans, sizeof(ans), "已为您生成客房报价方案（%s · %s · %s · %s · %s，%d晚%d间）：",
                 cust_cn(ls_json_get_string(v, "cust_type", "")),
                 ls_json_get_string(v, "date", ""),
                 ls_json_get_string(v, "building", ""),
                 ls_json_get_string(v, "room_type", ""),
                 ls_json_get_string(v, "category", ""),
                 (int)ls_json_get_number(v, "nights", 0),
                 (int)ls_json_get_number(v, "rooms", 0));
        ls_json_set(j, "goto", ls_json_string("rooms"));
        ls_json_set(j, "type", ls_json_string("room"));
        ls_json_set(j, "date", ls_json_string(ls_json_get_string(v, "date", "")));
        ls_json_set(j, "room_type", ls_json_string(ls_json_get_string(v, "room_type", "")));
        ls_json_set(j, "building", ls_json_string(ls_json_get_string(v, "building", "")));
        ls_json_set(j, "category", ls_json_string(ls_json_get_string(v, "category", "")));
        ls_json_set(j, "nights", ls_json_number((int)ls_json_get_number(v, "nights", 0)));
        ls_json_set(j, "rooms", ls_json_number((int)ls_json_get_number(v, "rooms", 0)));
        {
            /* 计算单间房总价作为确认下单默认金额 */
            char* pp = join_path(g_data_dir, "room_pricing.json");
            ls_room_price* arr = NULL; int n = 0;
            if (ls_pricing_load_rooms(pp, &arr, &n) == 0) {
                ls_customer_type ct = strcmp(ls_json_get_string(v, "cust_type", ""), "study_travel") == 0 ? LS_CUST_STUDY_TRAVEL : LS_CUST_GOV_ENTERPRISE;
                ls_room_price out[8];
                int m = ls_pricing_query_rooms(arr, n, NULL, ls_json_get_string(v, "room_type", ""), ls_json_get_string(v, "category", ""), ct, out, 8);
                if (m > 0) {
                    double fee = ls_pricing_calc_room(&out[0], (int)ls_json_get_number(v, "nights", 0), (int)ls_json_get_number(v, "rooms", 0));
                    ls_json_set(j, "amount", ls_json_number(fee));
                }
                ls_pricing_free_rooms(arr);
            }
            free(pp);
        }
        /* 无可满足房型: 提示重新选择房间种类/分类 */
        if (!ls_json_get(j, "cards")) {
            ls_json_set(j, "no_match", ls_json_number(1));
            ls_json_set(j, "retry_param", ls_json_string("room_type"));
            snprintf(ans, sizeof(ans), "抱歉，当前 %s · %s 没有可满足的房型（该客户类型/早餐类型下无匹配），请重新选择房间种类或分类。",
                     cust_cn(ls_json_get_string(v, "cust_type", "")),
                     ls_json_get_string(v, "category", ""));
        }
    } else {
        char dur[32];
        int h2 = (int)ls_json_get_number(v, "hours", 0);
        int fd = (int)ls_json_get_number(v, "is_full_day", -1);
        if (h2 > 0) snprintf(dur, sizeof(dur), "%d 小时", h2);
        else if (fd > 0) snprintf(dur, sizeof(dur), "全天");
        else snprintf(dur, sizeof(dur), "半天");
        const char* layout = ls_json_get_string(v, "layout", "");
        const char* lc = "未知";
        if (strcmp(layout, "theater") == 0) lc = "剧院式";
        else if (strcmp(layout, "classroom") == 0) lc = "课桌式";
        else if (strcmp(layout, "island") == 0) lc = "岛屿式";
        else if (strcmp(layout, "round_table") == 0) lc = "圆桌式";
        snprintf(ans, sizeof(ans), "已为您生成会场报价方案（%s · %s · %s · %d人 · %s）：合计 ¥%.0f",
                 cust_cn(ls_json_get_string(v, "cust_type", "")),
                 ls_json_get_string(v, "date", ""), dur,
                 (int)ls_json_get_number(v, "people", 0), lc,
                 ls_json_get_number(j, "total", 0));
        ls_json_set(j, "goto", ls_json_string("venues"));
        ls_json_set(j, "type", ls_json_string("venue"));
        ls_json_set(j, "date", ls_json_string(ls_json_get_string(v, "date", "")));
        ls_json_set(j, "amount", ls_json_number(ls_json_get_number(j, "total", 0)));
        ls_json_set(j, "people", ls_json_number((int)ls_json_get_number(v, "people", 0)));
        ls_json_set(j, "layout", ls_json_string(lc));
        /* 会场名称: 从 id 反查 */
        {
            char* vpp = join_path(g_data_dir, "venue_pricing.json");
            ls_venue_price* vps = NULL; int vpn = 0;
            if (ls_pricing_load_venues(vpp, &vps, &vpn) == 0) {
                int vid = (int)ls_json_get_number(v, "venue_id", 0), vi;
                for (vi = 0; vi < vpn; vi++) if (vps[vi].id == vid) { ls_json_set(j, "room_type", ls_json_string(vps[vi].name)); break; }
                ls_pricing_free_venues(vps);
            }
            free(vpp);
        }
    }
    ls_json_set(j, "answer", ls_json_string(ans));
    ls_json_free(req);
    ls_json_free(v);
    return j;
}

/* ============================================================
 *  HTTP 服务
 * ============================================================ */

static void handle_request(LS_SOCK sock, const char* method, const char* path, const char* body)
{
    if (strcmp(path, "/api/health") == 0) {
        ls_json* j = ls_json_object();
        ls_json_set(j, "status", ls_json_string("ok"));
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/query") == 0 && strcmp(method, "GET") == 0) {
        const char* q = strstr(body, "q=");
        const char* qv = q ? q + 2 : "";
        ls_json* j = handle_query(qv);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/quote") == 0 && strcmp(method, "POST") == 0) {
        ls_json* req = ls_json_parse(body);
        ls_json* j = calc_quote(req ? req : ls_json_object());
        if (req) ls_json_free(req);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/conv") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = handle_conv(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    /* ---- 看板数据 API ---- */
    if (strcmp(path, "/api/dashboard") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_dashboard();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/orders") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_orders();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/rooms") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_rooms();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/rooms/pricing") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_rooms_pricing();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/venues/pricing") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_venues_pricing();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/bookings") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_bookings();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/pay/orders") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_pay_orders();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/leads") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_leads();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/tasks") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_tasks();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/sync") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_sync();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/invoice") == 0 && strcmp(method, "GET") == 0) {
        ls_json* j = api_invoice();
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    /* ---- 看板写操作 API ---- */
    if (strcmp(path, "/api/booking") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = api_booking(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/booking/batch") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = api_booking_batch(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/pay/create") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = api_pay_create(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    if (strcmp(path, "/api/pay/mock") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = api_pay_mock(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    /* ---- 语音识别: 接收 base64 PCM, 返回文字 ---- */
    if (strcmp(path, "/api/asr") == 0 && strcmp(method, "POST") == 0) {
        ls_json* j = api_asr(body);
        char* s = ls_json_dump(j);
        send_json(sock, 200, "OK", s);
        free(s); ls_json_free(j);
        return;
    }
    /* ---- 语音合成: 文本转 mp3 音频(后端调用 edge-tts) ---- */
    if (strcmp(path, "/api/tts") == 0 && strcmp(method, "GET") == 0) {
        api_tts(sock, body && body[0] ? body : "");
        return;
    }
    /* ---- 静态页面托管: / 或 .html 返回看板页面 ---- */
    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strstr(path, ".html") != NULL || strcmp(path, "/index.html") == 0)) {
        char* p = join_path(g_data_dir, "web/index.html");
        char* text = ls_storage_read_text(p);
        free(p);
        if (text) {
            send_raw(sock, 200, "OK", "text/html; charset=utf-8", text);
            free(text);
        } else {
            ls_json* j = err_json("看板页面不存在");
            char* s = ls_json_dump(j);
            send_json(sock, 500, "Internal Server Error", s);
            free(s); ls_json_free(j);
        }
        return;
    }
    ls_json* j = err_json("未找到接口");
    char* s = ls_json_dump(j);
    send_json(sock, 404, "Not Found", s);
    free(s); ls_json_free(j);
}

/* 读取一行(以 \r\n 或 \n 结束) */
static int recv_line(LS_SOCK sock, char* buf, size_t n)
{
    size_t len = 0;
    for (;;) {
        if (len + 1 >= n) break;
        char c;
        int rc = (int)recv(sock, &c, 1, 0);
        if (rc <= 0) break;
        if (c == '\n') { if (len > 0 && buf[len - 1] == '\r') buf[len - 1] = '\0'; else buf[len] = '\0'; break; }
        buf[len++] = c;
        buf[len] = '\0';
    }
    return (int)len;
}

/* 简易 HTTP 服务循环(单线程阻塞, 适配演示) */
int ls_dashboard_start(int port, const char* data_dir)
{
    g_data_dir = data_dir;
    syn_init(data_dir);   /* 加载同义词表, 供对话理解"换个说法" */
#if LS_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { return -1; }
#endif
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(NULL, port_str, &hints, &res) != 0 || !res) { return -1; }
    LS_SOCK listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd == LS_INV) { freeaddrinfo(res); return -1; }
    {
        int one = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    }
    if (bind(listen_fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        freeaddrinfo(res); ls_sclose(listen_fd); return -1;
    }
    freeaddrinfo(res);
    if (listen(listen_fd, 16) != 0) { ls_sclose(listen_fd); return -1; }
    printf("\n  📊 实时数据看板已启动  http://localhost:%d\n\n", port);
    fflush(stdout);
    for (;;) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        LS_SOCK cfd = accept(listen_fd, (struct sockaddr*)&client, &clen);
        if (cfd == LS_INV) { ls_sleep_ms(10); continue; }
        char line[4096];
        if (recv_line(cfd, line, sizeof(line)) <= 0) { ls_sclose(cfd); continue; }
        /* 解析请求行: METHOD PATH HTTP/1.1 */
        char method[16] = {0}, path[4096] = {0};
        sscanf(line, "%15s %4095s", method, path);
        /* 跳过请求头 */
        char hdr[4096];
        int body_len = 0;
        while (recv_line(cfd, hdr, sizeof(hdr)) > 0 && hdr[0] != '\0') {
            if (ls_strnicmp(hdr, "Content-Length:", 15) == 0) body_len = atoi(hdr + 15);
        }
        /* 读取 body */
        char* body = NULL;
        if (body_len > 0) {
            body = (char*)malloc((size_t)body_len + 1);
            int got = 0;
            while (got < body_len) {
                int rc = (int)recv(cfd, body + got, body_len - got, 0);
                if (rc <= 0) break;
                got += rc;
            }
            body[got] = '\0';
        }
        /* 解析查询串: 截断 path, 并把 query 作为 GET 请求体传入 handle_request
           (GET 无 body, 这样 /api/tts、/api/query 等才能取到参数) */
        char* qmark = strchr(path, '?');
        const char* query = "";
        if (qmark) { *qmark = '\0'; query = qmark + 1; }
        const char* final_body = (body && body[0]) ? (const char*)body : query;
        handle_request(cfd, method, path, final_body);
        if (body) free(body);
        ls_sclose(cfd);
    }
    ls_sclose(listen_fd);
#if LS_WINDOWS
    WSACleanup();
#endif
    return 0;
}

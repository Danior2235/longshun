/*
 * json.c - 零依赖轻量级 JSON 解析/序列化实现
 * 递归下降解析器 + 字符串构建器序列化。
 */
#include "json.h"

/* ---------------- 内部辅助 ---------------- */

static void json_ensure_capacity(ls_json* j, int need)
{
    if (j->capacity >= need) return;
    int new_cap = j->capacity ? j->capacity : 4;
    while (new_cap < need) new_cap *= 2;
    ls_json_pair* ni = (ls_json_pair*)realloc(j->items,
                            (size_t)new_cap * sizeof(ls_json_pair));
    if (!ni) return;
    j->items = ni;
    j->capacity = new_cap;
}

static void json_append_char(char** buf, size_t* len, size_t* cap, char c)
{
    if (*len + 2 > *cap) {
        *cap = *cap ? *cap * 2 : 128;
        *buf = (char*)realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
}

static void json_append_str(char** buf, size_t* len, size_t* cap, const char* s)
{
    if (!s) return;
    while (*s) json_append_char(buf, len, cap, *s++);
}

static void json_append_digits(char** buf, size_t* len, size_t* cap, double v)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.17g", v);
    json_append_str(buf, len, cap, tmp);
}

/* 字符串转义输出 */
static void json_escape_append(char** buf, size_t* len, size_t* cap, const char* s)
{
    json_append_char(buf, len, cap, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  json_append_str(buf, len, cap, "\\\""); break;
            case '\\': json_append_str(buf, len, cap, "\\\\"); break;
            case '\n': json_append_str(buf, len, cap, "\\n"); break;
            case '\r': json_append_str(buf, len, cap, "\\r"); break;
            case '\t': json_append_str(buf, len, cap, "\\t"); break;
            case '\b': json_append_str(buf, len, cap, "\\b"); break;
            case '\f': json_append_str(buf, len, cap, "\\f"); break;
            default:
                if (c < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", c);
                    json_append_str(buf, len, cap, hex);
                } else {
                    json_append_char(buf, len, cap, (char)c);
                }
        }
    }
    json_append_char(buf, len, cap, '"');
}

/* ---------------- 解析器 ---------------- */

typedef struct {
    const char* p;
    const char* end;
    int failed;
} json_parser;

static void parser_skip_ws(json_parser* ps)
{
    while (ps->p < ps->end &&
           (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
        ps->p++;
}

static ls_json* parse_value(json_parser* ps);

static ls_json* parse_string(json_parser* ps)
{
    /* ps->p 指向 '"' */
    ps->p++;
    size_t cap = 64, len = 0;
    char* out = (char*)malloc(cap);
    if (!out) { ps->failed = 1; return NULL; }

    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == '"') {
            ps->p++;
            out[len] = '\0';
            ls_json* j = ls_json_string(out);
            free(out);
            return j;
        }
        if (c == '\\') {
            ps->p++;
            if (ps->p >= ps->end) break;
            char e = *ps->p;
            switch (e) {
                case '"':  json_append_char(&out, &len, &cap, '"');  break;
                case '\\': json_append_char(&out, &len, &cap, '\\'); break;
                case '/':  json_append_char(&out, &len, &cap, '/');  break;
                case 'b':  json_append_char(&out, &len, &cap, '\b'); break;
                case 'f':  json_append_char(&out, &len, &cap, '\f'); break;
                case 'n':  json_append_char(&out, &len, &cap, '\n'); break;
                case 'r':  json_append_char(&out, &len, &cap, '\r'); break;
                case 't':  json_append_char(&out, &len, &cap, '\t'); break;
                case 'u': {
                    /* \uXXXX 仅处理基本多语言平面 */
                    unsigned code = 0;
                    int i;
                    if (ps->p + 4 >= ps->end) { ps->failed = 1; break; }
                    for (i = 1; i <= 4; i++) {
                        char h = ps->p[i];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                        else { ps->failed = 1; break; }
                    }
                    ps->p += 4;
                    /* 以 UTF-8 编码输出 */
                    if (code < 0x80) {
                        json_append_char(&out, &len, &cap, (char)code);
                    } else if (code < 0x800) {
                        json_append_char(&out, &len, &cap, (char)(0xC0 | (code >> 6)));
                        json_append_char(&out, &len, &cap, (char)(0x80 | (code & 0x3F)));
                    } else {
                        json_append_char(&out, &len, &cap, (char)(0xE0 | (code >> 12)));
                        json_append_char(&out, &len, &cap, (char)(0x80 | ((code >> 6) & 0x3F)));
                        json_append_char(&out, &len, &cap, (char)(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: ps->failed = 1; break;
            }
            ps->p++;
        } else {
            json_append_char(&out, &len, &cap, c);
            ps->p++;
        }
    }
    free(out);
    ps->failed = 1;
    return NULL;
}

static ls_json* parse_number(json_parser* ps)
{
    const char* start = ps->p;
    while (ps->p < ps->end &&
           (isdigit((unsigned char)*ps->p) || *ps->p == '.' || *ps->p == '-' ||
            *ps->p == '+' || *ps->p == 'e' || *ps->p == 'E'))
        ps->p++;
    size_t n = (size_t)(ps->p - start);
    char* tmp = (char*)malloc(n + 1);
    if (!tmp) { ps->failed = 1; return NULL; }
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    ls_json* j = ls_json_number(atof(tmp));
    free(tmp);
    return j;
}

static ls_json* parse_array(json_parser* ps)
{
    ls_json* arr = ls_json_array();
    if (!arr) { ps->failed = 1; return NULL; }
    ps->p++; /* '[' */
    parser_skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        parser_skip_ws(ps);
        ls_json* v = parse_value(ps);
        if (!v) { ls_json_free(arr); ps->failed = 1; return NULL; }
        ls_json_array_add(arr, v);
        parser_skip_ws(ps);
        if (ps->p >= ps->end) { ls_json_free(arr); ps->failed = 1; return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        ls_json_free(arr); ps->failed = 1; return NULL;
    }
    return arr;
}

static ls_json* parse_object(json_parser* ps)
{
    ls_json* obj = ls_json_object();
    if (!obj) { ps->failed = 1; return NULL; }
    ps->p++; /* '{' */
    parser_skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        parser_skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') { ls_json_free(obj); ps->failed = 1; return NULL; }
        ls_json* key_j = parse_string(ps);
        if (!key_j || !key_j->string) { ls_json_free(obj); if (key_j) ls_json_free(key_j); ps->failed = 1; return NULL; }
        char* key = ls_strdup(key_j->string);
        ls_json_free(key_j);
        parser_skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') { free(key); ls_json_free(obj); ps->failed = 1; return NULL; }
        ps->p++;
        parser_skip_ws(ps);
        ls_json* v = parse_value(ps);
        if (!v) { free(key); ls_json_free(obj); ps->failed = 1; return NULL; }
        ls_json_set(obj, key, v);
        free(key);
        parser_skip_ws(ps);
        if (ps->p >= ps->end) { ls_json_free(obj); ps->failed = 1; return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        ls_json_free(obj); ps->failed = 1; return NULL;
    }
    return obj;
}

static ls_json* parse_value(json_parser* ps)
{
    parser_skip_ws(ps);
    if (ps->p >= ps->end) { ps->failed = 1; return NULL; }
    char c = *ps->p;
    if (c == '{') return parse_object(ps);
    if (c == '[') return parse_array(ps);
    if (c == '"') return parse_string(ps);
    if (c == 'n' && ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
        ps->p += 4; return ls_json_null();
    }
    if (c == 't' && ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) {
        ps->p += 4; return ls_json_bool(true);
    }
    if (c == 'f' && ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) {
        ps->p += 5; return ls_json_bool(false);
    }
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(ps);
    ps->failed = 1;
    return NULL;
}

ls_json* ls_json_parse(const char* text)
{
    if (!text) return NULL;
    json_parser ps;
    ps.p = text;
    ps.end = text + strlen(text);
    ps.failed = 0;
    ls_json* j = parse_value(&ps);
    if (ps.failed || !j) {
        if (j) ls_json_free(j);
        return NULL;
    }
    parser_skip_ws(&ps);
    if (ps.p < ps.end) { /* 尾部有非空白字符 */
        ls_json_free(j);
        return NULL;
    }
    return j;
}

/* ---------------- 构造 ---------------- */

static ls_json* json_new(ls_json_type t)
{
    ls_json* j = (ls_json*)calloc(1, sizeof(ls_json));
    if (j) j->type = t;
    return j;
}

ls_json* ls_json_object(void) { return json_new(LS_JSON_OBJECT); }
ls_json* ls_json_array(void)  { return json_new(LS_JSON_ARRAY); }

ls_json* ls_json_string(const char* s)
{
    ls_json* j = json_new(LS_JSON_STRING);
    if (!j) return NULL;
    j->string = ls_strdup(s ? s : "");
    return j;
}

ls_json* ls_json_number(double v)
{
    ls_json* j = json_new(LS_JSON_NUMBER);
    if (j) j->number = v;
    return j;
}

ls_json* ls_json_bool(bool b)
{
    ls_json* j = json_new(LS_JSON_BOOL);
    if (j) j->boolean = b;
    return j;
}

ls_json* ls_json_null(void) { return json_new(LS_JSON_NULL); }

/* ---------------- 释放 ---------------- */

void ls_json_free(ls_json* j)
{
    if (!j) return;
    int i;
    for (i = 0; i < j->count; i++) {
        free(j->items[i].key);
        ls_json_free(j->items[i].value);
    }
    free(j->items);
    free(j->string);
    free(j);
}

/* ---------------- 对象操作 ---------------- */

void ls_json_set(ls_json* obj, const char* key, ls_json* value)
{
    if (!obj || obj->type != LS_JSON_OBJECT || !key || !value) return;
    int i;
    for (i = 0; i < obj->count; i++) {
        if (strcmp(obj->items[i].key, key) == 0) {
            ls_json_free(obj->items[i].value);
            obj->items[i].value = value;
            return;
        }
    }
    json_ensure_capacity(obj, obj->count + 1);
    obj->items[obj->count].key = ls_strdup(key);
    obj->items[obj->count].value = value;
    obj->count++;
}

ls_json* ls_json_get(const ls_json* obj, const char* key)
{
    if (!obj || obj->type != LS_JSON_OBJECT || !key) return NULL;
    int i;
    for (i = 0; i < obj->count; i++) {
        if (obj->items[i].key && strcmp(obj->items[i].key, key) == 0)
            return obj->items[i].value;
    }
    return NULL;
}

/* ---------------- 数组操作 ---------------- */

void ls_json_array_add(ls_json* arr, ls_json* value)
{
    if (!arr || arr->type != LS_JSON_ARRAY || !value) return;
    json_ensure_capacity(arr, arr->count + 1);
    arr->items[arr->count].key = NULL;
    arr->items[arr->count].value = value;
    arr->count++;
}

int ls_json_array_size(const ls_json* arr)
{
    return (arr && arr->type == LS_JSON_ARRAY) ? arr->count : 0;
}

ls_json* ls_json_array_get(const ls_json* arr, int idx)
{
    if (!arr || arr->type != LS_JSON_ARRAY || idx < 0 || idx >= arr->count) return NULL;
    return arr->items[idx].value;
}

/* ---------------- 序列化 ---------------- */

static void json_serialize(const ls_json* j, char** buf, size_t* len, size_t* cap,
                           int pretty, int depth)
{
    if (!j) { json_append_str(buf, len, cap, "null"); return; }
    int i;
    switch (j->type) {
        case LS_JSON_NULL:  json_append_str(buf, len, cap, "null"); break;
        case LS_JSON_BOOL:  json_append_str(buf, len, cap, j->boolean ? "true" : "false"); break;
        case LS_JSON_NUMBER: json_append_digits(buf, len, cap, j->number); break;
        case LS_JSON_STRING: json_escape_append(buf, len, cap, j->string ? j->string : ""); break;
        case LS_JSON_ARRAY: {
            json_append_char(buf, len, cap, '[');
            for (i = 0; i < j->count; i++) {
                if (i) json_append_char(buf, len, cap, ',');
                if (pretty) {
                    json_append_char(buf, len, cap, '\n');
                    int k;
                    for (k = 0; k <= depth; k++) json_append_str(buf, len, cap, "  ");
                }
                json_serialize(j->items[i].value, buf, len, cap, pretty, depth + 1);
            }
            if (pretty && j->count) {
                json_append_char(buf, len, cap, '\n');
                int k;
                for (k = 0; k < depth; k++) json_append_str(buf, len, cap, "  ");
            }
            json_append_char(buf, len, cap, ']');
            break;
        }
        case LS_JSON_OBJECT: {
            json_append_char(buf, len, cap, '{');
            for (i = 0; i < j->count; i++) {
                if (i) json_append_char(buf, len, cap, ',');
                if (pretty) {
                    json_append_char(buf, len, cap, '\n');
                    int k;
                    for (k = 0; k <= depth; k++) json_append_str(buf, len, cap, "  ");
                }
                json_escape_append(buf, len, cap, j->items[i].key);
                json_append_char(buf, len, cap, ':');
                if (pretty) json_append_char(buf, len, cap, ' ');
                json_serialize(j->items[i].value, buf, len, cap, pretty, depth + 1);
            }
            if (pretty && j->count) {
                json_append_char(buf, len, cap, '\n');
                int k;
                for (k = 0; k < depth; k++) json_append_str(buf, len, cap, "  ");
            }
            json_append_char(buf, len, cap, '}');
            break;
        }
    }
}

char* ls_json_dump(const ls_json* j)
{
    size_t len = 0, cap = 0;
    char* buf = NULL;
    json_serialize(j, &buf, &len, &cap, 0, 0);
    json_append_char(&buf, &len, &cap, '\0');
    return buf ? buf : ls_strdup("null");
}

char* ls_json_dump_pretty(const ls_json* j)
{
    size_t len = 0, cap = 0;
    char* buf = NULL;
    json_serialize(j, &buf, &len, &cap, 1, 0);
    json_append_char(&buf, &len, &cap, '\0');
    return buf ? buf : ls_strdup("null");
}

/* ---------------- 便捷取值 ---------------- */

const char* ls_json_get_string(const ls_json* obj, const char* key, const char* def)
{
    ls_json* v = ls_json_get(obj, key);
    if (v && v->type == LS_JSON_STRING && v->string) return v->string;
    return def;
}

double ls_json_get_number(const ls_json* obj, const char* key, double def)
{
    ls_json* v = ls_json_get(obj, key);
    if (v && v->type == LS_JSON_NUMBER) return v->number;
    return def;
}

int ls_json_get_bool(const ls_json* obj, const char* key, int def)
{
    ls_json* v = ls_json_get(obj, key);
    if (v && v->type == LS_JSON_BOOL) return v->boolean ? 1 : 0;
    return def;
}

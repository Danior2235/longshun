/*
 * storage.c - JSON 文件存储层实现
 */
#include "storage.h"
#include "log.h"

char* ls_storage_read_text(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = '\0';
    return buf;
}

ls_json* ls_storage_load(const char* path)
{
    char* text = ls_storage_read_text(path);
    if (!text) return NULL;
    ls_json* j = ls_json_parse(text);
    if (!j) LOG_WARN("解析文件失败: %s", path);
    free(text);
    return j;
}

int ls_storage_save(const char* path, ls_json* j)
{
    char* text = ls_json_dump_pretty(j);
    if (!text) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) { free(text); return -1; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    free(text);
    return 0;
}

int ls_storage_append_item(const char* path, ls_json* item)
{
    ls_json* root = ls_storage_load(path);
    if (!root) {
        root = ls_json_array();
    }
    if (root->type != LS_JSON_ARRAY) {
        ls_json_free(root);
        return -1;
    }
    ls_json_array_add(root, item);
    int rc = ls_storage_save(path, root);
    ls_json_free(root);
    return rc;
}

int ls_storage_count(const char* path)
{
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;
    int n = ls_json_array_size(root);
    ls_json_free(root);
    return n;
}

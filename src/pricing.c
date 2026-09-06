/*
 * pricing.c - 客房与会场定价体系实现
 */
#include "pricing.h"
#include "log.h"
#include "storage.h"

const char* ls_pricing_cust_name(ls_customer_type t)
{
    switch (t) {
        case LS_CUST_GOV_ENTERPRISE: return "政府及企业团队";
        case LS_CUST_STUDY_TRAVEL:   return "研学及旅行社";
        default: return "未知";
    }
}

const char* ls_pricing_breakfast_name(ls_breakfast_type t)
{
    switch (t) {
        case LS_BREAKFAST_BUFFET: return "自助早";
        case LS_BREAKFAST_TABLE:  return "桌早";
        default: return "未知";
    }
}

/* ---------------- 加载客房定价 ---------------- */

int ls_pricing_load_rooms(const char* path, ls_room_price** out, int* out_n)
{
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;
    if (root->type != LS_JSON_ARRAY) { ls_json_free(root); return -1; }

    int n = ls_json_array_size(root);
    *out = (ls_room_price*)calloc((size_t)(n > 0 ? n : 1), sizeof(ls_room_price));
    if (!*out) { ls_json_free(root); return -1; }

    int i, cnt = 0;
    for (i = 0; i < n; i++) {
        ls_json* o = ls_json_array_get(root, i);
        ls_room_price* r = &(*out)[cnt];
        ls_safe_copy(r->building, sizeof(r->building), ls_json_get_string(o, "building", ""));
        ls_safe_copy(r->room_type, sizeof(r->room_type), ls_json_get_string(o, "room_type", ""));
        ls_safe_copy(r->category, sizeof(r->category), ls_json_get_string(o, "category", ""));

        const char* ct = ls_json_get_string(o, "cust_type", "");
        if (strcmp(ct, "study_travel") == 0) r->cust_type = LS_CUST_STUDY_TRAVEL;
        else r->cust_type = LS_CUST_GOV_ENTERPRISE;

        const char* bt = ls_json_get_string(o, "breakfast", "");
        if (strcmp(bt, "table") == 0) r->breakfast = LS_BREAKFAST_TABLE;
        else r->breakfast = LS_BREAKFAST_BUFFET;

        r->price = ls_json_get_number(o, "price", 0);
        cnt++;
    }
    ls_json_free(root);
    *out_n = cnt;
    return 0;
}

void ls_pricing_free_rooms(ls_room_price* p) { free(p); }

/* ---------------- 加载会场定价 ---------------- */

static void parse_layout(ls_json* obj, ls_venue_layout* l)
{
    memset(l, 0, sizeof(*l));
    if (!obj || obj->type != LS_JSON_OBJECT) return;
    l->theater     = (int)ls_json_get_number(obj, "theater", 0);
    l->classroom   = (int)ls_json_get_number(obj, "classroom", 0);
    l->island      = (int)ls_json_get_number(obj, "island", 0);
    l->round_table = (int)ls_json_get_number(obj, "round_table", 0);
}

static void parse_equip(ls_json* obj, ls_venue_equipment* e)
{
    memset(e, 0, sizeof(*e));
    if (!obj || obj->type != LS_JSON_OBJECT) return;
    e->has_led        = ls_json_get_bool(obj, "has_led", 0);
    ls_safe_copy(e->led_size, sizeof(e->led_size), ls_json_get_string(obj, "led_size", ""));
    e->has_sound      = ls_json_get_bool(obj, "has_sound", 0);
    e->has_projector  = ls_json_get_bool(obj, "has_projector", 0);
    ls_safe_copy(e->projector_spec, sizeof(e->projector_spec), ls_json_get_string(obj, "projector_spec", ""));
    e->mic_count      = (int)ls_json_get_number(obj, "mic_count", 0);
    e->paper_per_person = (int)ls_json_get_number(obj, "paper_per_person", 0);
    e->pen_per_person = (int)ls_json_get_number(obj, "pen_per_person", 0);
    e->has_tea        = ls_json_get_bool(obj, "has_tea", 0);
    e->has_tv         = ls_json_get_bool(obj, "has_tv", 0);
    e->screen_fee     = ls_json_get_number(obj, "screen_fee", 0);
}

static void parse_rules(ls_json* obj, ls_venue_rules* r)
{
    memset(r, 0, sizeof(*r));
    if (!obj || obj->type != LS_JSON_OBJECT) return;
    r->min_tables              = ls_json_get_number(obj, "min_tables", 0);
    r->table_venue_fee         = ls_json_get_number(obj, "table_venue_fee", 0);
    r->rehearsal_included      = ls_json_get_bool(obj, "rehearsal_included", 0);
    r->rehearsal_extra_fee     = ls_json_get_number(obj, "rehearsal_extra_fee", 0);
    r->lighting_extra_fee      = ls_json_get_number(obj, "lighting_extra_fee", 0);
    r->buffet_water_half       = (int)ls_json_get_number(obj, "buffet_water_half", 0);
    r->buffet_water_full       = (int)ls_json_get_number(obj, "buffet_water_full", 0);
    r->min_catering_free_venue = ls_json_get_number(obj, "min_catering_free_venue", 0);
    r->insurance_required      = ls_json_get_bool(obj, "insurance_required", 0);
    r->is_empty_venue          = ls_json_get_bool(obj, "is_empty_venue", 0);
}

int ls_pricing_load_venues(const char* path, ls_venue_price** out, int* out_n)
{
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;
    if (root->type != LS_JSON_ARRAY) { ls_json_free(root); return -1; }

    int n = ls_json_array_size(root);
    *out = (ls_venue_price*)calloc((size_t)(n > 0 ? n : 1), sizeof(ls_venue_price));
    if (!*out) { ls_json_free(root); return -1; }

    int i, cnt = 0;
    for (i = 0; i < n; i++) {
        ls_json* o = ls_json_array_get(root, i);
        ls_venue_price* v = &(*out)[cnt];
        v->id = (int)ls_json_get_number(o, "id", 0);
        ls_safe_copy(v->area, sizeof(v->area), ls_json_get_string(o, "area", ""));
        ls_safe_copy(v->name, sizeof(v->name), ls_json_get_string(o, "name", ""));
        v->capacity_min = (int)ls_json_get_number(o, "capacity_min", 0);
        v->capacity_max = (int)ls_json_get_number(o, "capacity_max", 0);
        v->area_sqm = ls_json_get_number(o, "area_sqm", 0);
        v->price_half_day = ls_json_get_number(o, "price_half_day", 0);
        v->price_full_day = ls_json_get_number(o, "price_full_day", 0);
        v->price_hourly = ls_json_get_number(o, "price_hourly", 0);
        v->price_by_hour = ls_json_get_bool(o, "price_by_hour", 0);
        parse_layout(ls_json_get(o, "layout"), &v->layout);
        parse_equip(ls_json_get(o, "equipment"), &v->equip);
        parse_rules(ls_json_get(o, "rules"), &v->rules);
        ls_safe_copy(v->notes, sizeof(v->notes), ls_json_get_string(o, "notes", ""));
        cnt++;
    }
    ls_json_free(root);
    *out_n = cnt;
    return 0;
}

void ls_pricing_free_venues(ls_venue_price* p) { free(p); }

/* ---------------- 加载时间规则 ---------------- */

int ls_pricing_load_time_rules(const char* path, ls_time_rules* out)
{
    memset(out, 0, sizeof(*out));
    ls_json* root = ls_storage_load(path);
    if (!root) return -1;
    out->half_day_hours = (int)ls_json_get_number(root, "half_day_hours", 4);
    out->evening_surcharge_pct = (int)ls_json_get_number(root, "evening_surcharge_pct", 40);
    out->overtime_2h_quarter = ls_json_get_bool(root, "overtime_2h_quarter", 1);
    out->overtime_over2h_half = ls_json_get_bool(root, "overtime_over2h_half", 1);
    out->catering_free_venue_threshold = ls_json_get_number(root, "catering_free_venue_threshold", 10000);
    ls_json_free(root);
    return 0;
}

/* ---------------- 查询 ---------------- */

int ls_pricing_query_rooms(const ls_room_price* arr, int n,
    const char* building, const char* room_type, const char* category,
    ls_customer_type cust_type, ls_room_price* out, int out_cap)
{
    int i, cnt = 0;
    for (i = 0; i < n && cnt < out_cap; i++) {
        const ls_room_price* r = &arr[i];
        if (building && building[0] && strcmp(r->building, building) != 0) continue;
        if (room_type && room_type[0] && strcmp(r->room_type, room_type) != 0) continue;
        if (category && category[0] && strcmp(r->category, category) != 0) continue;
        if ((int)cust_type >= 0 && r->cust_type != cust_type) continue;
        out[cnt++] = *r;
    }
    return cnt;
}

int ls_pricing_query_venues(const ls_venue_price* arr, int n,
    const char* area, const char* name, int min_capacity,
    ls_venue_price* out, int out_cap)
{
    int i, cnt = 0;
    for (i = 0; i < n && cnt < out_cap; i++) {
        const ls_venue_price* v = &arr[i];
        if (area && area[0] && strcmp(v->area, area) != 0) continue;
        if (name && name[0] && strcmp(v->name, name) != 0) continue;
        if (min_capacity > 0 && v->capacity_max < min_capacity) continue;
        out[cnt++] = *v;
    }
    return cnt;
}

/* ---------------- 费用计算 ---------------- */

double ls_pricing_calc_room(const ls_room_price* r, int nights, int rooms)
{
    if (!r || nights <= 0 || rooms <= 0) return 0;
    return r->price * nights * rooms;
}

double ls_pricing_calc_venue(const ls_venue_price* v,
    int is_full_day, int hours, int is_evening,
    int need_lighting, int need_screen, double catering_amount,
    const ls_time_rules* tr)
{
    if (!v || !tr) return 0;
    double base = 0;

    if (v->price_by_hour && v->price_hourly > 0) {
        base = v->price_hourly * (hours > 0 ? hours : 1);
    } else {
        if (is_full_day) base = v->price_full_day;
        else base = v->price_half_day;
        /* 如果半天价无效但全天价有效,回退到全天 */
        if (base <= 0 && v->price_full_day > 0) base = v->price_full_day;
    }

    /* 晚间加收 */
    if (is_evening && tr->evening_surcharge_pct > 0) {
        base += base * tr->evening_surcharge_pct / 100.0;
    }

    /* 舞美灯光 */
    if (need_lighting && v->rules.lighting_extra_fee > 0) {
        base += v->rules.lighting_extra_fee;
    }

    /* 额外大屏 */
    if (need_screen && v->equip.screen_fee > 0) {
        base += v->equip.screen_fee;
    }

    /* 用餐满额减免场地费 */
    if (v->rules.min_catering_free_venue > 0 &&
        catering_amount >= v->rules.min_catering_free_venue) {
        base = 0; /* 免场地费,但额外设备费仍保留? 按常见逻辑仅免场地基础费 */
        /* 重新加回设备附加费 */
        if (need_lighting && v->rules.lighting_extra_fee > 0)
            base += v->rules.lighting_extra_fee;
        if (need_screen && v->equip.screen_fee > 0)
            base += v->equip.screen_fee;
    }

    return base;
}

/* ---------------- 打印 ---------------- */

void ls_pricing_print_room(const ls_room_price* r)
{
    if (!r) return;
    printf("  %-10s | %-8s | %-6s | %-14s(%s) | %.0f 元\n",
           r->building, r->room_type, r->category,
           ls_pricing_cust_name(r->cust_type),
           ls_pricing_breakfast_name(r->breakfast),
           r->price);
}

void ls_pricing_print_venue(const ls_venue_price* v)
{
    if (!v) return;
    printf("  [%2d] %-8s | %-20s | 人数 %d-%d | %.1f㎡ | ",
           v->id, v->area, v->name, v->capacity_min, v->capacity_max, v->area_sqm);
    if (v->price_by_hour) {
        printf("%.0f 元/小时", v->price_hourly);
    } else {
        printf("半天 %.0f / 全天 %.0f", v->price_half_day, v->price_full_day);
    }
    printf("\n");
    if (v->layout.theater || v->layout.classroom || v->layout.island || v->layout.round_table) {
        printf("       布局: 剧院%d 课桌%d 岛屿%d 圆桌%d\n",
               v->layout.theater, v->layout.classroom, v->layout.island, v->layout.round_table);
    }
    if (v->equip.has_led || v->equip.has_sound || v->equip.mic_count > 0) {
        printf("       设备: LED%s%s | 音响%s | 话筒%d支 | 纸%d笔%d",
               v->equip.has_led ? "(含" : "(无",
               v->equip.has_led ? v->equip.led_size : "",
               v->equip.has_sound ? "(含)" : "(无)",
               v->equip.mic_count,
               v->equip.paper_per_person, v->equip.pen_per_person);
        if (v->equip.has_tea) printf(" | 茶水");
        if (v->equip.has_tv) printf(" | 电视");
        if (v->equip.screen_fee > 0) printf(" | 大屏附加%.0f元/天", v->equip.screen_fee);
        printf("\n");
    }
    if (v->rules.min_tables > 0 || v->rules.lighting_extra_fee > 0 || v->rules.is_empty_venue) {
        printf("       规则: ");
        if (v->rules.is_empty_venue) printf("空厅 ");
        if (v->rules.min_tables > 0) printf("起订%.0f桌/桌费%.0f元 ", v->rules.min_tables, v->rules.table_venue_fee);
        if (v->rules.lighting_extra_fee > 0) printf("灯光+%.0f元 ", v->rules.lighting_extra_fee);
        if (v->rules.rehearsal_included) printf("含彩排 ");
        if (v->rules.min_catering_free_venue > 0) printf("餐满%.0f免场地 ", v->rules.min_catering_free_venue);
        if (v->rules.insurance_required) printf("需保险 ");
        printf("\n");
    }
    if (v->notes[0]) {
        printf("       备注: %s\n", v->notes);
    }
}

/* ---------------- JSON 序列化 ---------------- */

ls_json* ls_pricing_room_to_json(const ls_room_price* r)
{
    ls_json* o = ls_json_object();
    ls_json_set(o, "building", ls_json_string(r->building));
    ls_json_set(o, "room_type", ls_json_string(r->room_type));
    ls_json_set(o, "category", ls_json_string(r->category));
    ls_json_set(o, "cust_type", ls_json_string(r->cust_type == LS_CUST_STUDY_TRAVEL ? "study_travel" : "government_enterprise"));
    ls_json_set(o, "breakfast", ls_json_string(r->breakfast == LS_BREAKFAST_TABLE ? "table" : "buffet"));
    ls_json_set(o, "price", ls_json_number(r->price));
    return o;
}

ls_json* ls_pricing_venue_to_json(const ls_venue_price* v)
{
    ls_json* o = ls_json_object();
    ls_json_set(o, "id", ls_json_number(v->id));
    ls_json_set(o, "area", ls_json_string(v->area));
    ls_json_set(o, "name", ls_json_string(v->name));
    ls_json_set(o, "capacity_min", ls_json_number(v->capacity_min));
    ls_json_set(o, "capacity_max", ls_json_number(v->capacity_max));
    ls_json_set(o, "area_sqm", ls_json_number(v->area_sqm));
    ls_json_set(o, "price_half_day", ls_json_number(v->price_half_day));
    ls_json_set(o, "price_full_day", ls_json_number(v->price_full_day));
    ls_json_set(o, "price_hourly", ls_json_number(v->price_hourly));
    ls_json_set(o, "price_by_hour", ls_json_bool(v->price_by_hour));

    ls_json* lo = ls_json_object();
    ls_json_set(lo, "theater", ls_json_number(v->layout.theater));
    ls_json_set(lo, "classroom", ls_json_number(v->layout.classroom));
    ls_json_set(lo, "island", ls_json_number(v->layout.island));
    ls_json_set(lo, "round_table", ls_json_number(v->layout.round_table));
    ls_json_set(o, "layout", lo);

    ls_json* eq = ls_json_object();
    ls_json_set(eq, "has_led", ls_json_bool(v->equip.has_led));
    ls_json_set(eq, "led_size", ls_json_string(v->equip.led_size));
    ls_json_set(eq, "has_sound", ls_json_bool(v->equip.has_sound));
    ls_json_set(eq, "has_projector", ls_json_bool(v->equip.has_projector));
    ls_json_set(eq, "projector_spec", ls_json_string(v->equip.projector_spec));
    ls_json_set(eq, "mic_count", ls_json_number(v->equip.mic_count));
    ls_json_set(eq, "paper_per_person", ls_json_number(v->equip.paper_per_person));
    ls_json_set(eq, "pen_per_person", ls_json_number(v->equip.pen_per_person));
    ls_json_set(eq, "has_tea", ls_json_bool(v->equip.has_tea));
    ls_json_set(eq, "has_tv", ls_json_bool(v->equip.has_tv));
    ls_json_set(eq, "screen_fee", ls_json_number(v->equip.screen_fee));
    ls_json_set(o, "equipment", eq);

    ls_json* ru = ls_json_object();
    ls_json_set(ru, "min_tables", ls_json_number(v->rules.min_tables));
    ls_json_set(ru, "table_venue_fee", ls_json_number(v->rules.table_venue_fee));
    ls_json_set(ru, "rehearsal_included", ls_json_bool(v->rules.rehearsal_included));
    ls_json_set(ru, "rehearsal_extra_fee", ls_json_number(v->rules.rehearsal_extra_fee));
    ls_json_set(ru, "lighting_extra_fee", ls_json_number(v->rules.lighting_extra_fee));
    ls_json_set(ru, "buffet_water_half", ls_json_number(v->rules.buffet_water_half));
    ls_json_set(ru, "buffet_water_full", ls_json_number(v->rules.buffet_water_full));
    ls_json_set(ru, "min_catering_free_venue", ls_json_number(v->rules.min_catering_free_venue));
    ls_json_set(ru, "insurance_required", ls_json_bool(v->rules.insurance_required));
    ls_json_set(ru, "is_empty_venue", ls_json_bool(v->rules.is_empty_venue));
    ls_json_set(o, "rules", ru);

    ls_json_set(o, "notes", ls_json_string(v->notes));
    return o;
}

/*
 * pricing.h - 龙顺庄园客房与会场定价体系
 * 将价格表拆解到最小粒度，支持查询、筛选与费用计算。
 */
#ifndef LS_PRICING_H
#define LS_PRICING_H

#include "common.h"
#include "json.h"

/* ---------------- 客房定价 ---------------- */

typedef enum {
    LS_CUST_GOV_ENTERPRISE = 0,  /* 政府及企业团队 */
    LS_CUST_STUDY_TRAVEL         /* 研学及旅行社 */
} ls_customer_type;

typedef enum {
    LS_BREAKFAST_BUFFET = 0,     /* 自助早 */
    LS_BREAKFAST_TABLE           /* 桌早 */
} ls_breakfast_type;

/* 单条客房报价记录 */
typedef struct {
    char building[32];           /* 楼栋: 庭院小院 / 商务楼 */
    char room_type[32];          /* 房间种类: 大床房 / 双床房 / 套房 */
    char category[16];           /* 分类: 净房 / 含早 */
    ls_customer_type cust_type;  /* 客户类型 */
    ls_breakfast_type breakfast; /* 早餐类型 */
    double price;                /* 报价(元) */
} ls_room_price;

/* ---------------- 会场定价 ---------------- */

/* 会场布局容量 */
typedef struct {
    int theater;      /* 剧院式 */
    int classroom;    /* 课桌式 */
    int island;       /* 岛屿式 */
    int round_table;  /* 圆桌式 */
} ls_venue_layout;

/* 设备明细 */
typedef struct {
    int has_led;           /* 是否含 LED */
    char led_size[32];     /* LED 尺寸, 如 "4M*10M" */
    int has_sound;         /* 是否含音响 */
    int has_projector;     /* 是否含投影仪 */
    char projector_spec[64]; /* 投影仪规格, 如 "120寸/3600流明" */
    int mic_count;         /* 话筒数量 */
    int paper_per_person;  /* 每人纸张数 */
    int pen_per_person;    /* 每人笔数 */
    int has_tea;           /* 是否含茶水 */
    int has_tv;            /* 是否含电视 */
    double screen_fee;     /* 大屏额外费用(元/天), 0 表示无 */
} ls_venue_equipment;

/* 计费规则 */
typedef struct {
    double min_tables;              /* 最低起订桌数, 0 表示无限制 */
    double table_venue_fee;         /* 每桌场地费(元), 0 表示无 */
    int rehearsal_included;         /* 是否含当日彩排 */
    double rehearsal_extra_fee;     /* 超时彩排额外费用 */
    double lighting_extra_fee;      /* 舞美灯光额外费用(元) */
    int buffet_water_half;          /* 半天含瓶装水次数 */
    int buffet_water_full;          /* 全天含瓶装水次数 */
    double min_catering_free_venue; /* 用餐满额免场地费金额, 0 表示无 */
    int insurance_required;         /* 是否需客人提供保险 */
    int is_empty_venue;             /* 是否空厅(仅场地) */
} ls_venue_rules;

/* 单条会场报价记录 */
typedef struct {
    int id;                   /* 序号 */
    char area[64];            /* 所在区域/楼层 */
    char name[64];            /* 会场名称 */
    int capacity_min;         /* 最少容纳人数 */
    int capacity_max;         /* 最多容纳人数 */
    double area_sqm;          /* 会场面积(平方米) */
    double price_half_day;    /* 半天价格(元), 0 表示不适用 */
    double price_full_day;    /* 全天价格(元), 0 表示不适用 */
    double price_hourly;      /* 小时价格(元), 0 表示不适用 */
    int price_by_hour;        /* 1=按小时计价, 0=按半天/全天计价 */
    ls_venue_layout layout;   /* 各布局容量 */
    ls_venue_equipment equip; /* 设备明细 */
    ls_venue_rules rules;     /* 计费规则 */
    char notes[512];          /* 其他备注 */
} ls_venue_price;

/* ---------------- 时间计费规则 ---------------- */
typedef struct {
    int half_day_hours;         /* 半天小时数(4) */
    int evening_surcharge_pct;  /* 晚间加收百分比(40) */
    int overtime_2h_quarter;    /* 超出2小时内按1/4天(1) */
    int overtime_over2h_half;   /* 超出2小时不足半天按半天(1) */
    double catering_free_venue_threshold; /* 用餐满额免场地费 */
} ls_time_rules;

/* ---------------- API ---------------- */

/* 加载客房定价 JSON 数组 */
int ls_pricing_load_rooms(const char* path, ls_room_price** out, int* out_n);
/* 加载会场定价 JSON 数组 */
int ls_pricing_load_venues(const char* path, ls_venue_price** out, int* out_n);
/* 加载时间计费规则 */
int ls_pricing_load_time_rules(const char* path, ls_time_rules* out);

void ls_pricing_free_rooms(ls_room_price* p);
void ls_pricing_free_venues(ls_venue_price* p);

/* 查询客房: building/room_type/category 传 NULL 表示不限, cust_type=-1 表示不限 */
int ls_pricing_query_rooms(const ls_room_price* arr, int n,
    const char* building, const char* room_type, const char* category,
    ls_customer_type cust_type, ls_room_price* out, int out_cap);

/* 查询会场: area/name 传 NULL 表示不限, min_capacity=0 表示不限 */
int ls_pricing_query_venues(const ls_venue_price* arr, int n,
    const char* area, const char* name, int min_capacity,
    ls_venue_price* out, int out_cap);

/* 计算会场预估费用
 *   is_full_day: 1=全天, 0=半天(仅当 price_by_hour=0 时有效)
 *   hours:       使用小时数(仅当 price_by_hour=1 时有效)
 *   is_evening:  1=晚间时段(加收40%)
 *   need_lighting: 1=需要舞美灯光
 *   need_screen:   1=需要额外大屏
 *   catering_amount: 餐饮消费金额(用于满额减免)
 */
double ls_pricing_calc_venue(const ls_venue_price* v,
    int is_full_day, int hours, int is_evening,
    int need_lighting, int need_screen, double catering_amount,
    const ls_time_rules* tr);

/* 计算客房预估费用(支持数量) */
double ls_pricing_calc_room(const ls_room_price* r, int nights, int rooms);

/* 打印单条记录 */
void ls_pricing_print_room(const ls_room_price* r);
void ls_pricing_print_venue(const ls_venue_price* v);

/* 转 JSON(用于 Dashboard 接口) */
ls_json* ls_pricing_room_to_json(const ls_room_price* r);
ls_json* ls_pricing_venue_to_json(const ls_venue_price* v);

/* 客户类型/早餐类型名称 */
const char* ls_pricing_cust_name(ls_customer_type t);
const char* ls_pricing_breakfast_name(ls_breakfast_type t);

#endif /* LS_PRICING_H */

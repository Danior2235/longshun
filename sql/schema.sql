-- ============================================================================
-- lszy_yx 核心数据表（简化版，PostgreSQL 风格）
-- 对应方案第 8 章。业务系统接入抽象后写入这些表，供画像/看板/方案生成读取。
-- 上线前按实际 PMS/CRM 字段微调；本文档表结构为统一数据地基。
-- ============================================================================

-- 1) 客户：酒店/餐饮/会议集团的多业态客户统一档案
CREATE TABLE IF NOT EXISTS customer (
    id              BIGSERIAL PRIMARY KEY,
    name            VARCHAR(200) NOT NULL,          -- 单位/客户名称
    industry        VARCHAR(100),                  -- 行业
    scale           VARCHAR(100),                  -- 规模（人数/预算量级）
    region          VARCHAR(100),                  -- 地区
    contact_name    VARCHAR(100),                  -- 对接人
    contact_phone   VARCHAR(50),
    source          VARCHAR(50),                   -- 线索来源(爬虫/微信/电话/转介绍)
    created_at      TIMESTAMPTZ DEFAULT now(),
    updated_at      TIMESTAMPTZ DEFAULT now()
);

-- 2) 商机/线索：含爬虫线索与销售自录，贯穿整个生命周期
CREATE TABLE IF NOT EXISTS opportunity (
    id              BIGSERIAL PRIMARY KEY,
    customer_id     BIGINT REFERENCES customer(id),
    title           VARCHAR(300),                  -- 商机标题
    source          VARCHAR(50),                   -- crawler_lead/wechat/phone/...
    stage           VARCHAR(50) DEFAULT 'lead',    -- lead→contact→proposal→won/lost
    budget          VARCHAR(100),                  -- 预算区间
    expected_date   DATE,                          -- 期望成交/举办时间
    owner           VARCHAR(100),                  -- 负责销售
    relevance       NUMERIC(3,2),                  -- 相关度(爬虫注入)
    raw_url         TEXT,                          -- 原始公告链接(爬虫)
    status          VARCHAR(20) DEFAULT 'open',    -- open/closed
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 3) 原始消息归档：5 种来源归一后的 raw_message（可追溯、可重算）
CREATE TABLE IF NOT EXISTS raw_message (
    id              BIGSERIAL PRIMARY KEY,
    session_id      VARCHAR(100),                  -- 一次会话/客户连续上下文
    customer_id     BIGINT REFERENCES customer(id),
    source          VARCHAR(50),                   -- wechat_text/ocr/phone_asr/crawler_lead/pms_event
    speaker_role    VARCHAR(20),                   -- sales/customer/system/unknown
    content         TEXT,
    sent_at         TIMESTAMPTZ,
    extra           JSONB,                         -- 来源特有字段(lead/segment/event)
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 4) 心脏抽取结果：raw_message → HEART_SCHEMA 结构化洞察
CREATE TABLE IF NOT EXISTS message_insight (
    id              BIGSERIAL PRIMARY KEY,
    session_id      VARCHAR(100),
    customer_id     BIGINT REFERENCES customer(id),
    insight         JSONB NOT NULL,                -- 见 core.schema.heart.HEART_SCHEMA
    extractor       VARCHAR(20) DEFAULT 'rule',    -- rule/llm
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 5) 房态：PMS 同步，驱动"实时房态看板"与报价可行性校验
CREATE TABLE IF NOT EXISTS room_status (
    id              BIGSERIAL PRIMARY KEY,
    room_type       VARCHAR(100),
    room_no         VARCHAR(50),
    status          VARCHAR(20),                   -- available/occupied/maintenance/blocked
    price           NUMERIC(10,2),
    effect_date     DATE,
    updated_at      TIMESTAMPTZ DEFAULT now()
);

-- 6) 订单：酒店/餐饮/会议各类订单统一
CREATE TABLE IF NOT EXISTS "order" (
    id              BIGSERIAL PRIMARY KEY,
    customer_id     BIGINT REFERENCES customer(id),
    type            VARCHAR(50),                   -- room/fnb/meeting
    amount          NUMERIC(12,2),
    status          VARCHAR(20),
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 7) 待办：next_best_action / commitments 落库，驱动销售跟进
CREATE TABLE IF NOT EXISTS todo (
    id              BIGSERIAL PRIMARY KEY,
    session_id      VARCHAR(100),
    customer_id     BIGINT REFERENCES customer(id),
    action          TEXT,
    due             TIMESTAMPTZ,
    owner           VARCHAR(100),
    done            BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 8) 方案/报价：AI 生成的报价方案、服务方案，关联历史案例
CREATE TABLE IF NOT EXISTS scheme (
    id              BIGSERIAL PRIMARY KEY,
    opportunity_id  BIGINT REFERENCES opportunity(id),
    type            VARCHAR(50),                   -- quote/proposal
    content         JSONB,                         -- 结构化方案(房型/餐标/会场/总价/毛利)
    margin_checked  BOOLEAN DEFAULT FALSE,          -- 是否过毛利红线校验
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- 9) 爬虫线索落库视图(与 opportunity 同源)：便于双轨(业务库)读取
CREATE OR REPLACE VIEW v_crawler_lead AS
SELECT id, customer_id, title, budget, expected_date, relevance, raw_url, created_at
FROM opportunity WHERE source = 'crawler_lead';

CREATE INDEX IF NOT EXISTS idx_raw_msg_session ON raw_message(session_id);
CREATE INDEX IF NOT EXISTS idx_opp_customer ON opportunity(customer_id);
CREATE INDEX IF NOT EXISTS idx_room_status_date ON room_status(effect_date);

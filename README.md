# lszy_yx — AI 销售助手（酒店 / 餐饮 / 会议多业态）

> 技术栈主线：**WorkBuddy（Agent 工作台） + 微信小程序（销售端） + 企微/微信对话导入 + 电话录音导入**
> 仓库根目录即项目主仓，当前阶段以「轨道 A（WorkBuddy 内部孵化）」为主，验证过的模块逐步沉淀为线上服务。

## 目录结构

```
lszy_yx/
├── docs/                      # 技术方案与汇报材料
│   └── AI销售助手-技术方案-v1.md
├── crawler/                   # ★ 每日招标 / 会议承办客户机会采集（本次新增）
│   ├── config/sources.yaml    #   可配置信息源 + 关键词（员工维护，免改代码）
│   ├── src/                   #   fetcher / parsers / matcher / storage
│   ├── run_daily.py           #   每日入口（--test 可离线验证）
│   └── output/                #   去重归档：leads.jsonl / leads_YYYYMMDD.json / leads.csv
└── README.md
```

## 模块说明

- **crawler**：投入最小、见效最快、完全合规的获客模块。政府/国企/大型企业的会议、住宿、培训、接待类采购几乎全部公开招标，是精准获客金矿。每日自动抓取 → 关键词相关性过滤 → 去重归档 → 推送销售。
- 详细设计见 `docs/AI销售助手-技术方案-v1.md` 第 14 章。

## 快速开始（crawler）

```bash
cd crawler
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

# 离线验证解析器（不发网络请求）
python run_daily.py --test

# 真实运行（需联网）
python run_daily.py

# 只看某来源
python run_daily.py --source 中国政府采购网 --limit 4
```

## 下一步

- 轨道 B：将 crawler 部署为定时任务（cron / K8s CronJob），结果写入业务库 `customer_opportunity` 表。
- 在 WorkBuddy 配置 `lead-hunter` Automation，每日 08:00 自动运行并推送新线索到小程序 + 企微群。
- 企微会话存档、AXB 中间号录音、PMS/MCP 对接，按技术方案路线图推进（见 docs）。

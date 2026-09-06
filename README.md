# 龙顺 AI 销售与客户运营助手(C 语言实现)

基于需求方案《AI 销售与客户运营助手 · 需求方案(v1.0)》实现的 C 语言核心系统,
面向酒店/餐饮/会议多业态,覆盖三大模块中适合 C 语言承担的高性能底层能力。

## 功能与需求方案对应

| 需求方案模块 | 功能 | 本实现 |
|---|---|---|
| 模块一: 客户线索获取 | 爬取招标网站/公开信息源,清洗后输出结构化线索 | `crawler` 多线程爬虫 + HTML/JSON 清洗提取 |
| 模块一: 商机转化 | 识别需求与成交概率,建议沟通时机与方式 | `lead` 评分模型 + 沟通建议 |
| 模块二: 实时数据看板 | 订单/房态/利润等关键指标统一看板 | `dashboard` HTTP JSON 服务 + 自然语言查询 |
| 模块二: 业务协同 | 需求自动拆解为子任务,分派至业务模块 | `task` 任务分派引擎(状态机+重试) |
| 模块二: 问题追溯 | 流程与沟通记录可追溯 | 日志系统 + 任务追溯时间线 |
| 模块三: 开票支持 | 根据需求与税务规则选择签约主体 | `invoice` 税务规则引擎 |
| 模块三: 信息同步 | 维修/采购状态实时同步业务系统 | `sync` 事件总线(维修完成→客房不可用) |
| 定价体系 | 客房/会场对外报价与智能报价 | `pricing` 最小粒度定价引擎 + `rooms/venues/quote` |
| 系统架构 | 网络服务器 + 客户端 | `rpc` JSON 行协议传输层 + `server/client` 双模式 |
| 多端适配 | 客户端自适应各平台 | `common/http/rpc` 平台抽象, Windows/Linux/macOS 均可编译运行 |
| 微信小程序 | 客房/会场报价、在线预订、微信支付 | `miniprogram/` 6 页面 + 服务端 REST API + 支付接口 |

> AI 推理/自然语言生成部分(C 不擅长)不在本实现范围,生产环境建议由 Python/外部
> LLM 服务提供,通过 C 侧 HTTP 接口封装对接。

## 构建

### Windows

```bat
build.bat            :: 自动选择 gcc(MinGW-w64)或 cl(MSVC)
:: 或
cmake -B build && cmake --build build --config Release
```

### Linux / macOS

```sh
./build.sh          # 自动编译 src/*.c 并链接, 依赖 pthread
# 或
make                # 或 cmake -B build && cmake --build build
```

要求: 支持 C11 的编译器,Windows 需链接 ws2_32(已写入构建脚本)。

### 工具包统一安装(可选, 推荐)

开发环境工具(MinGW 编译器 / Node.js 等)统一安装到 `D:\workbuddytool`:

```bat
install_tools.bat              :: 全量安装(MinGW + Node.js LTS + 环境变量)
install_tools.bat --no-node    :: 跳过 Node.js 下载
```

安装完成后新开命令行即生效(gcc/node 进入用户 PATH),也可随时执行
`call D:\workbuddytool\bin\tools_env.bat` 加载工具环境。
微信开发者工具体积较大,请按脚本提示手动安装。

## 使用

```text
crawl      启动多线程爬虫,采集并清洗客户线索 -> data/leads.json
leads      查看结构化客户线索清单(含评分与建议)
score      按成交概率对线索重新评分排序
dispatch   需求自动拆解为子任务,分派至各业务模块执行
tasks      查看任务与追溯说明
sync       演示状态同步(维修完成 -> 客房自动置为不可用)
invoice    演示开票支持(税务规则引擎推荐签约主体)
rooms      查看客房对外报价体系
venues     查看会场对外报价体系
quote      智能组合报价演示
dashboard  启动实时数据看板(http://localhost:8080)
help / quit
```

### 网络服务器 + 客户端架构

系统支持单机 CLI 与「网络服务器 + 客户端」两种运行方式:

```text
# 方式一: 启动服务端(默认端口 9000, 同时后台提供 HTTP 看板 8080)
longshun server [port]

# 方式二: 客户端连接服务端执行任意命令
longshun client [-h 主机] [-p 端口] <命令> [参数...]

# 示例
longshun client ping                       # 健康检查
longshun client rooms 商务楼               # 查询商务楼客房报价
longshun client venues 四层                # 查询四层会场报价
longshun client -h 192.168.1.5 -p 9000 quote   # 远程智能报价
longshun client crawl / leads / score / invoice ...
```

通信协议为单行 JSON:请求 `{"cmd":"rooms","args":["商务楼"]}`,
响应 `{"code":0,"msg":"...","data":{...}}`。客户端与服务端自动统一
中文字符编码(GBK/UTF-8)。

### HTTP REST API(小程序 / 看板共用)

服务端启动后,HTTP 看板服务(默认 `8080`)同时提供以下 REST 接口:

| 接口 | 方法 | 说明 |
|---|---|---|
| `/api/dashboard` | GET | 实时汇总(订单/房态/利润/线索/定价) |
| `/api/rooms/pricing` | GET | 客房报价数组(24 档) |
| `/api/venues/pricing` | GET | 会场报价数组(23 个) |
| `/api/bookings` | GET | 预订清单 |
| `/api/booking` | POST | 创建预订 `{customer,phone,type,room_type/venue,date,amount,remark}` |
| `/api/pay/create` | POST | 创建支付单,返回 `wx.requestPayment` 参数 `{order_id,amount,subject}` |
| `/api/pay/mock` | POST | 模拟支付成功(开发调试) `{order_id}` |
| `/api/pay/notify` | POST | 微信支付结果回调(模拟) `{order_id,result}` |
| `/api/pay/status?order_id=` | GET | 查询支付状态 |
| `/api/pay/orders` | GET | 支付单清单 |
| `/api/query?q=...` | GET | 自然语言查询 |
| `/api/health` | GET | 健康检查 |

## 微信小程序(miniprogram/)

面向客户的展示与预订端,目录结构:

```
miniprogram/
  app.js / app.json / app.wxss / sitemap.json / project.config.json
  utils/api.js          服务端 REST API 封装(wx.request)
  pages/index/          首页(品牌 + 功能入口)
  pages/rooms/          客房报价(楼栋分组/搜索/预订跳转)
  pages/venues/         会场报价(楼层分组/容量/设备/规则)
  pages/booking/        在线预订表单(客房/会场)
  pages/pay/            订单支付(模拟支付 + wx.requestPayment 双模式)
  pages/orders/         我的订单(预订记录/支付状态)
```

使用步骤:

1. 启动服务端:`longshun server 9000`(HTTP 看板在 8080)。
2. 用微信开发者工具「导入项目」选择 `miniprogram/` 目录
   (`appid` 默认游客模式 `touristappid`,发布时改为真实 AppID)。
3. 开发调试:在开发者工具「详情-本地设置」勾选
   「不校验合法域名…」,并将 `app.js` 中 `apiBase` 指向服务端地址
   (默认 `http://127.0.0.1:8080`)。
4. 生产环境:将 `apiBase` 改为已备案 HTTPS 域名,由网关反向代理到
   服务端 8080 端口(小程序要求 HTTPS + 合法域名)。

### 微信支付

- 开发阶段默认 **模拟支付**:`/api/pay/mock` 直接标记支付成功,
  便于完整跑通「预订 → 下单 → 支付 → 状态查询」流程。
- 真实微信支付:填写 `data/wxpay_config.json`
  (`appid/mchid/api_key/notify_url`, `mode` 改为 `real`),
  小程序支付页将调用 `wx.requestPayment` 拉起收银台;
  服务端 `/api/pay/notify` 为支付结果回调入口(生产环境需 HTTPS 网关)。
- 支付单与预订数据持久化在 `data/payments.json` / `data/bookings.json`。

## 数据文件(data/)

| 文件 | 说明 |
|---|---|
| `seeds.json` | 信息源清单(URL/名称/类型/频率),对应需求方案实施待办"信息源清单" |
| `demo_pages/*` | 模拟招标网页与 JSON 信息源(离线演示用) |
| `leads.json` | 爬虫输出的结构化客户线索 |
| `orders.json` / `rooms.json` | 订单与房态业务数据(看板指标) |
| `room_maintenance.json` | 维修单(状态同步演示) |
| `company_licenses.json` | 营业执照主体(税务规则引擎) |
| `inventory.json` | 库存(采购到货同步) |
| `room_pricing.json` | 客房对外报价(楼栋/房型/分类/客户类型,最小粒度) |
| `venue_pricing.json` | 会场报价(23 个会场: 容量/面积/布局/设备/规则) |
| `time_rules.json` | 时间计费规则(半天 4 小时、晚间加收 40% 等) |
| `bookings.json` | 预订订单(小程序在线预订落盘, 首次写入自动创建) |
| `payments.json` | 支付单与支付状态(支付流水, 首次写入自动创建) |
| `wxpay_config.json` | 微信支付配置(appid/mchid/api_key/notify_url, 默认 mock) |
| `logs/` | 分级日志(问题追溯) |

## 模块结构

```
include/  src/
  common.*   平台抽象、线程、字符串工具
  log.*      分级日志(控制台+文件,线程安全)
  json.*     零依赖 JSON 解析/序列化
  http.*     原生 socket HTTP/1.1 客户端(超时/重定向/chunked)
  storage.*  JSON 文件持久化
  crawler.*  多线程爬虫(抓取->清洗->实体提取->去重->落盘)
  lead.*     线索模型、业态识别、商机评分、沟通建议
  task.*     任务拆解、状态机、优先级队列、执行引擎、追溯
  dashboard.* HTTP 看板服务(汇总/明细/自然语言查询)
  sync.*     状态同步事件总线(维修->房态,采购->库存)
  invoice.*  税务规则引擎(开票主体推荐与风险提示)
  pricing.*  客房/会场定价引擎(加载/查询/费用计算/序列化)
  rpc.*      网络 RPC 传输层(JSON 行协议, 服务器 + 客户端)
  main.*     CLI 集成 + 命令处理器 + server/client 模式
  dashboard.* 同时提供预订/支付 REST API(POST 请求体 + 持久化)

miniprogram/         微信小程序(客房/会场报价、预订、支付)
install_tools.bat    工具包统一安装脚本 -> D:\workbuddytool
build.sh             Linux/macOS 构建脚本
```

## 说明

- 零外部依赖,HTTP 基于原生 socket;HTTPS 需接入 TLS 层(代码已留扩展位)。
- 默认 `crawl` 以 demo 模式(本地模拟页面)运行,便于离线演示完整流程;
  将 `main.c` 中 `cfg.demo_mode` 改为 0 即可对 `seeds.json` 中的真实 HTTP 信息源抓取。
- 看板 `dashboard` 为阻塞服务,可用浏览器访问或另开终端 `curl` 调用。

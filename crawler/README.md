# crawler — 每日招标 / 会议承办客户机会采集

投入最小、见效最快、完全合规的获客模块。政府/国企/大型企业的会议、住宿、培训、接待类采购几乎全部公开招标，是精准获客金矿。

## 数据流

```
sources.yaml（可配置信息源 + 关键词）
  → fetcher（UA 伪装 / 重试退避 / 编码修正）
  → parser（ccgp 已内置；新增站点用 generic + CSS 选择器配置）
  → matcher（命中业务关键词即相关；命中排除词丢弃）
  → storage（去重 + leads.jsonl / leads_YYYYMMDD.json / leads.csv）
  → 线索卡片 → lead-hunter Skill → 小程序「线索」页 + 企微推送
```

## 运行

```bash
pip install -r requirements.txt

python run_daily.py --test          # 离线验证解析器（不发网络请求）
python run_daily.py                 # 真实运行（需联网）
python run_daily.py --source 中国政府采购网 --limit 4
```

## 新增一个信息源（员工维护，免改代码）

在 `config/sources.yaml` 的 `sources` 下加一段：

```yaml
  - name: XX市公共资源交易中心
    type: generic
    enabled: true
    urls:
      - https://xxx.gov.cn/jyxx/ggzypzfcg/
    list_selector: "ul.list li"      # 列表项选择器
    title_selector: "a"              # 标题/链接选择器
    date_selector: "span.time"       # 发布日期（可选）
```

复杂站点（如中国政府采购网）写专用解析器放在 `src/parsers.py`，并在 `PARSERS` 注册。

## 输出示例（leads.csv）

| source | title | url | publish_date | buyer | relevance | matched_keywords |
|---|---|---|---|---|---|---|
| 中国政府采购网 | 某市政府会议服务采购项目公开招标公告 | http://www.ccgp.gov.cn/... | 2026-08-09 | 某市机关事务管理局 | 0.6 | 会议服务 |

## 注意事项

- 反爬：fetcher 已带 UA 伪装 + 重试退避；生产建议单源间隔 ≥3 秒、每日总量可控。
- 不碰需登录的付费招标网（采招网/千里马），合规与成本都不划算；公共资源交易中心的免费公开板块足够覆盖。
- 线索只做「提示」，是否跟进由销售决定；AI 不自动外呼、不自动发函。

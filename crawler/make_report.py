"""把 output/ 下的线索渲染成浏览器看板 report.html。

用法：
  python make_report.py
"""
import glob
import json
import os
from collections import Counter
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "output")


def load_leads():
    leads = []
    jsonl = os.path.join(OUT, "leads.jsonl")
    if os.path.exists(jsonl):
        with open(jsonl, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        leads.append(json.loads(line))
                    except Exception:
                        pass
    return leads


def fmt(v, default="-"):
    return v if v not in (None, "", []) else default


def escape(s):
    return (
        str(s)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def card(l):
    kw = l.get("matched_keywords") or []
    if isinstance(kw, str):
        kw = [x for x in kw.split(",") if x]
    kw_html = "".join(
        f'<span class="kw">{escape(k)}</span>' for k in kw
    )
    rel = l.get("relevance", 0) or 0
    rel_cls = "rel-high" if rel >= 1.0 else ("rel-mid" if rel >= 0.6 else "rel-low")
    return f"""
    <div class="card">
      <div class="card-top">
        <span class="src">{escape(l.get('source', '-'))}</span>
        <span class="rel {rel_cls}">相关度 {rel}</span>
      </div>
      <a class="title" href="{escape(l.get('url', '#'))}" target="_blank" rel="noopener">
        {escape(l.get('title', '(无标题)'))}
      </a>
      <div class="meta">
        <span>📅 {escape(fmt(l.get('publish_date')))}</span>
        <span>🏢 {escape(fmt(l.get('buyer')))}</span>
        <span>📍 {escape(fmt(l.get('region')))}</span>
        <span>💰 {escape(fmt(l.get('budget')))}</span>
      </div>
      <div class="kw-row">{kw_html or '<span class="kw kw-empty">未标注关键词</span>'}</div>
      <p class="summary">{escape(fmt(l.get('summary')))}</p>
    </div>
    """


def main():
    leads = load_leads()
    leads.sort(key=lambda x: x.get("fetched_at", ""), reverse=True)

    total = len(leads)
    today = datetime.now().strftime("%Y-%m-%d")
    today_cnt = sum(
        1 for l in leads if (l.get("fetched_at") or "").startswith(today)
    )
    src_cnt = Counter(l.get("source", "-") for l in leads)
    src_bars = "".join(
        f'<div class="bar-row"><span class="bar-label">{escape(k)}</span>'
        f'<span class="bar-val">{v}</span></div>'
        for k, v in src_cnt.most_common()
    )

    gen = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cards = "".join(card(l) for l in leads) or '<p class="empty">暂无线索，先运行 run_daily.py 采集。</p>'

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>每日客户机会看板 · lszy_yx</title>
<style>
  * {{ box-sizing: border-box; }}
  body {{ margin:0; font-family: -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif;
         background:#f5f7fa; color:#1f2933; }}
  header {{ background:linear-gradient(135deg,#0e7c66,#10a37f); color:#fff; padding:28px 32px; }}
  header h1 {{ margin:0; font-size:22px; }}
  header p {{ margin:6px 0 0; opacity:.85; font-size:13px; }}
  .stats {{ display:flex; gap:16px; padding:20px 32px 0; flex-wrap:wrap; }}
  .stat {{ background:#fff; border-radius:12px; padding:14px 20px; min-width:120px;
           box-shadow:0 1px 3px rgba(0,0,0,.08); }}
  .stat .num {{ font-size:26px; font-weight:700; color:#0e7c66; }}
  .stat .lbl {{ font-size:12px; color:#667; margin-top:2px; }}
  .body {{ display:flex; gap:24px; padding:24px 32px; align-items:flex-start; }}
  .list {{ flex:1; display:grid; grid-template-columns:repeat(auto-fill,minmax(320px,1fr)); gap:16px; }}
  .side {{ width:240px; background:#fff; border-radius:12px; padding:18px;
           box-shadow:0 1px 3px rgba(0,0,0,.08); }}
  .side h3 {{ margin:0 0 12px; font-size:14px; color:#334; }}
  .bar-row {{ display:flex; justify-content:space-between; font-size:13px; padding:5px 0;
              border-bottom:1px dashed #eee; }}
  .card {{ background:#fff; border-radius:12px; padding:16px; box-shadow:0 1px 3px rgba(0,0,0,.08); }}
  .card-top {{ display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; }}
  .src {{ font-size:12px; color:#0e7c66; background:#e6f7f1; padding:2px 8px; border-radius:6px; }}
  .rel {{ font-size:12px; padding:2px 8px; border-radius:6px; }}
  .rel-high {{ background:#e6f4ea; color:#1e7e34; }}
  .rel-mid {{ background:#fff4e5; color:#b26a00; }}
  .rel-low {{ background:#f1f3f5; color:#868e96; }}
  .title {{ display:block; font-size:15px; font-weight:600; color:#1f2933; text-decoration:none;
            line-height:1.4; margin:4px 0 8px; }}
  .title:hover {{ color:#0e7c66; }}
  .meta {{ display:flex; flex-wrap:wrap; gap:10px; font-size:12px; color:#667; margin-bottom:8px; }}
  .kw-row {{ margin-bottom:8px; }}
  .kw {{ font-size:11px; background:#eef2ff; color:#3b4cca; padding:2px 8px; border-radius:10px; margin-right:6px; }}
  .kw-empty {{ background:#f1f3f5; color:#adb5bd; }}
  .summary {{ font-size:13px; color:#495057; line-height:1.5; margin:0; }}
  .empty {{ color:#868e96; }}
  footer {{ text-align:center; color:#adb5bd; font-size:12px; padding:20px; }}
</style>
</head>
<body>
  <header>
    <h1>🦀 每日客户机会看板</h1>
    <p>lszy_yx · 招标 / 会议承办 / 培训场地 类客户线索自动采集 · 生成于 {gen}</p>
  </header>
  <div class="stats">
    <div class="stat"><div class="num">{total}</div><div class="lbl">累计线索</div></div>
    <div class="stat"><div class="num">{today_cnt}</div><div class="lbl">今日新增</div></div>
    <div class="stat"><div class="num">{len(src_cnt)}</div><div class="lbl">数据来源</div></div>
  </div>
  <div class="body">
    <div class="list">{cards}</div>
    <div class="side">
      <h3>来源分布</h3>
      {src_bars}
    </div>
  </div>
  <footer>由 crawler 模块自动生成 · run_daily.py 采集 + make_report.py 渲染</footer>
</body>
</html>
"""
    out = os.path.join(OUT, "report.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"[OK] 看板已生成: {out} (线索 {total} 条)")


if __name__ == "__main__":
    main()

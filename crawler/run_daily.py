"""每日招标 / 会议承办客户机会采集入口。

用法：
  python run_daily.py                 # 真实运行（需联网）
  python run_daily.py --test          # 用内置样例离线验证解析器，不发网络请求
  python run_daily.py --source 中国政府采购网 --limit 4
"""
import argparse
import os
import sys
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from src.config_loader import load_config
from src.fetcher import create_session, fetch
from src.parsers import get_parser
from src.matcher import Matcher
from src.storage import Storage
from src.lead import TZ

HERE = os.path.dirname(os.path.abspath(__file__))


def build_search_urls(source_cfg):
    urls = []
    ptype = source_cfg.get("type")
    if ptype == "ccgp":
        base = source_cfg.get("base_url", "http://search.ccgp.gov.cn/bxsearch")
        params = source_cfg.get("params", {})
        pages = source_cfg.get("pages", 1)
        for kw in source_cfg.get("keywords", []):
            for pg in range(1, pages + 1):
                p = dict(params)
                p["kw"] = kw
                p["page_index"] = pg
                urls.append(("GET", base, p))
    else:
        for u in source_cfg.get("urls", []):
            urls.append(("GET", u, None))
        tmpl = source_cfg.get("search_url_template")
        if tmpl:
            for kw in source_cfg.get("keywords", []):
                urls.append(("GET", tmpl.format(keyword=kw), None))
    return urls


def run_source(session, source_cfg, matcher, storage, args, test_html=None):
    parser = get_parser(source_cfg)
    src_name = source_cfg.get("name", parser.name)
    all_leads = []

    if test_html is not None:
        all_leads.extend(parser.parse(test_html, source_cfg.get("base_url", "")))
    else:
        urls = build_search_urls(source_cfg)
        for i, (_, url, params) in enumerate(urls):
            if args.limit and i >= args.limit:
                break
            try:
                html = fetch(session, url, params=params)
            except Exception as e:  # noqa: BLE001
                print(f"  [WARN] fetch failed {url}: {e}")
                continue
            leads = parser.parse(html, source_cfg.get("base_url", ""))
            all_leads.extend(leads)
            print(f"  fetched {url} -> {len(leads)} items")

    kept, meta = matcher.filter(all_leads)
    for l in kept:
        l.source = src_name
        l.fetched_at = datetime.now(TZ).isoformat(timespec="seconds")
    for title, mk, rel in meta:
        print(f"    ✓ {title}  [相关词:{mk} 分:{rel}]")
    new = storage.save(kept)
    return kept, new


def main():
    ap = argparse.ArgumentParser(description="每日招标/会议承办客户机会采集")
    ap.add_argument(
        "--config",
        default=os.path.join(HERE, "config", "sources.yaml"),
    )
    ap.add_argument("--source", default=None, help="只跑指定来源(name)")
    ap.add_argument("--test", action="store_true", help="离线验证解析器")
    ap.add_argument("--test-file", default=None)
    ap.add_argument("--limit", type=int, default=10)
    args = ap.parse_args()

    cfg = load_config(args.config)
    matcher = Matcher(cfg.get("matcher", {}))
    storage = Storage(cfg.get("storage", {}).get("output_dir", "output"))
    session = create_session()

    sources = [s for s in cfg.get("sources", []) if s.get("enabled", True)]
    if args.source:
        sources = [s for s in sources if s.get("name") == args.source]

    total_new = 0
    for s in sources:
        print(f"[*] 来源: {s.get('name')}")
        test_html = None
        if args.test:
            tf = args.test_file or os.path.join(
                HERE, "tests", f"sample_{s.get('type', 'generic')}.html"
            )
            if not os.path.exists(tf):
                print(f"  [SKIP] 无测试样例 {tf}")
                continue
            with open(tf, "r", encoding="utf-8") as f:
                test_html = f.read()
        _, new = run_source(session, s, matcher, storage, args, test_html=test_html)
        print(f"    本轮新增 {len(new)} 条")
        total_new += len(new)

    print(f"[DONE] 本轮新增有效线索 {total_new} 条")


if __name__ == "__main__":
    main()

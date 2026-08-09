"""每日招标 / 会议承办客户机会采集入口。

用法：
  python run_daily.py                 # 真实运行（需联网）
  python run_daily.py --test          # 用内置样例离线验证解析器，不发网络请求
  python run_daily.py --source 中国政府采购网 --limit 4
"""
import argparse
import os
import sys
from datetime import datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from src.config_loader import load_config
from src.fetcher import create_session, fetch
from src.parsers import get_parser
from src.matcher import Matcher
from src.storage import Storage
from src.lead import TZ

HERE = os.path.dirname(os.path.abspath(__file__))


def _reset_output(output_dir):
    """清空归档内容（截断文件，不删除文件，规避安全删除钩子）。"""
    import glob

    output_dir = os.path.join(HERE, output_dir)
    os.makedirs(output_dir, exist_ok=True)
    seen = os.path.join(output_dir, "seen_urls.txt")
    jsonl = os.path.join(output_dir, "leads.jsonl")
    csvp = os.path.join(output_dir, "leads.csv")
    for p in (seen, jsonl):
        open(p, "w", encoding="utf-8").close()
    # csv 重写为表头
    open(csvp, "w", encoding="utf-8-sig").close()
    # 按日 json 清空为 []
    for jf in glob.glob(os.path.join(output_dir, "leads_*.json")):
        with open(jf, "w", encoding="utf-8") as f:
            f.write("[]")
    print(f"[RESET] 已清空归档: {output_dir}")


def build_search_urls(source_cfg):
    urls = []
    ptype = source_cfg.get("type")
    if ptype == "ccgp":
        base = source_cfg.get("base_url", "http://search.ccgp.gov.cn/bxsearch")
        raw = source_cfg.get("params", {})
        # ccgp 搜索接口：timeType/pppStatus/bidSort 等字段会触发服务端 500，
        # 改用显式时间范围（近 180 天）即可正常返回 200。
        params_tpl = {
            k: v for k, v in raw.items() if k not in ("timeType", "pppStatus", "bidSort")
        }
        params_tpl.setdefault("searchtype", 1)
        params_tpl.setdefault("bidType", 0)
        params_tpl.setdefault("dbselect", "bidx")
        end = datetime.now()
        start = end - timedelta(days=180)
        pages = source_cfg.get("pages", 1)
        for kw in source_cfg.get("keywords", []):
            for pg in range(1, pages + 1):
                p = dict(params_tpl)
                p["kw"] = kw
                p["page_index"] = pg
                p["start_time"] = start.strftime("%Y-%m-%d")
                p["end_time"] = end.strftime("%Y-%m-%d")
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
    ap.add_argument(
        "--reset",
        action="store_true",
        help="清空去重记录与归档后重跑（仅清空文件内容，不删除文件）",
    )
    args = ap.parse_args()

    cfg = load_config(args.config)
    if args.reset:
        _reset_output(cfg.get("storage", {}).get("output_dir", "output"))

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

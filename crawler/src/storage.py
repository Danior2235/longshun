"""存储与去重：JSONL 全量 + 当日 JSON + CSV(Excel 可直接打开) + seen 集合。"""
import os
import csv
import json
from datetime import datetime
from .lead import TZ


class Storage:
    def __init__(self, output_dir):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)
        self.seen_file = os.path.join(output_dir, "seen_urls.txt")
        self.seen = self._load_seen()

    def _load_seen(self):
        if os.path.exists(self.seen_file):
            with open(self.seen_file, "r", encoding="utf-8") as f:
                return set(line.strip() for line in f if line.strip())
        return set()

    def is_new(self, url):
        return url not in self.seen

    def save(self, leads):
        new_leads = [l for l in leads if self.is_new(l.url)]
        ts = datetime.now(TZ).strftime("%Y%m%d")
        run_file = os.path.join(self.output_dir, f"leads_{ts}.json")

        existing = []
        if os.path.exists(run_file):
            try:
                with open(run_file, "r", encoding="utf-8") as f:
                    existing = json.load(f)
            except Exception:
                existing = []
        all_today = existing + [l.to_dict() for l in new_leads]
        with open(run_file, "w", encoding="utf-8") as f:
            json.dump(all_today, f, ensure_ascii=False, indent=2)

        master = os.path.join(self.output_dir, "leads.jsonl")
        with open(master, "a", encoding="utf-8") as f:
            for l in new_leads:
                f.write(json.dumps(l.to_dict(), ensure_ascii=False) + "\n")

        csv_path = os.path.join(self.output_dir, "leads.csv")
        write_header = not os.path.exists(csv_path)
        with open(csv_path, "a", encoding="utf-8-sig", newline="") as f:
            w = csv.DictWriter(
                f,
                fieldnames=[
                    "source", "title", "url", "publish_date", "buyer",
                    "region", "budget", "summary", "relevance",
                    "matched_keywords", "fetched_at",
                ],
            )
            if write_header:
                w.writeheader()
            for l in new_leads:
                d = l.to_dict()
                d["matched_keywords"] = ",".join(d.get("matched_keywords") or [])
                w.writerow(d)

        with open(self.seen_file, "a", encoding="utf-8") as f:
            for l in new_leads:
                f.write(l.url + "\n")
        self.seen.update(l.url for l in new_leads)
        return new_leads

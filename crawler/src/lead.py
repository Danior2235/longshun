"""线索数据模型。"""
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone, timedelta

TZ = timezone(timedelta(hours=8))


@dataclass
class Lead:
    source: str
    title: str
    url: str
    publish_date: str = ""
    buyer: str = ""
    region: str = ""
    budget: str = ""
    summary: str = ""
    relevance: float = 0.0
    matched_keywords: list = field(default_factory=list)
    fetched_at: str = ""

    def to_dict(self):
        return asdict(self)

    def dedupe_key(self):
        return self.url.strip()

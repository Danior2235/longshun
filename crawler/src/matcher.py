"""相关性匹配：命中业务关键词即相关，命中排除词直接丢弃。"""


class Matcher:
    def __init__(self, cfg):
        self.must = cfg.get("must_include_any", [])
        self.exclude = cfg.get("exclude", [])
        self.min_relevance = cfg.get("min_relevance", 0.5)

    def score(self, text):
        t = (text or "").lower()
        matched = [k for k in self.must if k.lower() in t]
        excluded = [k for k in self.exclude if k.lower() in t]
        rel = min(1.0, 0.4 + 0.2 * len(matched)) if matched else 0.0
        return rel, matched, excluded

    def filter(self, leads):
        out = []
        kept_meta = []
        for lead in leads:
            blob = f"{lead.title} {lead.summary} {lead.buyer}"
            rel, matched, excluded = self.score(blob)
            if excluded:
                continue
            if rel < self.min_relevance:
                continue
            lead.relevance = round(rel, 2)
            lead.matched_keywords = matched
            out.append(lead)
            kept_meta.append((lead.title, matched, round(rel, 2)))
        out.sort(key=lambda x: x.relevance, reverse=True)
        return out, kept_meta

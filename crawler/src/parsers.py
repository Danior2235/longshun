"""站点解析器。新增站点优先用 generic + 配置选择器，复杂站点再写专用解析器。"""
from urllib.parse import urljoin
from bs4 import BeautifulSoup
from .lead import Lead


class BaseParser:
    name = "base"

    def parse(self, html, base_url=""):
        raise NotImplementedError


class CCGPParser(BaseParser):
    """中国政府采购网搜索结果解析。"""
    name = "ccgp"

    def parse(self, html, base_url="http://www.ccgp.gov.cn"):
        soup = BeautifulSoup(html, "lxml")
        leads = []
        items = soup.select(".vT-srch-result-list li")
        if not items:
            items = soup.select("ul li")
        for li in items:
            a = li.select_one("a")
            if not a:
                continue
            title = a.get_text(strip=True)
            if not title:
                continue
            href = a.get("href", "")
            url = urljoin(base_url, href) if href else ""
            date_span = li.select_one(".vT-srch-result-list-date")
            date = date_span.get_text(strip=True) if date_span else ""
            buyer_span = li.select_one(".vT-srch-result-list-title-sub")
            buyer = (
                buyer_span.get_text(strip=True).replace("采购人：", "").strip()
                if buyer_span
                else ""
            )
            leads.append(
                Lead(
                    source="中国政府采购网",
                    title=title,
                    url=url,
                    publish_date=date,
                    buyer=buyer,
                )
            )
        return leads


class GenericParser(BaseParser):
    """通用列表解析器：选择器全部来自 sources.yaml 配置。"""
    name = "generic"

    def __init__(self, cfg):
        self.list_sel = cfg.get("list_selector", "ul li")
        self.title_sel = cfg.get("title_selector", "a")
        self.date_sel = cfg.get("date_selector", "")
        self.buyer_sel = cfg.get("buyer_selector", "")
        self.base_url = cfg.get("base_url", "")

    def parse(self, html, base_url=None):
        soup = BeautifulSoup(html, "lxml")
        base = base_url or self.base_url
        leads = []
        for li in soup.select(self.list_sel):
            a = li.select_one(self.title_sel)
            if not a:
                continue
            title = a.get_text(strip=True)
            if not title:
                continue
            href = a.get("href", "")
            url = urljoin(base, href) if href else ""
            date = ""
            if self.date_sel:
                d = li.select_one(self.date_sel)
                date = d.get_text(strip=True) if d else ""
            buyer = ""
            if self.buyer_sel:
                b = li.select_one(self.buyer_sel)
                buyer = b.get_text(strip=True) if b else ""
            leads.append(
                Lead(source="generic", title=title, url=url,
                     publish_date=date, buyer=buyer)
            )
        return leads


PARSERS = {"ccgp": CCGPParser, "generic": GenericParser}


def get_parser(source_cfg):
    ptype = source_cfg.get("type", "generic")
    cls = PARSERS.get(ptype, GenericParser)
    return cls(source_cfg) if ptype == "generic" else cls()

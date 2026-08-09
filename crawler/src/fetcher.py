"""HTTP 抓取：带 UA 伪装、重试退避、编码修正。"""
import time
import requests

DEFAULT_HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
    ),
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
    "Connection": "keep-alive",
}


def create_session(headers=None):
    s = requests.Session()
    s.headers.update(headers or DEFAULT_HEADERS)
    return s


def fetch(session, url, timeout=15, retries=3, backoff=2, params=None):
    last_err = None
    for i in range(retries):
        try:
            r = session.get(url, params=params, timeout=timeout)
            r.raise_for_status()
            if not r.encoding or r.encoding.lower() == "iso-8859-1":
                r.encoding = r.apparent_encoding or "utf-8"
            return r.text
        except Exception as e:  # noqa: BLE001
            last_err = e
            time.sleep(backoff * (i + 1))
    raise RuntimeError(f"fetch failed after {retries} retries: {url} -> {last_err}")

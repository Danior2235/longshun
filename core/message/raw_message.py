"""统一消息模型 raw_message —— 把 5 种来源归一到同一结构，下游 Agent 只认这一种。

来源（source）：
  wechat_text           小程序内粘贴的微信对话文本
  wechat_screenshot_ocr 微信截图 OCR 后的结构化气泡
  phone_recording_asr   电话录音 ASR 转写（带说话人分离）
  crawler_lead          每日招标爬虫抓到的客户机会
  pms_event             PMS / 业务系统事件（房态变更、订单、维修等）

说话人（speaker_role）：
  sales     我方销售
  customer  客户
  system    系统 / 线索 / 事件（非对话）
  unknown   暂未识别
"""
from dataclasses import dataclass, field, asdict
from datetime import datetime
from typing import Any, Dict, List, Optional

SOURCE_WECHAT_TEXT = "wechat_text"
SOURCE_WECHAT_OCR = "wechat_screenshot_ocr"
SOURCE_PHONE_ASR = "phone_recording_asr"
SOURCE_CRAWLER_LEAD = "crawler_lead"
SOURCE_PMS_EVENT = "pms_event"

SPEAKER_SALE = "sales"
SPEAKER_CUSTOMER = "customer"
SPEAKER_SYSTEM = "system"
SPEAKER_UNKNOWN = "unknown"


@dataclass
class RawMessage:
    source: str
    speaker_role: str
    content: str
    sent_at: str                       # ISO-8601
    session_id: str = ""              # 一次会话 / 一个客户的连续上下文
    customer_id: str = ""
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


# ---------- 归一化工厂 ----------

def from_crawler_lead(lead: Dict[str, Any], session_id: str = "") -> RawMessage:
    """爬虫线索 → 系统消息（非对话，作为客户背景注入）。"""
    summary = (
        f"【新客户机会】{lead.get('title', '')}\n"
        f"采购人：{lead.get('buyer', '-')}｜地区：{lead.get('region', '-')}｜"
        f"预算：{lead.get('budget', '-')}｜相关度：{lead.get('relevance', '-')}\n"
        f"关键词：{', '.join(lead.get('matched_keywords') or [])}"
    )
    return RawMessage(
        source=SOURCE_CRAWLER_LEAD,
        speaker_role=SPEAKER_SYSTEM,
        content=summary,
        sent_at=lead.get("fetched_at") or datetime.now().isoformat(timespec="seconds"),
        session_id=session_id,
        extra={"lead": lead},
    )


def from_wechat_ocr(blocks: List[Dict[str, Any]], session_id: str,
                    customer_id: str = "", sent_at: Optional[str] = None) -> List[RawMessage]:
    """微信截图 OCR 后的气泡 → 多条消息。
    blocks: [{"speaker": "sales"|"customer", "text": "..."}, ...]
    """
    out = []
    for b in blocks:
        sp = b.get("speaker", SPEAKER_UNKNOWN)
        if sp not in (SPEAKER_SALE, SPEAKER_CUSTOMER):
            sp = SPEAKER_UNKNOWN
        out.append(RawMessage(
            source=SOURCE_WECHAT_OCR,
            speaker_role=sp,
            content=b.get("text", ""),
            sent_at=sent_at or datetime.now().isoformat(timespec="seconds"),
            session_id=session_id,
            customer_id=customer_id,
        ))
    return out


def from_wechat_text(text: str, session_id: str, customer_id: str = "",
                     sent_at: Optional[str] = None) -> List[RawMessage]:
    """小程序内粘贴的微信对话文本 → 按行切分（'客户：'/'我：' 前缀识别说话人）。"""
    out = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line[:2].lower().startswith(("客户", "客人", "c:")):
            sp, content = SPEAKER_CUSTOMER, line.split("：", 1)[-1].split(":", 1)[-1]
        elif line[:2].lower().startswith(("我：", "销售", "s:")):
            sp, content = SPEAKER_SALE, line.split("：", 1)[-1].split(":", 1)[-1]
        else:
            sp, content = SPEAKER_UNKNOWN, line
        out.append(RawMessage(
            source=SOURCE_WECHAT_TEXT,
            speaker_role=sp,
            content=content.strip(),
            sent_at=sent_at or datetime.now().isoformat(timespec="seconds"),
            session_id=session_id,
            customer_id=customer_id,
        ))
    return out


def from_phone_asr(segments: List[Dict[str, Any]], session_id: str,
                   customer_id: str = "", sent_at: Optional[str] = None) -> List[RawMessage]:
    """电话录音 ASR 转写（带说话人分离）→ 多条消息。
    segments: [{"speaker_label": "0"/"1", "text": "...", "start": 0.0}, ...]
    标签 0/1 映射到 sales/customer（默认 0=销售 1=客户，可配）。
    """
    label_map = {0: SPEAKER_SALE, 1: SPEAKER_CUSTOMER,
                 "0": SPEAKER_SALE, "1": SPEAKER_CUSTOMER}
    out = []
    for seg in segments:
        lbl = seg.get("speaker_label", SPEAKER_UNKNOWN)
        sp = label_map.get(lbl, SPEAKER_UNKNOWN)
        out.append(RawMessage(
            source=SOURCE_PHONE_ASR,
            speaker_role=sp,
            content=seg.get("text", ""),
            sent_at=sent_at or datetime.now().isoformat(timespec="seconds"),
            session_id=session_id,
            customer_id=customer_id,
            extra={"start": seg.get("start"), "end": seg.get("end")},
        ))
    return out


def from_pms_event(event: Dict[str, Any], session_id: str = "") -> RawMessage:
    """PMS / 业务系统事件 → 系统消息（房态变更、订单、维修等）。"""
    return RawMessage(
        source=SOURCE_PMS_EVENT,
        speaker_role=SPEAKER_SYSTEM,
        content=event.get("description", str(event)),
        sent_at=event.get("occurred_at") or datetime.now().isoformat(timespec="seconds"),
        session_id=session_id,
        customer_id=event.get("customer_id", ""),
        extra={"event": event},
    )

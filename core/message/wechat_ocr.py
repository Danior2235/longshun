"""微信对话导入层。

提供三类能力：
  1. classify_bubbles()   —— 截图 OCR 后的带坐标气泡 → 判断说话人（按微信绿 #95EC69 + 气泡 x 坐标）。
                             纯函数，不依赖任何外部服务，Demo 可直接跑。
  2. WeChatOCRClient      —— 腾讯云 OCR 封装，把截图图片转成带坐标文本块。需 TENCENT key。
  3. WeComArchiveClient   —— 企业微信会话存档封装（seq 游标增量 + RSA/AES 解密）。需开通企微 + 配置密钥。

对外统一产出 raw_message（经 from_wechat_ocr / from_wechat_text），下游 Agent 无感知来源差异。
"""
from __future__ import annotations

import base64
from typing import Any, Dict, List, Optional

from core.message.raw_message import (
    SPEAKER_SALE, SPEAKER_CUSTOMER, SPEAKER_UNKNOWN,
    from_wechat_ocr, from_wechat_text,
)
from core.config import Settings

# 微信自己发出的气泡背景色 #95EC69
WECHAT_GREEN = (149, 236, 105)


def _color_near(a, b, tol: int = 28) -> bool:
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def classify_bubbles(ocr_blocks: List[Dict[str, Any]], image_width: Optional[int] = None,
                     my_name: str = "我", customer_name: str = "客户") -> List[Dict[str, Any]]:
    """OCR 文本块 → wechat_ocr 格式 blocks（带说话人标注）。

    ocr_blocks 元素:
      {"text": str, "bbox": [x1,y1,x2,y2] | None,
       "bg_color": [r,g,b] | None,   # 气泡背景色采样，微信绿表示自己
       "confidence": float | None}

    判断优先级：
      1) 文本显式前缀（"我："/"客户："等）→ 用前缀
      2) bg_color 接近微信绿 → sales（自己）
      3) 气泡中心 x < 图像宽度 60% → customer（对方在左），否则 sales
    """
    if image_width is None and ocr_blocks:
        xs = [b["bbox"][2] for b in ocr_blocks if b.get("bbox")]
        image_width = max(xs) if xs else 1000

    out: List[Dict[str, Any]] = []
    for b in ocr_blocks:
        text = (b.get("text") or "").strip()
        if not text:
            continue
        sp: Optional[str] = None
        # 1) 显式前缀
        head = text[:3].lower()
        if head.startswith(("我", "sale", "s:")):
            sp = SPEAKER_SALE
        elif head.startswith(("客户", "客人", "c:")):
            sp = SPEAKER_CUSTOMER
        # 2) 气泡背景色
        if sp is None and b.get("bg_color"):
            if _color_near(b["bg_color"], WECHAT_GREEN):
                sp = SPEAKER_SALE
        # 3) 气泡 x 坐标（单聊：对方在左、自己在右）
        if sp is None and b.get("bbox"):
            cx = (b["bbox"][0] + b["bbox"][2]) / 2
            sp = SPEAKER_CUSTOMER if cx < (image_width or 1000) * 0.6 else SPEAKER_SALE
        if sp is None:
            sp = SPEAKER_UNKNOWN
        out.append({"speaker": sp, "text": text,
                    "bg_color": b.get("bg_color"), "bbox": b.get("bbox")})
    return out


class WeChatOCRClient:
    """腾讯云 OCR（通用印刷体）封装。把微信截图转成带坐标文本块。

    真实依赖：pip install tencentcloud-sdk-python-ocr + TENCENT_SECRET_ID/KEY。
    无 key 时不调用，可直接把第三方 OCR 的 blocks 喂给 classify_bubbles。
    """

    def __init__(self, settings: Settings):
        self.s = settings
        self._client = None
        self._models = None

    def _ensure_client(self):
        if self._client is None:
            try:
                from tencentcloud.common import credential
                from tencentcloud.ocr.v20181119 import ocr_client, models
            except ImportError as e:
                raise RuntimeError(
                    "未安装腾讯云 OCR SDK，请 pip install tencentcloud-sdk-python-ocr") from e
            if not (self.s.tencent_secret_id and self.s.tencent_secret_key):
                raise RuntimeError(
                    "缺少 TENCENT_SECRET_ID/KEY，无法调用 OCR；可用 classify_bubbles 直接喂 OCR blocks")
            cred = credential.Credential(self.s.tencent_secret_id, self.s.tencent_secret_key)
            self._client = ocr_client.OcrClient(cred, "ap-guangzhou")
            self._models = models
        return self._client

    def image_to_blocks(self, image_path: str) -> List[Dict[str, Any]]:
        """截图 → 带坐标文本块（供 classify_bubbles 判断说话人）。"""
        client = self._ensure_client()
        with open(image_path, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("ascii")
        req = self._models.GeneralBasicOCRRequest()
        req.ImageBase64 = b64
        resp = client.GeneralBasicOCR(req)
        blocks: List[Dict[str, Any]] = []
        for d in resp.TextDetections:
            poly = d.Polygon
            xs = [p.X for p in poly]
            ys = [p.Y for p in poly]
            bbox = [min(xs), min(ys), max(xs), max(ys)]
            blocks.append({"text": d.DetectedText, "bbox": bbox,
                           "confidence": d.Confidence / 100.0 if d.Confidence else None})
        return blocks


class WeComArchiveClient:
    """企业微信会话存档封装（seq 游标增量 + RSA/AES 解密）。

    真实运行前置（业务决策，非纯技术）：
      1) 企业微信后台开通「会话内容存档」并付费（约 90 元/人/年）；
      2) 配置 CORP_ID / SECRET / RSA 私钥；
      3) 安装 SDK（pyweworkfinance 或对接 libWeWorkFinanceSdk）。
    此处提供接口与本地解密骨架，未配置时返回空（Demo 阶段用截图 OCR / 文本导入顶替）。
    """

    def __init__(self, settings: Settings):
        self.s = settings
        self.last_seq = 0

    def is_configured(self) -> bool:
        return bool(self.s.wework_corp_id and self.s.wework_secret
                    and self.s.wework_rsa_private_key)

    def fetch_chat_records(self, limit: int = 100) -> List[Dict[str, Any]]:
        """拉取 last_seq 之后的增量会话。

        真实实现骨架（需企微 SDK）：
            sdk = WeWorkFinanceSdk(corp_id, secret)
            msgs = sdk.get_chat_data(self.last_seq, limit)
            for m in msgs:
                plaintext = sdk.decrypt_chat_record(m, self.s.wework_rsa_private_key)
                self.last_seq = m.seq
                # plaintext → raw_message（按 msgtype 映射文本/图片/语音）
        """
        if not self.is_configured():
            return []
        raise NotImplementedError(
            "需对接企业微信会话存档 SDK；Demo 阶段请用截图 OCR / 文本导入")


def import_wechat_text(text: str, session_id: str, customer_id: str = "",
                      sent_at: Optional[str] = None):
    """小程序内粘贴的微信对话文本 → raw_message 列表。"""
    return from_wechat_text(text, session_id, customer_id, sent_at)


def import_wechat_screenshot(image_path: str, settings: Settings, session_id: str,
                             customer_id: str = "", image_width: Optional[int] = None):
    """微信截图 → OCR → 气泡分类 → raw_message 列表。

    无 TENCENT key 时抛出明确提示，引导改用文本导入或配置 key。
    """
    ocr = WeChatOCRClient(settings)
    blocks = ocr.image_to_blocks(image_path)
    classified = classify_bubbles(blocks, image_width)
    return from_wechat_ocr(classified, session_id, customer_id)

"""电话录音 ASR 流水线。

本地上传 → 腾讯云录音文件识别（8k_zh_large + 说话人分离 + 酒店热词表）→ raw_message。

三种获取方式（封装占位，接上即用）：
  • local_upload  手机/本地录音文件直接上传
  • axb           隐私号（AXB 中间号）通话，录音自动落 COS，自带告知音（合规）
  • call_center   云呼叫中心（如腾讯云呼叫中心）回调录音 URL

不依赖 key 时提供 mock_recognize() 用样例验证下游抽取链路（from_phone_asr 规则版）。
"""
from __future__ import annotations

import os
from typing import Any, Dict, List, Optional

from core.message.raw_message import from_phone_asr, SPEAKER_SALE
from core.config import Settings

# 酒店行业热词表：ASR 成败在热词，通用模型听不懂「围餐/含双早/协议价/场地费」
HOTWORDS = [
    "围餐", "含双早", "双早", "协议价", "场地费", "会议服务", "住宿", "宴会", "年会",
    "接机", "茶歇", "标准间", "大床房", "商务间", "会场", "投影", "音响", "led屏",
    "团建", "培训", "展会", "展位", "用餐", "自助餐", "中餐", "西餐", "包厢",
    "发票", "专票", "普票", "履约", "保证金", "中标", "询价", "招投标", "康体",
]


class PhoneASRClient:
    """腾讯云录音文件识别封装。"""

    def __init__(self, settings: Settings):
        self.s = settings
        self._client = None
        self._models = None

    def _ensure_client(self):
        if self._client is None:
            try:
                from tencentcloud.common import credential
                from tencentcloud.asr.v20190614 import asr_client, models
            except ImportError as e:
                raise RuntimeError(
                    "未安装腾讯云 ASR SDK：pip install tencentcloud-sdk-python-asr") from e
            if not (self.s.tencent_secret_id and self.s.tencent_secret_key):
                raise RuntimeError(
                    "缺少 TENCENT_SECRET_ID/KEY，无法调用 ASR；可用 mock_recognize 验证链路")
            cred = credential.Credential(self.s.tencent_secret_id, self.s.tencent_secret_key)
            self._client = asr_client.AsrClient(cred, "ap-guangzhou")
            self._models = models
        return self._client

    def recognize_file(self, audio_url: str, callback_url: Optional[str] = None) -> List[Dict[str, Any]]:
        """录音文件识别（异步）：提交任务 → 轮询 → 解析带说话人分离的转写。

        真实实现骨架（需腾讯云 ASR）：
            task = client.CreateRecTask(
                EngineModelType="8k_zh_large",
                ChannelNum=1, SpeakerDiarization=1, SpeakerNumber=2,
                HotWordList="|".join(HOTWORDS),
                Url=audio_url, CallbackUrl=callback_url)
            # 轮询 DescribeTaskStatus 直到 Status==2(成功)
            # 解析 ResultDetail（含 VoiceId / FinalSentence / StartMs / EndMs）
        """
        client = self._ensure_client()
        _ = (client, self._models, audio_url, callback_url)
        raise NotImplementedError(
            "需对接腾讯云 ASR；Demo 阶段请用 mock_recognize 验证链路")


def mock_recognize() -> List[Dict[str, Any]]:
    """无 key 时的样例转写（带说话人分离），用于验证下游抽取链路。"""
    return [
        {"speaker_label": "0", "text": "王总您好，我是XX酒店小李，想跟您确认下月底会议的事。",
         "start": 0.0, "end": 5.0},
        {"speaker_label": "1", "text": "哦对，我们大概80人，要两个会场，住宿也要安排。",
         "start": 5.0, "end": 11.0},
        {"speaker_label": "0", "text": "好的，会场含LED屏和茶歇，住宿按协议价含双早，预算大概多少？",
         "start": 11.0, "end": 19.0},
        {"speaker_label": "1", "text": "预算十万以内吧，但你们价格得实在点，别比别家贵太多。",
         "start": 19.0, "end": 25.0},
    ]


def ingest_local(audio_path: str, settings: Settings, session_id: str,
                 customer_id: str = "", sent_at: Optional[str] = None):
    """本地上传录音 → ASR → raw_message。

    无 ASR key 时自动回退 mock_recognize，保证 Demo 链路不断。
    """
    client = PhoneASRClient(settings)
    try:
        segs = client.recognize_file(audio_path)
    except NotImplementedError:
        segs = mock_recognize()
    return from_phone_asr(segs, session_id, customer_id, sent_at)


def ingest_axb(call_record_id: str, settings: Settings, session_id: str,
               customer_id: str = "", sent_at: Optional[str] = None):
    """AXB 隐私号通话录音 → COS URL → ASR → raw_message。

    录音 URL 由隐私号中间件（如腾讯云号码保护）回传，自带「本次通话可能被录音」告知音（合规）。
    """
    cos_url = f"cos://lszy-recordings/{call_record_id}.wav"  # 真实环境由中间件提供
    return ingest_local(cos_url, settings, session_id, customer_id, sent_at)

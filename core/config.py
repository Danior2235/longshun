"""全局配置：外部 key 均以占位形式提供，接上即用。

读取顺序：环境变量 > 默认值。保持依赖最小，不强制引入 yaml。

接入清单（上线前由技术负责人填）：
  LLM_API_KEY            结构化抽取 / 方案生成所用 LLM 的 key
  TENCENT_SECRET_ID/KEY  腾讯云 ASR（电话录音识别）密钥
  WEWORK_CORP_ID/SECRET  企业微信会话存档（需先开通）
  PMS_TYPE/API_BASE/TOKEN  业务系统对接（西软/Opera/石基等）
  DATA_IN_INTRANET=1     数据不能出内网时置 1，强制走私有化 LLM
"""
import os
from dataclasses import dataclass, field
from typing import List


@dataclass
class Settings:
    # —— LLM（结构化抽取 / 方案生成）——
    llm_provider: str = "openai"            # openai / tencent-hunyuan / local
    llm_base_url: str = "https://api.openai.com/v1"
    llm_api_key: str = ""
    llm_model: str = "gpt-4o-mini"
    # 数据能否出内网：False 时强制私有化部署，不调用公有云 LLM
    data_in_intranet: bool = False

    # —— ASR（电话录音识别）——
    asr_provider: str = "tencent"          # tencent / ali
    tencent_secret_id: str = ""
    tencent_secret_key: str = ""
    asr_hotwords: List[str] = field(default_factory=list)

    # —— 企业微信会话存档 ——
    wework_corp_id: str = ""
    wework_secret: str = ""
    wework_rsa_private_key: str = ""

    # —— 业务系统（PMS / POS / 会议 / 财务）——
    pms_type: str = "unknown"              # 西软 / opera / 石基 / 未知
    pms_api_base: str = ""
    pms_api_token: str = ""

    # —— 爬虫输出 ——
    crawler_output_dir: str = "crawler/output"

    @classmethod
    def load(cls, _path: str = "config/settings.yaml") -> "Settings":
        s = cls()
        s.llm_api_key = os.getenv("LLM_API_KEY", s.llm_api_key)
        s.llm_base_url = os.getenv("LLM_BASE_URL", s.llm_base_url)
        s.llm_model = os.getenv("LLM_MODEL", s.llm_model)
        s.tencent_secret_id = os.getenv("TENCENT_SECRET_ID", s.tencent_secret_id)
        s.tencent_secret_key = os.getenv("TENCENT_SECRET_KEY", s.tencent_secret_key)
        s.wework_corp_id = os.getenv("WEWORK_CORP_ID", s.wework_corp_id)
        s.wework_secret = os.getenv("WEWORK_SECRET", s.wework_secret)
        s.pms_type = os.getenv("PMS_TYPE", s.pms_type)
        s.pms_api_base = os.getenv("PMS_API_BASE", s.pms_api_base)
        s.pms_api_token = os.getenv("PMS_API_TOKEN", s.pms_api_token)
        s.data_in_intranet = os.getenv("DATA_IN_INTRANET", "0") == "1"
        return s

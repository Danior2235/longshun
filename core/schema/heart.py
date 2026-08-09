"""心脏 Schema：从归一化消息中抽取结构化客户洞察。

输出 JSON 同时驱动：客户画像、实时看板、待办、方案生成。
字段定义见 HEART_SCHEMA。抽取策略：
  1) 优先 LLM（配置 llm_api_key 后启用，内网模式强制本地模型）
  2) 无 key / 内网无本地模型 → 规则兜底（关键词/正则），保证 Demo 链路不中断
"""
import json
import re
from typing import Any, Dict, List

from core.config import Settings
from core.message.raw_message import RawMessage

HEART_SCHEMA = {
    "customer": {
        "name": "客户/单位名称",
        "industry": "行业",
        "scale": "规模（人数/预算量级）",
        "region": "地区",
    },
    "needs": [
        {"type": "会议/住宿/餐饮/活动", "detail": "具体需求描述", "quantity": "规模", "date": "期望时间"}
    ],
    "budget": {"amount": "预算区间", "currency": "币种", "flexible": "是否可谈"},
    "objections": [{"point": "异议点", "category": "价格/档期/竞品/信任"}],
    "competitors": [{"name": "竞品酒店/服务商", "advantage": "对方优势"}],
    "commitments": [{"who": "销售/客户", "action": "承诺动作", "due": "时限"}],
    "next_best_action": "建议下一步动作（具体可执行的销售动作）",
    "risk_flags": [{"level": "高/中/低", "reason": "风险原因"}],
    "summary": "一句话客户画像",
}


class RuleExtractor:
    """无 LLM 时的兜底抽取：基于关键词与简单正则，保证 Demo 链路不中断。"""
    _CN = {'零': 0, '一': 1, '二': 2, '两': 2, '三': 3, '四': 4, '五': 5,
           '六': 6, '七': 7, '八': 8, '九': 9}
    DATE_RE = re.compile(r"(\d{1,2})\s*月(\d{1,2})?\s*[号日]?|(\d{4}[-/]\d{1,2}[-/]\d{1,2})")
    NEED_KEYS = {
        "会议": "会议", "会场": "场地", "住宿": "住宿", "餐饮": "餐饮", "用餐": "餐饮",
        "宴会": "宴会", "年会": "年会", "培训": "培训", "接待": "接待", "活动": "活动",
        "场地": "场地", "团建": "活动", "展": "活动", "茶歇": "餐饮", "围餐": "餐饮",
    }
    OBJ_KEYS = {
        "太贵": "价格", "贵": "价格", "价格": "价格", "报价高": "价格", "不便宜": "价格",
        "没档期": "档期", "档期紧": "档期", "满房": "档期", "订不到": "档期",
        "别家": "竞品", "竞品": "竞品", "比不上": "竞品", "同行": "竞品",
        "不放心": "信任", "没合作过": "信任", "再考虑": "信任", "风险": "信任",
    }

    @staticmethod
    def _cn_to_int(s: str) -> int:
        if s.isdigit():
            return int(s)
        if s == "十":
            return 10
        if "十" in s:
            left, _, right = s.partition("十")
            return (RuleExtractor._CN.get(left, 1) if left else 1) * 10 + \
                   (RuleExtractor._CN.get(right, 0) if right else 0)
        return RuleExtractor._CN.get(s, 0)

    def _parse_budget(self, text: str):
        m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*万", text)
        if m:
            return f"{float(m.group(1)):.0f}万"
        m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*千", text)
        if m:
            return f"{float(m.group(1)):.0f}千"
        m = re.search(r"([零一二两三四五六七八九十]+)\s*万", text)
        if m:
            return f"{self._cn_to_int(m.group(1))}万"
        m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*元", text)
        if m:
            return f"{float(m.group(1)):.0f}元"
        return None

    def extract(self, messages: List[RawMessage]) -> Dict[str, Any]:
        text = "\n".join(m.content for m in messages)
        needs, seen = [], set()
        for k, label in self.NEED_KEYS.items():
            if k in text and label not in seen:
                seen.add(label)
                needs.append({"type": label, "detail": "", "quantity": "", "date": ""})
        objections, oseen = [], set()
        for k, cat in self.OBJ_KEYS.items():
            if k in text and cat not in oseen:
                oseen.add(cat)
                objections.append({"point": k, "category": cat})
        budget_amt = self._parse_budget(text)
        budget = {"amount": budget_amt or "", "currency": "CNY", "flexible": "约" if budget_amt else ""}
        dates = self.DATE_RE.findall(text)
        if dates and needs:
            needs[0]["date"] = str(dates[0])
        summary = "规则抽取（无LLM）" + (
            f"：识别到需求-{','.join(n['type'] for n in needs)}"
            + (f"、预算{budget_amt}" if budget_amt else "")
            + (f"、异议-{','.join(o['category'] for o in objections)}" if objections else ""))
        return {
            "customer": {"name": "", "industry": "", "scale": "", "region": ""},
            "needs": needs,
            "budget": budget,
            "objections": objections,
            "competitors": [],
            "commitments": [],
            "next_best_action": ("结合预算给出报价初稿，并针对异议准备话术"
                                 if (needs or budget_amt) else "进一步沟通明确需求"),
            "risk_flags": ([{"level": "中", "reason": "信息不足，需销售进一步核实"}]
                           if not (needs or budget_amt) else []),
            "summary": summary,
        }


class LLMExtractor:
    """调用 LLM 做结构化抽取；未配置 key 时回退规则抽取。"""
    def __init__(self, settings: Settings):
        self.s = settings

    def extract(self, messages: List[RawMessage]) -> Dict[str, Any]:
        can_llm = self.s.llm_api_key and not (
            self.s.data_in_intranet and "local" not in self.s.llm_provider
        )
        if not can_llm:
            return RuleExtractor().extract(messages)
        # —— 接入点（占位）——
        # prompt = self._build_prompt(messages)
        # resp = call_llm(prompt, model=self.s.llm_model, api_key=self.s.llm_api_key, base_url=self.s.llm_base_url)
        # return json.loads(_strip_code_fence(resp))
        # 未接 SDK 前回退规则，保证不报错
        return RuleExtractor().extract(messages)

    def _build_prompt(self, messages: List[RawMessage]) -> str:
        convo = "\n".join(f"[{m.speaker_role}] {m.content}" for m in messages)
        return (
            "请从以下销售对话中抽取结构化客户洞察，严格按 Schema 输出 JSON：\n"
            f"{convo}\n\nSchema:\n{json.dumps(HEART_SCHEMA, ensure_ascii=False)}"
        )


def extract(messages: List[RawMessage], settings: Settings = None) -> Dict[str, Any]:
    """统一入口：有 key 走 LLM，否则规则兜底。"""
    settings = settings or Settings()
    return LLMExtractor(settings).extract(messages)

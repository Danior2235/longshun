"""方案 / 报价生成：根据心脏抽取洞察，生成服务方案 + 报价初稿。

有 LLM 走 LLM（生成式文案），无 key 走模板拼接（基于需求 + 业态参考单价 + 预算）。
报价单价目前为占位（集团业态参考价），接 PMS 后取真实房价/餐标。
"""
from __future__ import annotations

from typing import Any, Dict, List

from core.config import Settings
from core.message.raw_message import RawMessage
from core.schema.heart import extract

# 集团业态参考单价（占位，接 PMS 后取真实价）
PRICE_REF = {
    "场地": {"unit": "元/场", "price": 8000, "note": "含LED屏、音响、茶歇"},
    "住宿": {"unit": "元/间夜", "price": 480, "note": "协议价含双早"},
    "餐饮": {"unit": "元/人", "price": 150, "note": "围餐/自助"},
    "会议": {"unit": "元/场", "price": 8000, "note": "含基础会务"},
    "年会": {"unit": "元/人", "price": 380, "note": "含场地+餐饮+搭建"},
    "宴会": {"unit": "元/桌", "price": 1888, "note": "10人/桌"},
    "培训": {"unit": "元/人天", "price": 200, "note": "含场地+茶歇"},
    "接待": {"unit": "元/次", "price": 3000, "note": "含住宿+餐饮+车辆"},
    "活动": {"unit": "元/场", "price": 6000, "note": "团建/拓展"},
}

# 缺量级时给的示例量（Demo 用，真实环境由销售补充）
DEFAULT_QTY = {"场地": 2, "住宿": 100, "餐饮": 100, "会议": 2, "年会": 100,
               "宴会": 10, "培训": 50, "接待": 1, "活动": 1}


class SolutionGenerator:
    def __init__(self, settings: Settings = None):
        self.s = settings or Settings()

    def generate(self, messages: List[RawMessage]) -> Dict[str, Any]:
        insight = extract(messages, self.s)
        return self.from_insight(insight)

    def from_insight(self, insight: Dict[str, Any]) -> Dict[str, Any]:
        needs = insight.get("needs", [])
        lines: List[str] = []
        total_low = 0.0
        total_high = 0.0
        for n in needs:
            t = n.get("type", "")
            ref = PRICE_REF.get(t)
            if not ref:
                lines.append(f"- {t}：需求已记录，待细化量级后报价")
                continue
            try:
                qty = int(float(n.get("quantity") or DEFAULT_QTY.get(t, 1)))
            except (ValueError, TypeError):
                qty = DEFAULT_QTY.get(t, 1)
            subtotal = ref["price"] * qty
            total_low += subtotal
            total_high += subtotal * 1.15
            lines.append(
                f"- {t}：约 {qty}{ref['unit']} × ¥{ref['price']:,} = ¥{subtotal:,.0f}（{ref['note']}）")

        budget = insight.get("budget", {}).get("amount") or "待定"
        objection_tips: List[str] = []
        for o in insight.get("objections", []):
            cat = o.get("category")
            if cat == "价格":
                objection_tips.append("价格异议：突出协议价/含早含茶歇的综合性价比，对比单订成本")
            elif cat == "档期":
                objection_tips.append("档期异议：提供备选档期或预留方案")
            elif cat == "竞品":
                objection_tips.append("竞品异议：强调集团多业态联动与既往案例")
            elif cat == "信任":
                objection_tips.append("信任异议：提供资质/案例/合同保障")

        return {
            "insight_summary": insight.get("summary", ""),
            "needs": needs,
            "budget": budget,
            "quote_table": lines,
            "quote_range": (f"¥{total_low:,.0f} ~ ¥{total_high:,.0f}" if lines else "待定"),
            "objection_tips": objection_tips,
            "next_best_action": insight.get("next_best_action", ""),
        }

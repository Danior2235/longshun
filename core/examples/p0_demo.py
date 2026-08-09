"""P0 Demo 串联：用一小部分真实形态数据，跑出「画像 + 报价初稿 + 异议话术 + 下一步」。

数据来源（均可在无外部 key 下跑通）：
  • 微信对话（from_wechat_text）
  • 电话录音 ASR（mock_recognize 模拟说话人分离）
  • 爬虫线索（crawler/output/leads.jsonl，复用真实抓取产物）

运行：python core/examples/p0_demo.py
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from core.message.raw_message import from_wechat_text, from_phone_asr
from core.phone.asr import mock_recognize
from core.schema.heart import extract
from core.solution.generator import SolutionGenerator
from core.message.raw_message import SOURCE_WECHAT_TEXT, SOURCE_PHONE_ASR

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def load_leads(limit: int = 5):
    path = os.path.join(ROOT, "crawler", "output", "leads.jsonl")
    if not os.path.exists(path):
        return []
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out[:limit]


def main():
    # 1) 归一化：微信 + 电话
    wx = from_wechat_text(
        "客户：我们年底年会要200人会场，住宿含早，预算十万\n"
        "我：好的，我给您出方案\n"
        "客户：价格得实在点，别比别家贵太多", "s1", "c_wang")
    ph = from_phone_asr(mock_recognize(), "s1", "c_wang")
    msgs = wx + ph

    # 2) 心脏抽取 + 方案生成
    insight = extract(msgs)
    sol = SolutionGenerator().from_insight(insight)

    # 3) 输出报告
    print("=" * 52)
    print("P0 DEMO — 客户画像 + 方案生成（无外部 key 实跑）")
    print("=" * 52)
    print(f"消息来源：微信 {len(wx)} 条 / 电话 {len(ph)} 条（已归一化到 raw_message）")
    print(f"\n【客户画像】{insight['summary']}")
    print("  需求：", "、".join(n["type"] for n in insight["needs"]) or "（无）")
    print("  预算：", insight["budget"]["amount"] or "（无）")
    print("  异议：", "、".join(o["category"] for o in insight["objections"]) or "（无）")

    print("\n【报价初稿】")
    for l in sol["quote_table"]:
        print("  " + l)
    print("  报价区间：", sol["quote_range"])

    print("\n【异议应对话术】")
    for t in sol["objection_tips"]:
        print("  - " + t)
    if not sol["objection_tips"]:
        print("  （无）")

    print("\n【下一步动作】", sol["next_best_action"])

    leads = load_leads(5)
    print(f"\n【关联客户机会（爬虫，前 {len(leads)} 条）】")
    for ld in leads:
        print(f"  - [{ld.get('relevance')}] {ld.get('title')}｜{ld.get('buyer')}｜{ld.get('region')}")

    print("\n✅ P0 Demo 串联完成：采集 → 抽取 → 画像 → 报价 → 异议 → 线索关联。")


if __name__ == "__main__":
    main()

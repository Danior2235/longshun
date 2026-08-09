"""端到端采集示范（无外部 key 也能跑）。

演示两条通道如何归一化到 raw_message，并喂给心脏 Schema 抽取：
  • 微信截图：伪造 OCR blocks（含微信绿气泡 + 右侧气泡 + 前缀）→ classify_bubbles → raw_message
  • 电话录音：mock_recognize（带说话人分离）→ from_phone_asr → raw_message
  • 合并 → extract() 结构化洞察
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from core.config import Settings
from core.message.raw_message import RawMessage
from core.message.wechat_ocr import classify_bubbles, from_wechat_ocr
from core.phone.asr import mock_recognize, from_phone_asr
from core.schema.heart import extract

S = Settings.load()


def demo_wechat():
    print("########## 微信截图 OCR 气泡分类 ##########")
    # 伪造 OCR blocks：左侧灰气泡(对方) / 右侧微信绿(自己) / 带前缀
    blocks = [
        {"text": "王总，你们年底年会场地还有空吗？", "bbox": [40, 100, 260, 150], "bg_color": [235, 235, 235]},
        {"text": "有的，200人会场目前能排。", "bbox": [300, 170, 560, 220], "bg_color": [149, 236, 105]},
        {"text": "客户：住宿能含早吗，协议价多少？", "bbox": [40, 240, 320, 290], "bg_color": [235, 235, 235]},
        {"text": "我：含双早，协议价480一间。", "bbox": [300, 310, 580, 360], "bg_color": [149, 236, 105]},
    ]
    classified = classify_bubbles(blocks, image_width=620)
    msgs = from_wechat_ocr(classified, session_id="s_wx_01", customer_id="c_wang")
    for b, m in zip(classified, msgs):
        print(f"  [{b['speaker']:>8}] {b['text']}  -> raw:{m.speaker_role}")
    return msgs


def demo_phone():
    print("\n########## 电话录音 ASR（说话人分离）##########")
    segs = mock_recognize()
    msgs = from_phone_asr(segs, session_id="s_ph_01", customer_id="c_wang")
    for m in msgs:
        print(f"  [{m.speaker_role:>8}] {m.content}")
    return msgs


def main():
    wx = demo_wechat()
    ph = demo_phone()
    all_msgs: list[RawMessage] = wx + ph
    print("\n########## 心脏 Schema 结构化抽取 ##########")
    insight = extract(all_msgs)
    print(json.dumps(insight, ensure_ascii=False, indent=2))
    print("\n✅ 两条采集通道均已归一化到 raw_message，并成功进入结构化抽取。")


if __name__ == "__main__":
    main()

"""最小 MCP Server（stdio / JSON-RPC 2.0，标准库实现，零额外依赖）。

让小程序 / WorkBuddy / 业务系统共用一套数据读写接口：
  • message_list(session_id)        列出某会话的原始消息
  • message_append(raw_message)     写入一条归一化消息（5 种来源统一）
  • lead_list(limit, only_new)      列出爬虫抓到的客户机会
  • heart_extract(session_id)       对会话跑结构化抽取（心脏 Schema）
  • customer_profile(session_id)    聚合客户画像（需求/预算/异议/下一步动作）

数据落地：
  data/messages.jsonl    归一化消息流
  data/insights.jsonl    结构化抽取结果
  crawler/output/leads.jsonl  每日招标线索（复用爬虫产物）

WorkBuddy 接入：在 ~/.workbuddy/mcp.json 增加
  {"mcpServers": {"lszy": {"command": "python", "args": ["D:/Source/Repos/lszy_yx/mcp/server.py"]}}}
"""
from __future__ import annotations

import json
import os
import sys
from typing import Any, Dict, List, Optional

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from core.config import Settings
from core.message.raw_message import RawMessage
from core.schema.heart import extract

DATA_DIR = os.path.join(ROOT, "data")
MSGS = os.path.join(DATA_DIR, "messages.jsonl")
LEADS = os.path.join(ROOT, "crawler", "output", "leads.jsonl")
INSIGHTS = os.path.join(DATA_DIR, "insights.jsonl")
os.makedirs(DATA_DIR, exist_ok=True)

S = Settings.load()


# ----------------------- 工具实现 -----------------------
def _append_jsonl(path: str, obj: Dict[str, Any]) -> None:
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")


def _read_jsonl(path: str) -> List[Dict[str, Any]]:
    if not os.path.exists(path):
        return []
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def tool_message_list(session_id: str = "") -> Dict[str, Any]:
    msgs = _read_jsonl(MSGS)
    if session_id:
        msgs = [m for m in msgs if m.get("session_id") == session_id]
    return {"count": len(msgs), "messages": msgs[-50:]}


def tool_message_append(raw_message: Dict[str, Any]) -> Dict[str, Any]:
    rm = RawMessage(
        source=raw_message.get("source", "unknown"),
        speaker_role=raw_message.get("speaker_role", "unknown"),
        content=raw_message.get("content", ""),
        sent_at=raw_message.get("sent_at", ""),
        session_id=raw_message.get("session_id", ""),
        customer_id=raw_message.get("customer_id", ""),
        extra=raw_message.get("extra", {}),
    )
    _append_jsonl(MSGS, rm.to_dict())
    return {"ok": True, "session_id": rm.session_id}


def tool_lead_list(limit: int = 50, only_new: bool = False) -> Dict[str, Any]:
    leads = _read_jsonl(LEADS)
    if only_new:
        leads = [l for l in leads if l.get("relevance", 0) and l.get("relevance", 0) >= 0.5]
    return {"count": len(leads), "leads": leads[:limit]}


def tool_heart_extract(session_id: str) -> Dict[str, Any]:
    msgs_raw = _read_jsonl(MSGS)
    msgs = [RawMessage(**{k: v for k, v in m.items() if k in RawMessage.__dataclass_fields__})
            for m in msgs_raw if m.get("session_id") == session_id]
    if not msgs:
        return {"error": f"session {session_id} 无消息"}
    insight = extract(msgs)
    insight["session_id"] = session_id
    _append_jsonl(INSIGHTS, insight)
    return insight


def tool_customer_profile(session_id: str) -> Dict[str, Any]:
    msgs_raw = _read_jsonl(MSGS)
    msgs = [m for m in msgs_raw if m.get("session_id") == session_id]
    insights = [i for i in _read_jsonl(INSIGHTS) if i.get("session_id") == session_id]
    latest = insights[-1] if insights else tool_heart_extract(session_id)
    return {
        "session_id": session_id,
        "message_count": len(msgs),
        "needs": latest.get("needs", []),
        "budget": latest.get("budget", {}),
        "objections": latest.get("objections", []),
        "next_best_action": latest.get("next_best_action", ""),
        "summary": latest.get("summary", ""),
    }


TOOLS = {
    "message_list": {
        "description": "列出某会话（session_id）的归一化原始消息；不传则返回全部",
        "inputSchema": {"type": "object", "properties": {"session_id": {"type": "string"}},
                        "required": []},
        "fn": tool_message_list,
    },
    "message_append": {
        "description": "写入一条归一化消息（raw_message JSON），5 种来源统一",
        "inputSchema": {"type": "object",
                        "properties": {"raw_message": {"type": "object"}},
                        "required": ["raw_message"]},
        "fn": tool_message_append,
    },
    "lead_list": {
        "description": "列出爬虫抓到的客户机会；only_new=true 只看相关度>=0.5 的",
        "inputSchema": {"type": "object",
                        "properties": {"limit": {"type": "integer"}, "only_new": {"type": "boolean"}},
                        "required": []},
        "fn": tool_lead_list,
    },
    "heart_extract": {
        "description": "对会话跑结构化抽取（需求/预算/异议/下一步动作）",
        "inputSchema": {"type": "object",
                        "properties": {"session_id": {"type": "string"}},
                        "required": ["session_id"]},
        "fn": tool_heart_extract if False else None,  # placeholder; set below
    },
    "customer_profile": {
        "description": "聚合客户画像：需求/预算/异议/下一步动作",
        "inputSchema": {"type": "object",
                        "properties": {"session_id": {"type": "string"}},
                        "required": ["session_id"]},
        "fn": tool_customer_profile,
    },
}
TOOLS["heart_extract"]["fn"] = tool_heart_extract


# ----------------------- MCP 协议层 -----------------------
def _tool_defs() -> List[Dict[str, Any]]:
    return [{"name": n, "description": t["description"], "inputSchema": t["inputSchema"]}
            for n, t in TOOLS.items()]


def handle(req: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    method = req.get("method")
    rid = req.get("id")
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": rid, "result": {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "lszy-sales-agent", "version": "0.1.0"}}}
    if method == "notifications/initialized":
        return None
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": rid, "result": {"tools": _tool_defs()}}
    if method == "tools/call":
        name = req["params"]["name"]
        args = req["params"].get("arguments", {})
        try:
            result = TOOLS[name]["fn"](**args)
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}]}}
        except Exception as e:  # noqa
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": f"ERROR: {e}"}], "isError": True}}
    return {"jsonrpc": "2.0", "id": rid, "error": {"code": -32601, "message": f"unknown method {method}"}}


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue
        resp = handle(req)
        if resp:
            sys.stdout.write(json.dumps(resp, ensure_ascii=False) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()

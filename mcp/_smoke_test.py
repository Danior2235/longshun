"""MCP Server 冒烟测试：驱动协议交互并验证工具。运行后可直接删除。"""
import subprocess, json, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "server.py")

cmds = [
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
    {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
    {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {
        "name": "message_append", "arguments": {"raw_message": {
            "source": "wechat_text", "speaker_role": "customer",
            "content": "我们年底年会要200人会场，住宿含早，预算十万", "session_id": "demo1"}}}},
    {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {
        "name": "message_append", "arguments": {"raw_message": {
            "source": "phone_recording_asr", "speaker_role": "sales",
            "content": "价格按协议价，比别家实在", "session_id": "demo1"}}}},
    {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {
        "name": "heart_extract", "arguments": {"session_id": "demo1"}}},
    {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {
        "name": "customer_profile", "arguments": {"session_id": "demo1"}}},
    {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {
        "name": "lead_list", "arguments": {"only_new": True, "limit": 2}}},
]
inp = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds)
out = subprocess.run([sys.executable, SERVER], input=inp, capture_output=True, text=True)
print("STDERR:", out.stderr[:400])
for line in out.stdout.splitlines():
    try:
        r = json.loads(line)
        if "result" in r and "content" in r["result"]:
            print("ID", r.get("id"), "->", r["result"]["content"][0]["text"][:600])
        else:
            print("ID", r.get("id"), "->", str(r)[:200])
    except Exception:
        print("RAW", line[:200])

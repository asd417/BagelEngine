"""MCP server (stdio) that lets Claude drive a running BagelEngine.

The engine's remote console (src/bagel_remote_console.hpp) listens on 127.0.0.1:27015: send one
console command line, get back what it logged. This script exposes that as two MCP tools,
`console` and `screenshot`. Registered for Claude Code in .mcp.json at the repo root.

Stdlib only: MCP over stdio is newline-delimited JSON-RPC, small enough to speak directly.
Self-test without an engine:  python tools/bagel_mcp.py --selftest
"""
import base64
import json
import os
import socket
import sys
import tempfile
import time

HOST, PORT = "127.0.0.1", 27015  # cfg::kRemoteConsolePort
SCREENSHOT = os.path.join(tempfile.gettempdir(), "bagel_mcp_screenshot.png")

TOOLS = [
    {
        "name": "console",
        "description": (
            "Run one BagelEngine developer-console command and return its output. Useful commands: "
            "help; getpos; setpos <x> <y> <z>; setang <pitch> <yaw> <roll> (degrees); ent_list; "
            "camfocus; orbit <0|1>; freefly; textmap <name>; map <name>; anim <name|list|stop>; "
            "r_drawmode <0-10>; r_drawbloom <0|1>; r_smaa; showwireframe; r_drawbbox; togglephysics; "
            "toggleui; editmode <0|1>. Commands are case-insensitive."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {"command": {"type": "string", "description": "e.g. 'setpos 0 10 -30'"}},
            "required": ["command"],
        },
    },
    {
        "name": "screenshot",
        "description": "Capture the engine window's next frame (ImGui panels included; "
        "run `toggleui` first to hide them) and return it as an image.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def console(command):
    try:
        with socket.create_connection((HOST, PORT), timeout=10) as s:
            s.sendall(command.encode() + b"\n")
            chunks = []
            while chunk := s.recv(65536):
                chunks.append(chunk)
    except ConnectionRefusedError:
        raise RuntimeError(f"BagelEngine is not running (nothing listening on {HOST}:{PORT})")
    return b"".join(chunks).decode(errors="replace")


def screenshot():
    if os.path.exists(SCREENSHOT):
        os.remove(SCREENSHOT)
    console("screenshot " + SCREENSHOT)
    # The engine writes the file after the frame is submitted, renaming it into place once complete.
    deadline = time.time() + 10
    while not os.path.exists(SCREENSHOT):
        if time.time() > deadline:
            raise TimeoutError("engine did not write the screenshot (window minimized?)")
        time.sleep(0.05)
    with open(SCREENSHOT, "rb") as f:
        return base64.b64encode(f.read()).decode()


def call_tool(name, args):
    if name == "console":
        return [{"type": "text", "text": console(args["command"]).rstrip() or "(no output)"}]
    if name == "screenshot":
        return [{"type": "image", "data": screenshot(), "mimeType": "image/png"}]
    raise ValueError(f"unknown tool: {name}")


def handle(msg):
    """Result for a JSON-RPC request, or None if the method is unknown."""
    method, params = msg.get("method"), msg.get("params") or {}
    if method == "initialize":
        return {
            "protocolVersion": params.get("protocolVersion", "2025-06-18"),
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "bagel-engine", "version": "0.1"},
        }
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        try:
            return {"content": call_tool(params["name"], params.get("arguments") or {})}
        except Exception as e:  # tool failures go back to the model, not as protocol errors
            return {"content": [{"type": "text", "text": f"{type(e).__name__}: {e}"}], "isError": True}
    return None


def serve():
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    for line in sys.stdin:
        if not line.strip():
            continue
        msg = json.loads(line)
        if "id" not in msg:
            continue  # notifications (initialized, cancelled) need no reply
        result = handle(msg)
        reply = {"jsonrpc": "2.0", "id": msg["id"]}
        if result is None:
            reply["error"] = {"code": -32601, "message": f"method not found: {msg.get('method')}"}
        else:
            reply["result"] = result
        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()


def selftest():
    assert handle({"method": "initialize", "params": {"protocolVersion": "X"}})["protocolVersion"] == "X"
    assert [t["name"] for t in handle({"method": "tools/list"})["tools"]] == ["console", "screenshot"]
    assert handle({"method": "nope"}) is None
    bad = handle({"method": "tools/call", "params": {"name": "nope"}})
    assert bad["isError"] and "unknown tool" in bad["content"][0]["text"]
    print("selftest ok")


if __name__ == "__main__":
    selftest() if "--selftest" in sys.argv else serve()

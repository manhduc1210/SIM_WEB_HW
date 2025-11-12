# server_combined_A.py
from __future__ import annotations
import socket
import threading
import time
from concurrent import futures
from typing import List, Optional, Literal

import grpc
import gpio_demo_pb2
import gpio_demo_pb2_grpc

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
import uvicorn
from pydantic import BaseModel, Field

# ================= Common =================
SOCK_PATH = "/tmp/gpio_sim.sock"
RECV_BUFSZ = 4096
SOCK_TIMEOUT = 1.0

def send_cmd(sock: socket.socket, cmd: str) -> str:
    if not cmd.endswith("\n"):
        cmd += "\n"
    sock.sendall(cmd.encode("utf-8"))
    data = b""
    # đọc 1 dòng kết thúc '\n'
    while not data.endswith(b"\n"):
        chunk = sock.recv(128)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", errors="replace").strip()

def parse_led_line(raw: str) -> List[int]:
    s = raw.strip()
    if not s.upper().startswith("LED"):
        return []
    rest = s[3:].strip(" :\t")
    rest = rest.replace(",", " ")
    parts = [p for p in rest.split() if p]
    out: List[int] = []
    for p in parts:
        try:
            out.append(1 if int(p) != 0 else 0)
        except ValueError:
            pass
    return out

# ================= gRPC Server (socket riêng) =================
class GpioDemoServicer(gpio_demo_pb2_grpc.GpioDemoServicer):
    def __init__(self):
        # mỗi servicer giữ socket riêng
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(SOCK_TIMEOUT)
        print("[PY][gRPC] connecting to C daemon ...")
        self.sock.connect(SOCK_PATH)
        print("[PY][gRPC] connected.")

    def PressButton(self, request, context):
        idx = request.index
        print(f"[PY][gRPC] PressButton({idx})")
        resp = send_cmd(self.sock, f"PRESS {idx}")
        print(f"[PY][C-DAEMON] {resp}")
        return gpio_demo_pb2.SimpleReply(msg=resp)

    def ReleaseButton(self, request, context):
        idx = request.index
        print(f"[PY][gRPC] ReleaseButton({idx})")
        resp = send_cmd(self.sock, f"RELEASE {idx}")
        print(f"[PY][C-DAEMON] {resp}")
        return gpio_demo_pb2.SimpleReply(msg=resp)

    def GetLedState(self, request, context):
        print("[PY][gRPC] GetLedState()")
        resp = send_cmd(self.sock, "GETLED")
        print(f"[PY][C-DAEMON] {resp}")
        leds = parse_led_line(resp)
        return gpio_demo_pb2.LedState(leds=leds)

def run_grpc_server():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    gpio_demo_pb2_grpc.add_GpioDemoServicer_to_server(GpioDemoServicer(), server)
    server.add_insecure_port("[::]:50051")
    print("[PY][gRPC] server started at :50051")
    server.start()
    server.wait_for_termination()

# ================= FastAPI (socket riêng) =================
class ButtonReq(BaseModel):
    index: int = Field(ge=0, description="0 hoặc 1")
    action: Literal["press", "release"]

class StepReq(BaseModel):
    times: int = Field(default=1, ge=1)
    interval_ms: int = Field(default=0, ge=0)

class LedResp(BaseModel):
    leds: List[int]

class SimpleReply(BaseModel):
    msg: str

class SimClient:
    """Client riêng cho FastAPI."""
    def __init__(self, path: str, timeout: float):
        self.path = path
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None
        self._lock = threading.Lock()
        self._connect()

    def _connect(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        s.connect(self.path)
        self._sock = s
        print("[PY][HTTP] connected to C daemon.")

    def _send_recv(self, line: str) -> str:
        with self._lock:
            try:
                assert self._sock is not None
                return send_cmd(self._sock, line)
            except (BrokenPipeError, ConnectionResetError, OSError):
                # reconnect 1 lần
                print("[PY][HTTP] reconnecting socket...")
                try:
                    if self._sock:
                        self._sock.close()
                finally:
                    self._connect()
                return send_cmd(self._sock, line)

    def get_leds(self) -> List[int]:
        raw = self._send_recv("GETLED")
        leds = parse_led_line(raw)
        if not leds:
            raise ValueError(f"Unexpected LED response: {raw!r}")
        return leds

    def press(self, idx: int) -> str:
        return self._send_recv(f"PRESS {idx}")

    def release(self, idx: int) -> str:
        return self._send_recv(f"RELEASE {idx}")

    def step(self, times: int, interval_ms: int) -> str:
        return self._send_recv(f"STEP {times} {interval_ms}")

app = FastAPI(title="GPIO Sim HTTP (A - separate sockets)", version="0.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173",
                   "http://localhost:3000", "http://127.0.0.1:3000"],
    allow_credentials=True, allow_methods=["*"], allow_headers=["*"],
)

_http_client: Optional[SimClient] = None

@app.on_event("startup")
def http_startup():
    global _http_client
    _http_client = SimClient(SOCK_PATH, SOCT_TIMEOUT) if False else SimClient(SOCK_PATH, SOCK_TIMEOUT)  # keep line length
    print("[PY][HTTP] FastAPI started at :8000")

@app.get("/api/health", response_model=SimpleReply)
def health():
    try:
        assert _http_client is not None
        _ = _http_client.get_leds()
        return SimpleReply(msg="ok")
    except Exception as e:
        raise HTTPException(status_code=503, detail=str(e))

@app.get("/api/leds", response_model=LedResp)
def get_leds():
    try:
        assert _http_client is not None
        return LedResp(leds=_http_client.get_leds())
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

@app.post("/api/button", response_model=SimpleReply)
def post_button(req: ButtonReq):
    try:
        assert _http_client is not None
        resp = _http_client.press(req.index) if req.action == "press" else _http_client.release(req.index)
        return SimpleReply(msg=resp)
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

@app.post("/api/step", response_model=SimpleReply)
def post_step(req: StepReq):
    try:
        assert _http_client is not None
        return SimpleReply(msg=_http_client.step(req.times, req.interval_ms))
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

def run_http_server():
    # chạy uvicorn trong thread
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")

# ================= Entry =================
if __name__ == "__main__":
    t = threading.Thread(target=run_grpc_server, daemon=True)
    t.start()
    # chạy HTTP ở main thread (để Ctrl+C ngắt gọn)
    run_http_server()

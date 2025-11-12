# web_api.py
from __future__ import annotations
import socket
import threading
import time
from typing import List, Literal, Optional

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field, validator

# ========= Config =========
SOCK_PATH = "/tmp/gpio_sim.sock"
SOCK_TIMEOUT = 1.0       # giây, timeout cho recv/send
RECV_BUFSZ = 4096
CONNECT_RETRY = 3        # số lần thử reconnect

# ========= Models =========
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

# ========= Socket Client (UNIX) =========
class SimClient:
    """
    Client nói chuyện với daemon C qua UNIX domain socket.
    - Reuse 1 connection (app-wide).
    - Thread-safe (Lock).
    - Tự reconnect khi gặp lỗi đường truyền.
    """
    def __init__(self, path: str, timeout: float = 1.0):
        self.path = path
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None
        self._lock = threading.Lock()
        self._connect_initial()

    def _connect_initial(self):
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(self.timeout)
        try:
            self._sock.connect(self.path)
        except Exception as e:
            # Để app vẫn khởi động; sẽ reconnect khi có request.
            self._sock.close()
            self._sock = None
            print(f"[WARN] Initial connect failed: {e}")

    def _ensure_conn(self):
        if self._sock is not None:
            return
        # thử reconnect vài lần
        last_exc = None
        for i in range(CONNECT_RETRY):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(self.timeout)
                s.connect(self.path)
                self._sock = s
                return
            except Exception as e:
                last_exc = e
                time.sleep(0.1 * (i + 1))
        raise ConnectionError(f"Cannot connect to {self.path}: {last_exc}")

    def _send_recv_line(self, line: str) -> str:
        """
        Gửi 1 dòng lệnh (thêm '\n' nếu thiếu), nhận 1 dòng phản hồi (đến '\n' hoặc hết buffer).
        Trả về chuỗi (đã strip).
        """
        with self._lock:
            self._ensure_conn()
            assert self._sock is not None

            cmd = line if line.endswith("\n") else line + "\n"
            data = cmd.encode("utf-8")

            try:
                self._sock.sendall(data)
                resp = self._sock.recv(RECV_BUFSZ)
            except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError) as e:
                # Thử reconnect 1 lần và gửi lại
                print(f"[WARN] send/recv error: {e}; reconnecting...")
                try:
                    if self._sock:
                        self._sock.close()
                finally:
                    self._sock = None

                self._ensure_conn()
                assert self._sock is not None
                self._sock.sendall(data)
                resp = self._sock.recv(RECV_BUFSZ)

            if not resp:
                raise ConnectionError("Empty response from daemon")

            return resp.decode("utf-8", errors="replace").strip()

    # ---- High-level helpers ----
    def get_leds(self) -> List[int]:
        raw = self._send_recv_line("GETLED")
        leds = parse_led_line(raw)
        if leds is None:
            raise ValueError(f"Unexpected LED response: {raw!r}")
        return leds

    def press(self, index: int) -> str:
        return self._send_recv_line(f"PRESS {index}")

    def release(self, index: int) -> str:
        return self._send_recv_line(f"RELEASE {index}")

    def step(self, times: int, interval_ms: int) -> str:
        # Tùy daemon: nếu không hỗ trợ interval_ms, bạn có thể chỉ gửi "STEP {times}"
        return self._send_recv_line(f"STEP {times} {interval_ms}")

# ========= Parsers =========
def parse_led_line(raw: str) -> Optional[List[int]]:
    """
    Chấp nhận một số format phổ biến:
      - "LED 1 0 0 0"
      - "LED: 1 0 0 0"
      - "LED 1,0,0,0"
      - "LED:1,0,0,0"
    Trả về list[int] hoặc None nếu không parse được.
    """
    s = raw.strip()
    if not s.upper().startswith("LED"):
        return None
    # bỏ "LED" + dấu câu
    rest = s[3:].strip(" :\t")
    # thay dấu phẩy => cách
    rest = rest.replace(",", " ")
    parts = [p for p in rest.split() if p]
    leds: List[int] = []
    for p in parts:
        try:
            v = int(p)
            leds.append(1 if v != 0 else 0)
        except ValueError:
            # bỏ qua rác
            pass
    return leds if leds else None

# ========= FastAPI App =========
app = FastAPI(title="GPIO Sim HTTP Shim", version="0.1.0")

# CORS cho dev front-end
app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
        "http://localhost:3000",
        "http://127.0.0.1:3000",
    ],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

client: Optional[SimClient] = None

@app.on_event("startup")
def on_startup():
    global client
    client = SimClient(SOCK_PATH, timeout=SOCK_TIMEOUT)
    print("[INFO] HTTP shim started")

@app.get("/api/health", response_model=SimpleReply)
def health():
    # thử get_leds nhẹ để biết dịch vụ sống
    try:
        assert client is not None
        _ = client.get_leds()
        return SimpleReply(msg="ok")
    except Exception as e:
        raise HTTPException(status_code=503, detail=f"daemon not ready: {e}")

@app.get("/api/leds", response_model=LedResp)
def get_leds():
    try:
        assert client is not None
        leds = client.get_leds()
        return LedResp(leds=leds)
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

@app.post("/api/button", response_model=SimpleReply)
def post_button(req: ButtonReq):
    try:
        assert client is not None
        if req.action == "press":
            resp = client.press(req.index)
        else:
            resp = client.release(req.index)
        return SimpleReply(msg=resp)
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

@app.post("/api/step", response_model=SimpleReply)
def post_step(req: StepReq):
    try:
        assert client is not None
        resp = client.step(req.times, req.interval_ms)
        return SimpleReply(msg=resp)
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

import socket
from concurrent import futures
import grpc
import gpio_demo_pb2
import gpio_demo_pb2_grpc

SOCK_PATH = "/tmp/gpio_sim.sock"

def send_cmd(sock: socket.socket, cmd: str) -> str:
    sock.sendall(cmd.encode("utf-8"))
    # đọc 1 dòng về
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(128)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8").strip()

class GpioDemoServicer(gpio_demo_pb2_grpc.GpioDemoServicer):
    def __init__(self, sock: socket.socket):
        self.sock = sock

    def PressButton(self, request, context):
        idx = request.index
        print(f"[PY][gRPC] PressButton({idx})")
        resp = send_cmd(self.sock, f"PRESS {idx}\n")
        print(f"[PY][C-DAEMON] {resp}")
        return gpio_demo_pb2.SimpleReply(msg=resp)

    def ReleaseButton(self, request, context):
        idx = request.index
        print(f"[PY][gRPC] ReleaseButton({idx})")
        resp = send_cmd(self.sock, f"RELEASE {idx}\n")
        print(f"[PY][C-DAEMON] {resp}")
        return gpio_demo_pb2.SimpleReply(msg=resp)

    def GetLedState(self, request, context):
        print("[PY][gRPC] GetLedState()")
        resp = send_cmd(self.sock, "GETLED\n")
        print(f"[PY][C-DAEMON] {resp}")
        parts = resp.split()
        leds = list(map(int, parts[1:])) if parts[0] == "LED" else []
        return gpio_demo_pb2.LedState(leds=leds)

def serve():
    # kết nối tới daemon C
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    print("[PY][INFO] connecting to C daemon ...")
    sock.connect(SOCK_PATH)
    print("[PY][INFO] connected to C daemon.")

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    gpio_demo_pb2_grpc.add_GpioDemoServicer_to_server(
        GpioDemoServicer(sock), server
    )
    server.add_insecure_port("[::]:50051")
    print("[PY][INFO] gRPC server started at :50051")
    server.start()
    server.wait_for_termination()

if __name__ == "__main__":
    serve()

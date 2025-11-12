import grpc, gpio_demo_pb2, gpio_demo_pb2_grpc
import time;
ch = grpc.insecure_channel("localhost:50051")
stub = gpio_demo_pb2_grpc.GpioDemoStub(ch)

# ấn
# đợi tí cho C tick

# đọc LED

# thả
stub.PressButton(gpio_demo_pb2.ButtonReq(index=0))
time.sleep(0.02)
stub.ReleaseButton(gpio_demo_pb2.ButtonReq(index=0))
print(stub.GetLedState(gpio_demo_pb2.Empty()).leds)
stub.PressButton(gpio_demo_pb2.ButtonReq(index=0))
time.sleep(0.02)
stub.ReleaseButton(gpio_demo_pb2.ButtonReq(index=0))
print(stub.GetLedState(gpio_demo_pb2.Empty()).leds)
stub.PressButton(gpio_demo_pb2.ButtonReq(index=0))
time.sleep(0.02)
stub.ReleaseButton(gpio_demo_pb2.ButtonReq(index=0))
print(stub.GetLedState(gpio_demo_pb2.Empty()).leds)
stub.PressButton(gpio_demo_pb2.ButtonReq(index=0))
time.sleep(0.02)
stub.ReleaseButton(gpio_demo_pb2.ButtonReq(index=0))
print(stub.GetLedState(gpio_demo_pb2.Empty()).leds)

stub.PressButton(gpio_demo_pb2.ButtonReq(index=1))
time.sleep(0.02)
stub.ReleaseButton(gpio_demo_pb2.ButtonReq(index=1))
print(stub.GetLedState(gpio_demo_pb2.Empty()).leds)

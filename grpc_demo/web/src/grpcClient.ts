import { GrpcWebFetchTransport } from "@protobuf-ts/grpcweb-transport";
import { GpioDemoClient } from "./generated/gpio_demo.client";
import type { ButtonReq, Empty } from "./generated/gpio_demo";

// Envoy gRPC-Web endpoint
const transport = new GrpcWebFetchTransport({
  baseUrl: "http://localhost:8080",
});

// Client gRPC-Web
const client = new GpioDemoClient(transport);

export async function getLedState(): Promise<number[]> {
  // với protobuf-ts, request chỉ là object {} phù hợp với type Empty
  const req: Empty = {};
  const { response } = await client.getLedState(req);
  return response.leds; // field leds[] trong LedState
}

export async function pressButton(index: number): Promise<string> {
  const req: ButtonReq = { index };
  const { response } = await client.pressButton(req);
  return response.msg;
}

export async function releaseButton(index: number): Promise<string> {
  const req: ButtonReq = { index };
  const { response } = await client.releaseButton(req);
  return response.msg;
}

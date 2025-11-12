export type LedResp = { leds: number[] };
export type ButtonAction = 'press' | 'release';
export type SimpleReply = { msg: string };

async function http<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json', ...(init?.headers || {}) },
    ...init,
  });
  if (!res.ok) {
    const text = await res.text().catch(() => '');
    throw new Error(`HTTP ${res.status}: ${text || res.statusText}`);
  }
  return res.json() as Promise<T>;
}

export const Api = {
  getLeds: () => http<LedResp>('/api/leds'),
  press:   (index: number) => http<SimpleReply>('/api/button', { method: 'POST', body: JSON.stringify({ index, action: 'press' as ButtonAction })}),
  release: (index: number) => http<SimpleReply>('/api/button', { method: 'POST', body: JSON.stringify({ index, action: 'release' as ButtonAction })}),
  step:    (times = 1, interval_ms = 0) => http<SimpleReply>('/api/step', { method: 'POST', body: JSON.stringify({ times, interval_ms })}),
};

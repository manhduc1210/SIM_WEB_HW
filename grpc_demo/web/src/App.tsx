import React, { useEffect, useState } from 'react';
import { getLedState, pressButton, releaseButton } from './grpcClient';

export default function App() {
  const [leds, setLeds] = useState<number[]>([]);
  const [msg, setMsg] = useState('');
  const [status, setStatus] = useState<'idle' | 'ok' | 'error'>('idle');

  useEffect(() => {
    let alive = true;

    const poll = async () => {
      try {
        const arr = await getLedState();
        if (!alive) return;
        setLeds(arr);
        setStatus('ok');
      } catch (e) {
        console.error('poll LED error', e);
        if (alive) setStatus('error');
      }
    };

    poll();
    const id = setInterval(poll, 300);

    return () => {
      alive = false;
      clearInterval(id);
    };
  }, []);

  const doPress = async (idx: number) => {
    const res = await pressButton(idx);
    setMsg(`Press BTN${idx}: ${res}`);
  };

  const doRelease = async (idx: number) => {
    const res = await releaseButton(idx);
    setMsg(`Release BTN${idx}: ${res}`);
  };

  return (
    <div style={{ padding: 20, fontFamily: 'sans-serif' }}>
      <h1>GPIO gRPC-Web Demo</h1>

      <h2>LEDs:</h2>
      <div style={{ display: 'flex', gap: 10 }}>
        {leds.map((v, i) => (
          <div
            key={i}
            style={{
              width: 40,
              height: 40,
              borderRadius: 8,
              background: v ? '#4ade80' : '#ccc',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontWeight: 'bold'
            }}
          >
            {i}
          </div>
        ))}
      </div>

      <p>Status: {status}</p>

      <h2>Buttons:</h2>
      <button onClick={() => doPress(0)}>Press BTN0</button>
      <button onClick={() => doRelease(0)}>Release BTN0</button>

      <br /><br />

      <button onClick={() => doPress(1)}>Press BTN1</button>
      <button onClick={() => doRelease(1)}>Release BTN1</button>

      <p>Message: {msg}</p>
    </div>
  );
}

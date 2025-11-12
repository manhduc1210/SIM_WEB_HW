import React, { useEffect, useRef, useState } from 'react';
import LedBar from './components/LedBar';
import Controls from './components/Controls';
import { Api } from './api';

export default function App() {
  const [leds, setLeds] = useState<number[]>([]);
  const [status, setStatus] = useState<'ok' | 'error' | 'idle'>('idle');
  const timerRef = useRef<number | null>(null);

  // Poll LEDs
  useEffect(() => {
    let mounted = true;

    const pull = async () => {
      try {
        const { leds } = await Api.getLeds();
        if (mounted) {
          setLeds(leds);
          setStatus('ok');
        }
      } catch (e) {
        console.error('[LED poll] error:', e);
        if (mounted) setStatus('error');
      }
    };

    // chạy ngay và lặp
    pull();
    timerRef.current = window.setInterval(pull, 300);

    return () => {
      mounted = false;
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    };
  }, []);

  const onPress = async (idx: number) => {
    try {
      await Api.press(idx);
      // Optional: pull ngay sau khi bấm để phản hồi nhanh
      const { leds } = await Api.getLeds();
      setLeds(leds);
    } catch (e) {
      console.error('press failed', e);
      setStatus('error');
    }
  };

  const onRelease = async (idx: number) => {
    try {
      await Api.release(idx);
      const { leds } = await Api.getLeds();
      setLeds(leds);
    } catch (e) {
      console.error('release failed', e);
      setStatus('error');
    }
  };

  return (
    <div className="page">
      <h1>GPIO Web Simulator</h1>

      <section>
        <h2>LEDs</h2>
        <LedBar leds={leds} />
        <div className={`status ${status}`}>Status: {status}</div>
      </section>

      <section>
        <h2>Buttons</h2>
        <Controls onPress={onPress} onRelease={onRelease} />
      </section>

      <footer>
        <small>Backend: FastAPI :8000 (proxy /api), gRPC :50051, C daemon via /tmp/gpio_sim.sock</small>
      </footer>
    </div>
  );
}

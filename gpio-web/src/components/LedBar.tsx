import React from 'react';

type Props = { leds: number[] };

export default function LedBar({ leds }: Props) {
  if (!leds?.length) return <div className="muted">No LEDs</div>;

  return (
    <div className="ledbar">
      {leds.map((v, i) => (
        <div
          key={i}
          className={`led ${v ? 'on' : 'off'}`}
          aria-label={`LED${i} ${v ? 'ON' : 'OFF'}`}
          title={`LED${i}: ${v ? 'ON' : 'OFF'}`}
        >
          {i}
        </div>
      ))}
    </div>
  );
}

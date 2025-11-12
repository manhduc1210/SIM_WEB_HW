import React from 'react';

type Props = {
  onPress: (idx: number) => void;
  onRelease: (idx: number) => void;
};

export default function Controls({ onPress, onRelease }: Props) {
  return (
    <div className="controls">
      <div className="group">
        <div className="label">BTN0</div>
        <button onClick={() => onPress(0)}>Press</button>
        <button onClick={() => onRelease(0)}>Release</button>
      </div>
      <div className="group">
        <div className="label">BTN1</div>
        <button onClick={() => onPress(1)}>Press</button>
        <button onClick={() => onRelease(1)}>Release</button>
      </div>
    </div>
  );
}

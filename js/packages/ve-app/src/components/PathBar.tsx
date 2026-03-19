import { useState, useCallback } from 'react';
import { Input, Button, Space } from 'antd';
import { ReloadOutlined, EnterOutlined } from '@ant-design/icons';

interface Props {
  value: string;
  onNavigate: (path: string) => void;
  onRefresh: () => void;
}

export function PathBar({ value, onNavigate, onRefresh }: Props) {
  const [input, setInput] = useState('');
  const [focused, setFocused] = useState(false);

  const handleSubmit = useCallback(() => {
    onNavigate(input || '');
    setFocused(false);
  }, [input, onNavigate]);

  const displayValue = focused ? input : value || '/';

  return (
    <Space.Compact style={{ flex: 1, maxWidth: 600 }}>
      <Input
        size="small"
        prefix={<span style={{ opacity: 0.5, fontSize: 12 }}>/</span>}
        value={displayValue}
        onFocus={() => { setInput(value); setFocused(true); }}
        onBlur={() => setFocused(false)}
        onChange={(e) => setInput(e.target.value)}
        onPressEnter={handleSubmit}
        placeholder="Enter node path..."
        style={{ fontFamily: 'monospace', fontSize: 12 }}
      />
      {focused && (
        <Button size="small" icon={<EnterOutlined />} onMouseDown={handleSubmit} />
      )}
      <Button size="small" icon={<ReloadOutlined />} onClick={onRefresh} title="Refresh" />
    </Space.Compact>
  );
}

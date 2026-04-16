import { Tag } from 'antd';

interface Props {
  connected: boolean;
}

export function StatusBar({ connected }: Props) {
  return (
    <Tag color={connected ? 'green' : 'red'} style={{ margin: 0 }}>
      {connected ? 'WS Connected' : 'WS Disconnected'}
    </Tag>
  );
}

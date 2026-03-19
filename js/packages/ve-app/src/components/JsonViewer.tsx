import { Typography } from 'antd';

const { Text } = Typography;

interface Props {
  data: unknown;
}

export function JsonViewer({ data }: Props) {
  if (data === null || data === undefined) {
    return <Text type="secondary" italic>No data</Text>;
  }

  const json = typeof data === 'string' ? data : JSON.stringify(data, null, 2);

  return (
    <pre style={{
      margin: 0,
      padding: 12,
      borderRadius: 6,
      fontSize: 12,
      fontFamily: "'Cascadia Code', 'Fira Code', 'Consolas', monospace",
      lineHeight: 1.6,
      overflow: 'auto',
      maxHeight: 'calc(100vh - 240px)',
      background: 'rgba(255,255,255,0.04)',
    }}>
      {json}
    </pre>
  );
}

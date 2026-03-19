import { useState, useEffect } from 'react';
import { Card, Descriptions, Input, Button, Space, Tabs, Typography, Tag, Modal, message } from 'antd';
import { EditOutlined, SaveOutlined, CloseOutlined, ExportOutlined, ImportOutlined, CopyOutlined } from '@ant-design/icons';
import type { NodeEntry } from '../store/ve-store';
import type { TreeNode, VarValue } from '@ve/sdk';
import { JsonViewer } from './JsonViewer';
import { veHttp } from '../store/ve-store';

const { Text } = Typography;

interface Props {
  selectedPath: string;
  selectedNode: NodeEntry | null;
  treeJson: TreeNode | null;
  onSetValue: (path: string, value: VarValue) => Promise<void>;
  onImportTree?: (path: string, json: string) => Promise<void>;
}

function varTypeLabel(value: VarValue): string {
  if (value === null || value === undefined) return 'null';
  if (typeof value === 'boolean') return 'bool';
  if (typeof value === 'number') return Number.isInteger(value) ? 'int' : 'double';
  if (typeof value === 'string') return 'string';
  if (Array.isArray(value)) return 'list';
  if (typeof value === 'object') return 'dict';
  return 'unknown';
}

function formatValue(value: VarValue): string {
  if (value === null || value === undefined) return '(null)';
  if (typeof value === 'object') return JSON.stringify(value, null, 2);
  return String(value);
}

function parseInputValue(text: string): VarValue {
  const trimmed = text.trim();
  if (trimmed === 'null') return null;
  if (trimmed === 'true') return true;
  if (trimmed === 'false') return false;
  const num = Number(trimmed);
  if (!isNaN(num) && trimmed !== '') return num;
  try {
    return JSON.parse(trimmed);
  } catch {
    return trimmed;
  }
}

export function NodeInspector({ selectedPath, selectedNode, treeJson, onSetValue, onImportTree }: Props) {
  const [editing, setEditing] = useState(false);
  const [editValue, setEditValue] = useState('');
  const [importOpen, setImportOpen] = useState(false);
  const [importJson, setImportJson] = useState('');

  useEffect(() => {
    setEditing(false);
  }, [selectedPath]);

  if (!selectedPath) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', opacity: 0.4 }}>
        Select a node from the tree
      </div>
    );
  }

  const value = selectedNode?.value;
  const typeTag = varTypeLabel(value ?? null);

  const handleSave = async () => {
    try {
      await onSetValue(selectedPath, parseInputValue(editValue));
      setEditing(false);
      message.success('Value updated');
    } catch (err) {
      message.error(`Failed: ${err}`);
    }
  };

  const handleStartEdit = () => {
    setEditValue(formatValue(value ?? null));
    setEditing(true);
  };

  const tabItems = [
    {
      key: 'info',
      label: 'Info',
      children: (
        <div>
          <Descriptions column={1} size="small" bordered>
            <Descriptions.Item label="Path">
              <Text code>{selectedPath || '/'}</Text>
            </Descriptions.Item>
            <Descriptions.Item label="Name">
              {selectedNode?.name || '(root)'}
            </Descriptions.Item>
            <Descriptions.Item label="Type">
              <Tag color="blue">{typeTag}</Tag>
            </Descriptions.Item>
            <Descriptions.Item label="Children">
              {selectedNode?.childNames.length ?? 0}
            </Descriptions.Item>
            <Descriptions.Item label="Value">
              {editing ? (
                <Space direction="vertical" style={{ width: '100%' }}>
                  <Input.TextArea
                    value={editValue}
                    onChange={(e) => setEditValue(e.target.value)}
                    autoSize={{ minRows: 2, maxRows: 10 }}
                    style={{ fontFamily: 'monospace', fontSize: 12 }}
                  />
                  <Space>
                    <Button size="small" type="primary" icon={<SaveOutlined />} onClick={handleSave}>
                      Save
                    </Button>
                    <Button size="small" icon={<CloseOutlined />} onClick={() => setEditing(false)}>
                      Cancel
                    </Button>
                  </Space>
                </Space>
              ) : (
                <Space>
                  <Text style={{ fontFamily: 'monospace', fontSize: 12, whiteSpace: 'pre-wrap' }}>
                    {formatValue(value ?? null)}
                  </Text>
                  <Button size="small" type="text" icon={<EditOutlined />} onClick={handleStartEdit} />
                </Space>
              )}
            </Descriptions.Item>
          </Descriptions>
        </div>
      ),
    },
    {
      key: 'json',
      label: 'JSON Tree',
      children: (
        <div>
          <Space style={{ marginBottom: 12 }}>
            <Button
              size="small"
              icon={<CopyOutlined />}
              onClick={async () => {
                try {
                  const raw = await veHttp.exportTree(selectedPath);
                  await navigator.clipboard.writeText(raw);
                  message.success('JSON copied to clipboard');
                } catch (err) {
                  message.error(`Export failed: ${err}`);
                }
              }}
            >
              Copy JSON
            </Button>
            <Button
              size="small"
              icon={<ExportOutlined />}
              onClick={async () => {
                try {
                  const raw = await veHttp.exportTree(selectedPath);
                  const blob = new Blob([raw], { type: 'application/json' });
                  const url = URL.createObjectURL(blob);
                  const a = document.createElement('a');
                  a.href = url;
                  a.download = `${selectedPath.replace(/\//g, '_') || 'root'}.json`;
                  a.click();
                  URL.revokeObjectURL(url);
                } catch (err) {
                  message.error(`Export failed: ${err}`);
                }
              }}
            >
              Download
            </Button>
            <Button
              size="small"
              icon={<ImportOutlined />}
              onClick={() => {
                setImportJson('');
                setImportOpen(true);
              }}
            >
              Import
            </Button>
          </Space>
          <JsonViewer data={treeJson} />
        </div>
      ),
    },
  ];

  return (
    <>
      <Card
        size="small"
        title={<Text strong style={{ fontSize: 14 }}>{selectedPath || '/'}</Text>}
      >
        <Tabs items={tabItems} size="small" />
      </Card>

      <Modal
        title={`Import JSON into ${selectedPath || '/'}`}
        open={importOpen}
        onCancel={() => setImportOpen(false)}
        onOk={async () => {
          if (!importJson.trim()) { message.warning('Empty JSON'); return; }
          try {
            if (onImportTree) await onImportTree(selectedPath, importJson);
            else await veHttp.importTree(selectedPath, importJson);
            message.success('Import successful');
            setImportOpen(false);
          } catch (err) {
            message.error(`Import failed: ${err}`);
          }
        }}
        width={640}
        okText="Import"
      >
        <Input.TextArea
          value={importJson}
          onChange={(e) => setImportJson(e.target.value)}
          autoSize={{ minRows: 8, maxRows: 20 }}
          placeholder='Paste JSON tree here, e.g. {"name":"root","value":42,"children":[...]}'
          style={{ fontFamily: 'monospace', fontSize: 12 }}
        />
        <div style={{ marginTop: 8 }}>
          <Button
            size="small"
            onClick={() => {
              const input = document.createElement('input');
              input.type = 'file';
              input.accept = '.json';
              input.onchange = () => {
                const file = input.files?.[0];
                if (!file) return;
                const reader = new FileReader();
                reader.onload = () => setImportJson(reader.result as string);
                reader.readAsText(file);
              };
              input.click();
            }}
          >
            Load from file
          </Button>
        </div>
      </Modal>
    </>
  );
}

import { useEffect, useCallback } from 'react';
import { Tree } from 'antd';
import { FolderOutlined, FileOutlined } from '@ant-design/icons';
import type { NodeEntry } from '../store/ve-store';
import type { TreeDataNode } from 'antd';

interface Props {
  nodes: Map<string, NodeEntry>;
  selectedPath: string;
  onSelect: (path: string) => void;
  onExpand: (path: string) => void;
  onLoadChildren: (path: string) => void;
}

function joinPath(parent: string, child: string): string {
  if (!parent) return child;
  return `${parent}/${child}`;
}

function buildTreeData(
  nodes: Map<string, NodeEntry>,
  path: string,
): TreeDataNode[] {
  const entry = nodes.get(path);
  if (!entry || !entry.childNames.length) return [];

  return entry.childNames.map((name) => {
    const childPath = joinPath(path, name);
    const childEntry = nodes.get(childPath);
    const hasChildren = childEntry ? childEntry.childNames.length > 0 : true;

    return {
      key: childPath,
      title: name || '(anonymous)',
      icon: hasChildren ? <FolderOutlined /> : <FileOutlined />,
      isLeaf: childEntry?.loaded ? childEntry.childNames.length === 0 : false,
      children: childEntry?.expanded ? buildTreeData(nodes, childPath) : undefined,
    };
  });
}

export function NodeTreePanel({ nodes, selectedPath, onSelect, onExpand, onLoadChildren }: Props) {
  useEffect(() => {
    onLoadChildren('');
  }, [onLoadChildren]);

  const handleSelect = useCallback((_: unknown, info: { node: TreeDataNode }) => {
    onSelect(info.node.key as string);
  }, [onSelect]);

  const handleExpand = useCallback((
    _: unknown,
    info: { node: TreeDataNode; expanded: boolean },
  ) => {
    const path = info.node.key as string;
    if (info.expanded) {
      onLoadChildren(path);
    }
    onExpand(path);
  }, [onExpand, onLoadChildren]);

  const treeData = buildTreeData(nodes, '');

  return (
    <div style={{ padding: 8 }}>
      <div style={{
        padding: '8px 8px 12px',
        fontSize: 13,
        fontWeight: 600,
        opacity: 0.7,
        textTransform: 'uppercase',
        letterSpacing: 1,
      }}>
        Node Tree
      </div>
      <Tree
        showIcon
        blockNode
        treeData={treeData}
        selectedKeys={selectedPath ? [selectedPath] : []}
        onSelect={handleSelect}
        onExpand={handleExpand}
        style={{ fontSize: 13 }}
      />
    </div>
  );
}

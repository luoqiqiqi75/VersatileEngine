import { useEffect, useCallback } from 'react';
import { useVeStore } from '../store/ve-store';

export function useNodeTree() {
  const init = useVeStore((s) => s.init);
  const connected = useVeStore((s) => s.connected);
  const selectedPath = useVeStore((s) => s.selectedPath);
  const selectedNode = useVeStore((s) => s.selectedNode);
  const treeJson = useVeStore((s) => s.treeJson);
  const nodes = useVeStore((s) => s.nodes);
  const selectNode = useVeStore((s) => s.selectNode);
  const loadChildren = useVeStore((s) => s.loadChildren);
  const toggleExpand = useVeStore((s) => s.toggleExpand);
  const setNodeValue = useVeStore((s) => s.setNodeValue);
  const refreshTree = useVeStore((s) => s.refreshTree);
  const importTree = useVeStore((s) => s.importTree);

  useEffect(() => { init(); }, [init]);

  const navigateTo = useCallback((path: string) => {
    const normalized = path.replace(/^\/+/, '').replace(/\.+/g, '/');
    selectNode(normalized);
    loadChildren(normalized);
  }, [selectNode, loadChildren]);

  return {
    connected,
    selectedPath,
    selectedNode,
    treeJson,
    nodes,
    selectNode,
    loadChildren,
    toggleExpand,
    setNodeValue,
    refreshTree,
    importTree,
    navigateTo,
  };
}

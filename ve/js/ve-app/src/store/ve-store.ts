import { create } from 'zustand';
import { VeHttpClient, VeWsClient } from '@ve/sdk';
import type { TreeNode, VarValue, WsMessage } from '@ve/sdk';

export const veHttp = new VeHttpClient();
export const veWs = new VeWsClient({ url: `ws://${location.host}/ws` });

export interface NodeEntry {
  path: string;
  name: string;
  value: VarValue;
  childNames: string[];
  expanded: boolean;
  loaded: boolean;
}

interface VeState {
  connected: boolean;
  selectedPath: string;
  selectedNode: NodeEntry | null;
  treeJson: TreeNode | null;
  nodes: Map<string, NodeEntry>;

  init: () => void;
  selectNode: (path: string) => Promise<void>;
  loadChildren: (path: string) => Promise<void>;
  toggleExpand: (path: string) => void;
  setNodeValue: (path: string, value: VarValue) => Promise<void>;
  refreshTree: (path?: string) => Promise<void>;
  importTree: (path: string, json: string) => Promise<void>;
}

function isSubPath(changedPath: string, subPath: string): boolean {
  if (!subPath) return true;
  return changedPath === subPath || changedPath.startsWith(subPath + '/');
}

export const useVeStore = create<VeState>((set, get) => ({
  connected: false,
  selectedPath: '',
  selectedNode: null,
  treeJson: null,
  nodes: new Map(),

  init() {
    veWs.onStateChange((connected) => set({ connected }));
    veWs.onMessage((msg: WsMessage) => {
      if ('event' in msg && msg.event === 'node.changed' && msg.path != null) {
        const { selectedPath } = get();
        if (isSubPath(msg.path, selectedPath) || isSubPath(selectedPath, msg.path)) {
          get().refreshTree(selectedPath);
        }
        set((s) => {
          const entry = s.nodes.get(msg.path!);
          if (entry && msg.value !== undefined) {
            const nodes = new Map(s.nodes);
            nodes.set(msg.path!, { ...entry, value: msg.value });
            return { nodes };
          }
          return {};
        });
      }
    });
    veWs.connect();
    get().loadChildren('');
  },

  async selectNode(path: string) {
    const oldPath = get().selectedPath;
    if (oldPath && oldPath !== path) veWs.unsubscribe(oldPath);

    try {
      const [nodeResp, treeJson] = await Promise.all([
        veHttp.getNode(path),
        veHttp.getTree(path),
      ]);
      const childNames = await veHttp.getChildren(path);

      const entry: NodeEntry = {
        path,
        name: path.split('/').pop() || '(root)',
        value: nodeResp.value,
        childNames,
        expanded: true,
        loaded: true,
      };

      set((s) => {
        const nodes = new Map(s.nodes);
        nodes.set(path, entry);
        return { selectedPath: path, selectedNode: entry, treeJson, nodes };
      });

      if (path) veWs.subscribe(path);
    } catch {
      set({ selectedPath: path, selectedNode: null, treeJson: null });
    }
  },

  async loadChildren(path: string) {
    try {
      const childNames = await veHttp.getChildren(path);

      set((s) => {
        const nodes = new Map(s.nodes);
        const existing = nodes.get(path);
        nodes.set(path, {
          path,
          name: path.split('/').pop() || '(root)',
          value: existing?.value ?? null,
          childNames,
          expanded: existing?.expanded ?? (path === ''),
          loaded: true,
        });
        return { nodes };
      });
    } catch { /* server unreachable */ }
  },

  toggleExpand(path: string) {
    set((s) => {
      const nodes = new Map(s.nodes);
      const entry = nodes.get(path);
      if (entry) {
        nodes.set(path, { ...entry, expanded: !entry.expanded });
        if (!entry.expanded && !entry.loaded) {
          get().loadChildren(path);
        }
      }
      return { nodes };
    });
  },

  async setNodeValue(path: string, value: VarValue) {
    await veHttp.setNode(path, value);
    await get().selectNode(path);
  },

  async refreshTree(path = '') {
    const treeJson = await veHttp.getTree(path);
    await get().loadChildren(path);
    set({ treeJson });
  },

  async importTree(path: string, json: string) {
    await veHttp.importTree(path, json);
    await get().selectNode(path);
  },
}));

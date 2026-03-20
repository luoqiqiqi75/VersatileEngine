import { ConfigProvider, Layout, theme } from 'antd';
import { NodeTreePanel } from './components/NodeTreePanel';
import { NodeInspector } from './components/NodeInspector';
import { PathBar } from './components/PathBar';
import { StatusBar } from './components/StatusBar';
import { useNodeTree } from './hooks/useNodeTree';

const { Header, Sider, Content } = Layout;

export default function App() {
  const tree = useNodeTree();

  return (
    <ConfigProvider theme={{ algorithm: theme.darkAlgorithm }}>
      <Layout style={{ height: '100vh' }}>
        <Header style={{
          display: 'flex', alignItems: 'center', gap: 16,
          padding: '0 20px', height: 48, lineHeight: '48px',
        }}>
          <span style={{ fontWeight: 700, fontSize: 15, whiteSpace: 'nowrap' }}>
            VersatileEngine
          </span>
          <PathBar
            value={tree.selectedPath}
            onNavigate={tree.navigateTo}
            onRefresh={() => tree.refreshTree(tree.selectedPath)}
          />
          <StatusBar connected={tree.connected} />
        </Header>

        <Layout>
          <Sider width={320} style={{ overflow: 'auto' }}>
            <NodeTreePanel
              nodes={tree.nodes}
              selectedPath={tree.selectedPath}
              onSelect={tree.selectNode}
              onExpand={tree.toggleExpand}
              onLoadChildren={tree.loadChildren}
            />
          </Sider>

          <Content style={{ overflow: 'auto', padding: 16 }}>
            <NodeInspector
              selectedPath={tree.selectedPath}
              selectedNode={tree.selectedNode}
              treeJson={tree.treeJson}
              onSetValue={tree.setNodeValue}
              onImportTree={tree.importTree}
            />
          </Content>
        </Layout>
      </Layout>
    </ConfigProvider>
  );
}

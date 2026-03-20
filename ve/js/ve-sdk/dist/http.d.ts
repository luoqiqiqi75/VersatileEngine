import type { HealthResponse, NodeResponse, NodeSetResponse, TreeNode, TreeImportResponse, VarValue } from './types';
export declare class VeHttpClient {
    private base;
    constructor(base?: string);
    private request;
    private requestText;
    health(): Promise<HealthResponse>;
    getNode(path?: string): Promise<NodeResponse>;
    setNode(path: string, value: VarValue): Promise<NodeSetResponse>;
    getTree(path?: string): Promise<TreeNode>;
    getChildren(path?: string): Promise<string[]>;
    /** Export subtree as raw JSON string (GET /api/tree) */
    exportTree(path?: string): Promise<string>;
    /** Import JSON into subtree (POST /api/tree) */
    importTree(path: string, json: string): Promise<TreeImportResponse>;
}
//# sourceMappingURL=http.d.ts.map
var c = Object.defineProperty;
var a = (r, e, t) => e in r ? c(r, e, { enumerable: !0, configurable: !0, writable: !0, value: t }) : r[e] = t;
var n = (r, e, t) => a(r, typeof e != "symbol" ? e + "" : e, t);
class h {
  constructor(e = "") {
    n(this, "base");
    this.base = e.replace(/\/$/, "");
  }
  async request(e, t) {
    const s = await fetch(`${this.base}${e}`, t);
    if (!s.ok) {
      const i = await s.text();
      throw new Error(`HTTP ${s.status}: ${i}`);
    }
    return s.json();
  }
  async requestText(e, t) {
    const s = await fetch(`${this.base}${e}`, t);
    if (!s.ok) {
      const i = await s.text();
      throw new Error(`HTTP ${s.status}: ${i}`);
    }
    return s.text();
  }
  async health() {
    return this.request("/health");
  }
  async getNode(e = "") {
    const t = e ? `/${e}` : "";
    return this.request(`/api/node${t}`);
  }
  async setNode(e, t) {
    return this.request(`/api/node/${e}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(t)
    });
  }
  async getTree(e = "") {
    const t = e ? `/${e}` : "";
    return this.request(`/api/tree${t}`);
  }
  async getChildren(e = "") {
    const t = e ? `/${e}` : "";
    return this.request(`/api/children${t}`);
  }
  /** Export subtree as raw JSON string (GET /api/tree) */
  async exportTree(e = "") {
    const t = e ? `/${e}` : "";
    return this.requestText(`/api/tree${t}`);
  }
  /** Import JSON into subtree (POST /api/tree) */
  async importTree(e, t) {
    const s = e ? `/${e}` : "";
    return this.request(`/api/tree${s}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: t
    });
  }
}
class l {
  constructor(e = {}) {
    n(this, "url");
    n(this, "ws", null);
    n(this, "reconnectInterval");
    n(this, "maxReconnectInterval");
    n(this, "currentInterval");
    n(this, "reconnectTimer", null);
    n(this, "intentionalClose", !1);
    n(this, "messageHandlers", /* @__PURE__ */ new Set());
    n(this, "stateHandlers", /* @__PURE__ */ new Set());
    this.url = e.url ?? "ws://localhost:8081", this.reconnectInterval = e.reconnectInterval ?? 1e3, this.maxReconnectInterval = e.maxReconnectInterval ?? 3e4, this.currentInterval = this.reconnectInterval;
  }
  get connected() {
    var e;
    return ((e = this.ws) == null ? void 0 : e.readyState) === WebSocket.OPEN;
  }
  connect() {
    this.intentionalClose = !1, this.createConnection();
  }
  disconnect() {
    var e;
    this.intentionalClose = !0, this.reconnectTimer && (clearTimeout(this.reconnectTimer), this.reconnectTimer = null), (e = this.ws) == null || e.close(), this.ws = null;
  }
  onMessage(e) {
    return this.messageHandlers.add(e), () => this.messageHandlers.delete(e);
  }
  onStateChange(e) {
    return this.stateHandlers.add(e), () => this.stateHandlers.delete(e);
  }
  send(e) {
    !this.ws || this.ws.readyState !== WebSocket.OPEN || this.ws.send(JSON.stringify(e));
  }
  get(e = "") {
    this.send({ cmd: "get", path: e });
  }
  set(e, t) {
    this.send({ cmd: "set", path: e, value: t });
  }
  subscribe(e) {
    this.send({ cmd: "subscribe", path: e });
  }
  unsubscribe(e) {
    this.send({ cmd: "unsubscribe", path: e });
  }
  createConnection() {
    try {
      this.ws = new WebSocket(this.url);
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.ws.onopen = () => {
      this.currentInterval = this.reconnectInterval, this.notifyState(!0);
    }, this.ws.onclose = () => {
      this.notifyState(!1), this.intentionalClose || this.scheduleReconnect();
    }, this.ws.onerror = () => {
      var e;
      (e = this.ws) == null || e.close();
    }, this.ws.onmessage = (e) => {
      try {
        const t = JSON.parse(e.data);
        for (const s of this.messageHandlers) s(t);
      } catch {
      }
    };
  }
  scheduleReconnect() {
    this.reconnectTimer || (this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null, this.currentInterval = Math.min(this.currentInterval * 2, this.maxReconnectInterval), this.createConnection();
    }, this.currentInterval));
  }
  notifyState(e) {
    for (const t of this.stateHandlers) t(e);
  }
}
export {
  h as VeHttpClient,
  l as VeWsClient
};

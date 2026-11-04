"use strict";

var VEService = function(wsUrl) {
    this.address = wsUrl || "ws://127.0.0.1:12100";
    this.transport = null;
    this.pendingRequests = new Map();
    this.subscriptions = new Map();
    this.requestIdCounter = 0;
    this.isConnected = false;
    this.timeout = 3000;
    this.connectTimeout = 5000;
    this._connectPromise = null;
    this._closeTransport = null;
    this._reconnectTimer = null;
    this._reconnectDelay = 1000;
    this._maxReconnectDelay = 8000;
    this._autoReconnect = true;
    this._messageHandlers = new Set();
    this._connectionHandlers = new Set();

    this._generateRequestId = function() {
        return ++this.requestIdCounter;
    };

    this._normalizePath = function(path) {
        return String(path || "").replace(/\./g, "/").replace(/^\/+/, "");
    };

    // ===== Connection management =====

    this.connect = function() {
        var self = this;

        if (this.transport && this.isConnected && this.transport.readyState === WebSocket.OPEN) return Promise.resolve();
        if (this._connectPromise) return this._connectPromise;
        if (this._closeTransport) {
            this._closeTransport(new Error("WebSocket connection closed"));
            return this.connect();
        }

        var retry = this._reconnectTimer !== null;
        clearTimeout(this._reconnectTimer);
        this._reconnectTimer = null;
        this._autoReconnect = true;
        if (typeof WebSocket === "undefined") return Promise.reject(new Error("WebSocket unsupported"));

        var resolveConnect, rejectConnect;
        var promise = new Promise(function(resolve, reject) { resolveConnect = resolve; rejectConnect = reject; });
        var transport = null;
        var timer = null;
        this._connectPromise = promise;

        function close(error) {
            clearTimeout(timer);
            rejectConnect(error);
            if (self._closeTransport !== close) return;
            var wasConnected = self.isConnected;
            self._connectPromise = null;
            self._closeTransport = null;
            self.transport = null;
            self.isConnected = false;
            self.pendingRequests.forEach(function(pending) { pending.reject(error); });
            self.pendingRequests.clear();
            if (transport) transport.close();
            if (self._autoReconnect && (wasConnected || retry)) {
                if (!wasConnected) self._reconnectDelay = Math.min(self._reconnectDelay * 2, self._maxReconnectDelay);
                self._scheduleReconnect();
            }
            if (wasConnected) self._notifyConnection(false);
        }
        this._closeTransport = close;

        try {
            transport = new WebSocket(this.address);
            this.transport = transport;
            timer = setTimeout(function() {
                console.warn("[veService] connection timeout: " + self.address);
                close(new Error("WebSocket connection timeout"));
            }, this.connectTimeout);

            transport.onopen = function() {
                if (self.transport !== transport) { transport.close(); return; }
                clearTimeout(timer);
                self._connectPromise = null;
                self.isConnected = true;
                self._reconnectDelay = 1000;
                console.log("[veService] connected to " + self.address);
                resolveConnect();
                self._resubscribe();
                self._notifyConnection(true);
            };

            transport.onmessage = function(evt) {
                if (self.transport === transport) self._handleMessage(evt.data);
            };
            transport.onerror = function(error) {
                if (self.transport !== transport) return;
                console.error("[veService] WebSocket error:", error);
                close(new Error("WebSocket connection error"));
            };
            transport.onclose = function(evt) {
                if (self.transport !== transport) return;
                console.warn("[veService] connection closed: " + self.address,
                    { code: evt.code, reason: evt.reason, wasClean: evt.wasClean });
                close(new Error("WebSocket connection closed"));
            };
        } catch (error) {
            console.error("[veService] connect failed:", error);
            close(error);
        }
        return promise;
    };

    this.disconnect = function() {
        this._autoReconnect = false;
        if (this._reconnectTimer !== null) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
        this.subscriptions.clear();
        if (this._closeTransport) this._closeTransport(new Error("WebSocket disconnected"));
    };

    this._scheduleReconnect = function() {
        var self = this;
        if (!this._autoReconnect || this._reconnectTimer !== null || this.transport) return;
        console.log("[veService] reconnecting in " + this._reconnectDelay + "ms...");
        this._reconnectTimer = setTimeout(function() {
            // connect() consumes the retry timer, including an explicit retry.
            self.connect().catch(function() {});
        }, this._reconnectDelay);
    };

    // ===== Message handling =====

    this._handleMessage = function(raw) {
        try {
            var message = JSON.parse(raw);
            if (message.event === "node.changed" && message.path !== undefined) {
                this._notifySubscribers(message.path, message.data);
                this._notifyMessage(message);
                return;
            }

            if (message.id !== undefined && this.pendingRequests.has(message.id)) {
                var pending = this.pendingRequests.get(message.id);
                this.pendingRequests.delete(message.id);
                pending.resolve(message);
                return;
            }

            this._notifyMessage(message);
        } catch (e) {
            console.error("[veService] invalid message:", e);
        }
    };

    this._notifySubscribers = function(path, data) {
        if (this.subscriptions.has(path)) {
            var entry = this.subscriptions.get(path);
            entry.callbacks.forEach(function(callback) {
                try {
                    callback(data, path);
                } catch (error) {
                    console.error("[veService] subscription callback error for '" + path + "':", error);
                }
            });
        }
    };

    this._notifyMessage = function(message) {
        this._messageHandlers.forEach(function(handler) {
            try {
                handler(message);
            } catch (error) {
                console.error("[veService] message handler error:", error);
            }
        });
    };

    this._notifyConnection = function(connected) {
        this._connectionHandlers.forEach(function(handler) {
            try {
                handler(connected);
            } catch (error) {
                console.error("[veService] connection handler error:", error);
            }
        });
    };

    this.onMessage = function(handler) {
        this._messageHandlers.add(handler);
        var self = this;
        return function() { self._messageHandlers.delete(handler); };
    };

    this.onConnectionChange = function(handler) {
        this._connectionHandlers.add(handler);
        var self = this;
        return function() { self._connectionHandlers.delete(handler); };
    };

    // ===== Low-level transport =====

    this.send = function(message) {
        var transport = this.transport;
        if (!transport || !this.isConnected || transport.readyState !== WebSocket.OPEN) {
            return Promise.reject(new Error("WebSocket not connected"));
        }

        var self = this;
        return new Promise(function(resolve, reject) {
            var id = self._generateRequestId();
            message.id = id;

            var timer = setTimeout(function() {
                if (self.pendingRequests.has(id)) {
                    self.pendingRequests.delete(id);
                    reject(new Error("Request timeout"));
                }
            }, self.timeout);

            self.pendingRequests.set(id, {
                resolve: function(data) { clearTimeout(timer); resolve(data); },
                reject:  function(err)  { clearTimeout(timer); reject(err); }
            });

            try {
                transport.send(JSON.stringify(message));
            } catch (error) {
                self.pendingRequests.delete(id);
                clearTimeout(timer);
                reject(error);
            }
        });
    };

    this._unwrapReply = function(reply) {
        if (reply.code < 0) {
            throw new Error(reply.message || "error " + reply.code);
        }
        return reply.data;
    };

    // ===== Std operations (op field) =====

    this.get = function(path) {
        return this.send({ op: "get", params: { path: this._normalizePath(path) } })
            .then(this._unwrapReply)
            .then(function(data) { return data ? data.value : undefined; });
    };

    this.set = function(path, value) {
        return this.send({ op: "set", params: { path: this._normalizePath(path), value: value } })
            .then(this._unwrapReply);
    };

    this.export = function(path, depth) {
        if (depth === undefined) { depth = -1; }
        return this.send({ op: "export", params: { path: this._normalizePath(path), depth: depth } })
            .then(this._unwrapReply)
            .then(function(data) { return data ? data.tree : undefined; });
    };

    this.import = function(path, tree, flags) {
        var params = { path: this._normalizePath(path), tree: tree };
        if (flags !== undefined) { params.flags = flags; }
        return this.send({ op: "import", params: params })
            .then(this._unwrapReply);
    };

    this.children = function(path) {
        return this.send({ op: "children", params: { path: this._normalizePath(path) } })
            .then(this._unwrapReply)
            .then(function(data) { return data ? data.children : []; });
    };

    this.erase = function(path) {
        return this.send({ op: "erase", params: { path: this._normalizePath(path) } })
            .then(this._unwrapReply);
    };

    this.trigger = function(path) {
        return this.send({ op: "trigger", params: { path: this._normalizePath(path) } })
            .then(this._unwrapReply);
    };

    this.commands = function() {
        return this.send({ op: "commands", params: {} })
            .then(this._unwrapReply)
            .then(function(data) { return data ? data.commands : []; });
    };

    // ===== Subscriptions =====

    this._resubscribe = function() {
        var self = this;
        this.subscriptions.forEach(function(entry) {
            if (entry.callbacks.size > 0) {
                self.send({ op: "subscribe", params: entry.params }).catch(function() {});
            }
        });
    };

    this.subscribe = function(path, callback, options) {
        if (typeof callback !== "function") {
            callback = function() {};
        }
        options = options || {};

        path = this._normalizePath(path);
        var subParams = { path: path };
        if (options.depth !== undefined) subParams.depth = options.depth;
        if (options.once) subParams.once = true;
        if (options.immediate) subParams.immediate = true;

        if (!this.subscriptions.has(path)) {
            this.subscriptions.set(path, { callbacks: new Set(), params: subParams });
            if (this.transport && this.isConnected) {
                var self2 = this;
                this.send({ op: "subscribe", params: subParams }).then(function(reply) {
                    if (reply.code >= 0 && reply.data != null) {
                        self2._notifySubscribers(path, reply.data);
                    }
                }).catch(function(err) {
                    console.error("[veService] subscribe failed for '" + path + "':", err);
                });
            }
        }

        var entry = this.subscriptions.get(path);
        entry.callbacks.add(callback);

        var self = this;
        return function() { self.unsubscribe(path, callback); };
    };

    this.unsubscribe = function(path, callback) {
        path = this._normalizePath(path);
        if (!this.subscriptions.has(path)) { return; }

        var entry = this.subscriptions.get(path);

        if (callback) {
            entry.callbacks.delete(callback);
            if (entry.callbacks.size === 0) {
                this.subscriptions.delete(path);
                if (this.transport && this.isConnected) {
                    this.send({ op: "unsubscribe", params: { path: path } }).catch(function() {});
                }
            }
        } else {
            this.subscriptions.delete(path);
            if (this.transport && this.isConnected) {
                this.send({ op: "unsubscribe", params: { path: path } }).catch(function() {});
            }
        }
    };

    // ===== User commands (cmd field) =====

    this.run = function(name, args) {
        return this.send({ cmd: name, params: args == null ? {} : args });
    };

    // ===== Batch =====

    this.batch = function(items) {
        return this.send({ batch: items })
            .then(this._unwrapReply);
    };
};

if (typeof veService === "undefined") {
    var veService = new VEService(typeof _ve_ws_url !== "undefined" ? _ve_ws_url : undefined);
    veService.connect().then(function() {
        if (typeof veServiceCallback === "function") {
            veServiceCallback(veService);
        }
    }).catch(function(err) {
        console.error("[veService] auto-connect failed:", err);
    });
}

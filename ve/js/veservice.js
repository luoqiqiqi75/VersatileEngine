"use strict";

var VEService = function(wsUrl) {
    this.address = wsUrl || "ws://127.0.0.1:12100";
    this.transport = null;
    this.pendingRequests = new Map();
    this.subscriptions = new Map();
    this.requestIdCounter = 0;
    this.isConnected = false;
    this.timeout = 3000;
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

        if (this.isConnected) {
            console.warn("[veService] already connected.");
            return Promise.resolve();
        }

        if (!("WebSocket" in window)) {
            console.error("[veService] WebSocket unsupported!");
            return Promise.reject(new Error("WebSocket unsupported"));
        }

        return new Promise(function(resolve, reject) {
            try {
                self.transport = new WebSocket(self.address);

                self.transport.onopen = function() {
                    self.isConnected = true;
                    self._reconnectDelay = 1000;
                    console.log("[veService] connected to " + self.address);
                    self._resubscribe();
                    self._notifyConnection(true);
                    resolve();
                };

                self.transport.onmessage = function(evt) {
                    self._handleMessage(evt.data);
                };

                self.transport.onerror = function(error) {
                    console.error("[veService] WebSocket error:", error);
                    reject(new Error("WebSocket connection error"));
                };

                self.transport.onclose = function() {
                    var wasConnected = self.isConnected;
                    self.isConnected = false;
                    self.pendingRequests.forEach(function(pending) {
                        pending.reject(new Error("WebSocket connection closed"));
                    });
                    self.pendingRequests.clear();
                    self.transport = null;
                    if (wasConnected) {
                        self._notifyConnection(false);
                    }
                    if (wasConnected && self._autoReconnect) {
                        self._scheduleReconnect();
                    }
                };
            } catch (error) {
                console.error("[veService] connect failed:", error);
                reject(error);
            }
        });
    };

    this.disconnect = function() {
        this._autoReconnect = false;
        if (this._reconnectTimer) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
        if (this.transport) {
            this.transport.close();
            this.transport = null;
        }
        this.isConnected = false;
        this.pendingRequests.clear();
        this.subscriptions.clear();
    };

    this._scheduleReconnect = function() {
        var self = this;
        if (this._reconnectTimer) { return; }
        console.log("[veService] reconnecting in " + this._reconnectDelay + "ms...");
        this._reconnectTimer = setTimeout(function() {
            self._reconnectTimer = null;
            self.connect().catch(function() {
                self._reconnectDelay = Math.min(self._reconnectDelay * 2, self._maxReconnectDelay);
                if (self._autoReconnect) {
                    self._scheduleReconnect();
                }
            });
        }, this._reconnectDelay);
    };

    // ===== Message handling =====

    this._handleMessage = function(raw) {
        try {
            var message = JSON.parse(raw);
            if (message.event === "node.changed" && message.path !== undefined) {
                this._notifySubscribers(message.path, message.value);
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
            var callbacks = this.subscriptions.get(path);
            callbacks.forEach(function(callback) {
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
        if (!this.transport || !this.isConnected) {
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
                self.transport.send(JSON.stringify(message));
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
        this.subscriptions.forEach(function(callbacks, path) {
            if (callbacks.size > 0) {
                self.send({ op: "watch", params: { path: path } }).catch(function() {});
            }
        });
    };

    this.watch = function(path, callback, options) {
        if (typeof callback !== "function") {
            callback = function() {};
        }
        options = options || {};
        var immediate = options.immediate || false;

        path = this._normalizePath(path);

        if (!this.subscriptions.has(path)) {
            this.subscriptions.set(path, new Set());
            if (this.transport && this.isConnected) {
                this.send({ op: "watch", params: { path: path } }).catch(function(err) {
                    console.error("[veService] watch failed for '" + path + "':", err);
                });
            }
        }

        var callbacks = this.subscriptions.get(path);
        if (!callbacks.has(callback)) {
            callbacks.add(callback);
        }

        if (immediate) {
            this.get(path).then(function(data) {
                try { callback(data, path); } catch (e) { /* ignore */ }
            }).catch(function() {});
        }

        var self = this;
        return function() { self.unwatch(path, callback); };
    };

    this.unwatch = function(path, callback) {
        path = this._normalizePath(path);
        if (!this.subscriptions.has(path)) { return; }

        var callbacks = this.subscriptions.get(path);

        if (callback) {
            callbacks.delete(callback);
            if (callbacks.size === 0) {
                this.subscriptions.delete(path);
                if (this.transport && this.isConnected) {
                    this.send({ op: "unwatch", params: { path: path } }).catch(function() {});
                }
            }
        } else {
            this.subscriptions.delete(path);
            if (this.transport && this.isConnected) {
                this.send({ op: "unwatch", params: { path: path } }).catch(function() {});
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

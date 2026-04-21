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
        return String(path || "").replace(/^\/+/, "");
    };

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

    this._resubscribe = function() {
        var self = this;
        this.subscriptions.forEach(function(callbacks, path) {
            if (callbacks.size > 0 && self.transport && self.isConnected) {
                try {
                    self.transport.send(JSON.stringify({ op: "subscribe", path: path }));
                } catch (error) {
                    console.error("[veService] resubscribe failed for '" + path + "':", error);
                }
            }
        });
    };

    this._handleMessage = function(raw) {
        try {
            var message = JSON.parse(raw);
            if (message.event === "node.changed" && message.path !== undefined) {
                this._notifySubscribers(message.path, message.value);
                this._notifyMessage(message);
                return;
            }
            if (message.event === "task.result") {
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

    this._unwrapReply = function(reply) {
        if (!reply.ok) {
            throw new Error((reply.code || "error") + ": " + (reply.error || "unknown error"));
        }
        if (reply.accepted) {
            return reply;
        }
        return reply.data;
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
        return function() {
            self._messageHandlers.delete(handler);
        };
    };

    this.onConnectionChange = function(handler) {
        this._connectionHandlers.add(handler);
        var self = this;
        return function() {
            self._connectionHandlers.delete(handler);
        };
    };

    this.call = function(op, payload) {
        if (!this.transport || !this.isConnected) {
            return Promise.reject(new Error("WebSocket not connected"));
        }

        var self = this;
        return new Promise(function(resolve, reject) {
            var id = self._generateRequestId();

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
                var body = payload || {};
                body.op = op;
                body.id = id;
                self.transport.send(JSON.stringify(body));
            } catch (error) {
                self.pendingRequests.delete(id);
                clearTimeout(timer);
                reject(error);
            }
        });
    };

    // ===== Tree operations (default behavior) =====
    this.get = function(path, depth) {
        if (depth === undefined) { depth = -1; }
        return this.call("node.get", { path: this._normalizePath(path), depth: depth }).then(function(reply) {
            if (!reply.ok) {
                throw new Error((reply.code || "error") + ": " + (reply.error || "unknown error"));
            }
            return reply.data.tree || reply.data.value;
        });
    };

    this.set = function(path, tree) {
        return this.call("node.put", { path: this._normalizePath(path), tree: tree }).then(this._unwrapReply);
    };

    // ===== Single value operations =====
    this.val = function(path, value) {
        path = this._normalizePath(path);
        if (arguments.length === 1) {
            return this.call("node.get", { path: path }).then(function(reply) {
                if (!reply.ok) {
                    throw new Error((reply.code || "error") + ": " + (reply.error || "unknown error"));
                }
                return reply.data.value;
            });
        } else {
            return this.call("node.set", { path: path, value: value }).then(this._unwrapReply);
        }
    };

    // ===== Structure operations =====
    this.list = function(path) {
        return this.call("node.list", { path: this._normalizePath(path) }).then(this._unwrapReply);
    };

    this.rm = function(path) {
        return this.call("node.remove", { path: this._normalizePath(path) }).then(this._unwrapReply);
    };

    this.trigger = function(path) {
        return this.call("node.trigger", { path: this._normalizePath(path) }).then(this._unwrapReply);
    };

    // ===== Subscription (tree mode by default) =====
    this.watch = function(path, callback, options) {
        if (typeof callback !== "function") {
            callback = function() {};
        }
        options = options || {};
        var immediate = options.immediate !== undefined ? options.immediate : false;
        var tree = options.tree !== undefined ? options.tree : true;
        var bubble = options.bubble !== undefined ? options.bubble : false;

        path = this._normalizePath(path);

        if (!this.subscriptions.has(path)) {
            this.subscriptions.set(path, new Set());

            if (this.transport && this.isConnected) {
                try {
                    this.transport.send(JSON.stringify({
                        op: "subscribe",
                        path: path,
                        tree: tree,
                        bubble: bubble
                    }));
                } catch (error) {
                    console.error("[veService] subscribe failed for '" + path + "':", error);
                }
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
        return function() {
            self.unwatch(path, callback);
        };
    };

    this.unwatch = function(path, callback) {
        if (!this.subscriptions.has(path)) { return; }

        var callbacks = this.subscriptions.get(path);

        if (callback) {
            callbacks.delete(callback);
            if (callbacks.size === 0) {
                this.subscriptions.delete(path);
                if (this.transport && this.isConnected) {
                    try {
                        this.transport.send(JSON.stringify({ op: "unsubscribe", path: path }));
                    } catch (error) {
                        console.error("[veService] unsubscribe failed for '" + path + "':", error);
                    }
                }
            }
        } else {
            this.subscriptions.delete(path);
            if (this.transport && this.isConnected) {
                try {
                    this.transport.send(JSON.stringify({ op: "unsubscribe", path: path }));
                } catch (error) {
                    console.error("[veService] unsubscribe failed for '" + path + "':", error);
                }
            }
        }
    };

    // ===== Commands =====
    this.run = function(name, args, wait) {
        if (wait === undefined) { wait = true; }
        return this.call("command.run", {
            name: name,
            args: args == null ? [] : args,
            wait: wait
        });
    };

    this.cmds = function() {
        return this.call("command.list", {}).then(this._unwrapReply);
    };

    // ===== Batch operations =====
    this.batch = function(items) {
        return this.call("batch", { items: items }).then(function(reply) {
            if (!reply.ok) {
                throw new Error((reply.code || "error") + ": " + (reply.error || "unknown error"));
            }
            return reply.data.items;
        });
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

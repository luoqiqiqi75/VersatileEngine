"use strict";

var VEService = function(wsUrl) {
    this.address = wsUrl || "ws://127.0.0.1:8081";
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

    this._generateRequestId = function() {
        return ++this.requestIdCounter;
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
                    self.transport.send(JSON.stringify({ cmd: "subscribe", path: path }));
                } catch (error) {
                    console.error("[veService] resubscribe failed for '" + path + "':", error);
                }
            }
        });
    };

    this._handleMessage = function(raw) {
        try {
            var message = JSON.parse(raw);

            if (message.id !== undefined && this.pendingRequests.has(message.id)) {
                var pending = this.pendingRequests.get(message.id);
                this.pendingRequests.delete(message.id);
                if (message.type === "error") {
                    pending.reject(new Error(message.msg || "unknown error"));
                } else {
                    pending.resolve(message.value !== undefined ? message.value : message);
                }
                return;
            }

            if (message.type === "event" && message.path !== undefined) {
                this._notifySubscribers(message.path, message.value);
                return;
            }
        } catch (e) {
            console.error("[veService] invalid message:", e);
        }
    };

    this._notifySubscribers = function(path, data) {
        if (this.subscriptions.has(path)) {
            var callbacks = this.subscriptions.get(path);
            callbacks.forEach(function(callback) {
                try {
                    callback(data);
                } catch (error) {
                    console.error("[veService] subscription callback error for '" + path + "':", error);
                }
            });
        }
    };

    this.get = function(path) {
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
                self.transport.send(JSON.stringify({ cmd: "get", path: path, id: id }));
            } catch (error) {
                self.pendingRequests.delete(id);
                clearTimeout(timer);
                reject(error);
            }
        });
    };

    this.set = function(path, value) {
        if (!this.transport || !this.isConnected) {
            console.error("[veService] WebSocket not connected");
            return;
        }

        try {
            this.transport.send(JSON.stringify({ cmd: "set", path: path, value: value }));
        } catch (error) {
            console.error("[veService] set failed for '" + path + "':", error);
        }
    };

    this.trigger = function(path) {
        this.set(path, null);
    };

    this.subscribe = function(path, callback, immediateGet) {
        if (typeof callback !== "function") {
            console.error("[veService] subscribe callback must be a function");
            return function() {};
        }

        if (!this.subscriptions.has(path)) {
            this.subscriptions.set(path, new Set());

            if (this.transport && this.isConnected) {
                try {
                    this.transport.send(JSON.stringify({ cmd: "subscribe", path: path }));
                } catch (error) {
                    console.error("[veService] subscribe failed for '" + path + "':", error);
                }
            }
        }

        var callbacks = this.subscriptions.get(path);
        if (!callbacks.has(callback)) {
            callbacks.add(callback);
        }

        if (immediateGet) {
            this.get(path).then(function(data) {
                try { callback(data); } catch (e) { /* ignore */ }
            }).catch(function() {});
        }

        var self = this;
        return function() {
            self.unsubscribe(path, callback);
        };
    };

    this.unsubscribe = function(path, callback) {
        if (!this.subscriptions.has(path)) { return; }

        var callbacks = this.subscriptions.get(path);

        if (callback) {
            callbacks.delete(callback);
            if (callbacks.size === 0) {
                this.subscriptions.delete(path);
                if (this.transport && this.isConnected) {
                    try {
                        this.transport.send(JSON.stringify({ cmd: "unsubscribe", path: path }));
                    } catch (error) {
                        console.error("[veService] unsubscribe failed for '" + path + "':", error);
                    }
                }
            }
        } else {
            this.subscriptions.delete(path);
            if (this.transport && this.isConnected) {
                try {
                    this.transport.send(JSON.stringify({ cmd: "unsubscribe", path: path }));
                } catch (error) {
                    console.error("[veService] unsubscribe failed for '" + path + "':", error);
                }
            }
        }
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

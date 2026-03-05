// ----------------------------------------------------------------------------
// command_service.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "asio2/asio2.hpp"

#include <veCommon>

#include "ve/core/module.h"

namespace ve::service {

class CommandServer
{
public:
    using SessionPtr = std::shared_ptr<asio2::tcp_session>;

public:
    explicit CommandServer(ve::Data* d);
    virtual ~CommandServer();

    void start();
    void stop();

private:
    void onSessionRecv(SessionPtr& session_ptr, std::string_view s);

    void onClientConnected(SessionPtr& session_ptr);
    void onClientDisconnected(SessionPtr& session_ptr);

private:
    Data* _d = nullptr;
    asio2::tcp_server _server;
    UnorderedHashMap<std::size_t, Data*> _context_map;
};

class CommandServerModule : public Module
{
public:
    explicit CommandServerModule();
    virtual ~CommandServerModule();

    CommandServer* server() const { return _cs; }
    void setServer(CommandServer* s) { _cs = s; }

private:
    void init() override;
    void ready() override;
    void deinit() override;

private:
    CommandServer* _cs = nullptr;
};

}

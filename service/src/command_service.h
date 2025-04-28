// ----------------------------------------------------------------------------
// command_service.h
// ----------------------------------------------------------------------------
// This file is part of Versatile Engine
// ----------------------------------------------------------------------------
// Copyright (c) 2023 - 2023 Thilo, LuoQi, Qi Lu.
// Copyright (c) 2023 - 2023 Versatile Engine contributors (cf. AUTHORS.md)
//
// This file may be used under the terms of the GNU General Public License
// version 3.0 as published by the Free Software Foundation and appearing in
// the file LICENSE included in the packaging of this file.  Please review the
// following information to ensure the GNU General Public License version 3.0
// requirements will be met: http://www.gnu.org/copyleft/gpl.html.
//
// If you do not wish to use this file under the terms of the GPL version 3.0
// then you may purchase a commercial license. For more information contact
// <luoqiqiqi75@sina.com>.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
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
    HashMap<std::size_t, Data*> _context_map;
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

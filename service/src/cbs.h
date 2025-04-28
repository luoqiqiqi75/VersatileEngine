// ----------------------------------------------------------------------------
// cbs.h
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

#include "ve/core/module.h"
#include "ve/core/common.h"

namespace ve::service {

using SessionPtr = std::shared_ptr<asio2::tcp_session>;
using FlagT = uint8_t;
using IdT   = uint16_t;

enum HeadFlag : FlagT {
    m_request       = 0x00,
    m_response      = 0x10,
    m_notify        = 0x20,
    m_error         = 0x30,

    l_short         = 0x00, // no Byte
    l_medium        = 0x40, // 2 Byte length
    l_long          = 0x80, // 4 Byte length

    c_echo          = 0b0010,
    c_pub           = 0b0100,
    c_sub           = 0b0110,

    o_recursive     = 0b0001,

    g_advanced      = 0b1000,

    s_added         = 0b1001,
    s_removed       = 0b1010,

    s_reordered     = 0b1011,

    s_gonna_insert  = 0b1101,
    s_gonna_remove  = 0b1110,

    s_activated     = 0b1111,

    c_cancel        = 0x0
};

class CBSServer
{
public:
    explicit CBSServer(ve::Data* d);
    ~CBSServer();

    void start();
    void stop();

private:
    void onSessionRecv(SessionPtr& session_ptr, std::string_view s);

    void onClientConnected(SessionPtr& session_ptr);
    void onClientDisconnected(SessionPtr& session_ptr);

private:
    Data* _d = nullptr;
    asio2::tcp_server _server;
    HashMap<std::size_t, QByteArray> _cache_map;
};

class CBSClient
{
public:
    explicit CBSClient(ve::Data* d);
    ~CBSClient();

    Data* data() const { return _d; }

    void connectToHost();
    void disconnectFromHost();

    template<HeadFlag C, bool R> void execRequest(const std::string& remote_path, ve::Data* local_data);
    template<bool R> void execPublish(const std::string& remote_path, ve::Data* local_data);
    void execUnsubscribe(const std::string& remote_path);

private:
    void onRecv(std::string_view s);

    void onConnected();
    void onDisconnected();

private:
    Data* _d = nullptr;
    asio2::tcp_client _client;
    std::mutex _mutex, _cm;
    std::condition_variable_any _cva;
    HashMap<IdT, std::pair<std::string, Data*>> _map;
    List<std::pair<std::string, Data*>> _pending;
    QByteArray _cache;
};

class CBSModule : public Module
{
public:
    CBSModule();
    ~CBSModule();

private:
    void init() override;
    void ready() override;
    void deinit() override;
};


}
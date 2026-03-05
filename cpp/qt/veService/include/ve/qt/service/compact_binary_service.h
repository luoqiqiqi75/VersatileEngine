// ----------------------------------------------------------------------------
// compact_binary_service.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include <veCommon>

/**
 * Compact Binary Service (CBS)
 */

namespace ve::server::cbs {

VE_API Data* defaultContextD();

VE_API Data* start(ve::Data* context = defaultContextD());
VE_API Data* stop(ve::Data* context = defaultContextD());

}

namespace ve::client::cbs {

VE_API Data* defaultContextD();

VE_API Data* connectTo(Data* context = defaultContextD());
VE_API Data* connectTo(Data* context, const std::string& ip, int port);
inline Data* connectTo(const std::string& ip, int port)
{ return connectTo(defaultContextD(), ip, port); }

VE_API Data* disconnectFrom(Data* context = defaultContextD());

VE_API Data* defaultLocalD(Data* context, const std::string& remote_path);

enum DataStructureType : int {
    single      = 1,
    recursive   = 2
};

template<DataStructureType DST = single> VE_API Data* echo(Data* context, const std::string& remote_path, Data* local_data);
template<DataStructureType DST = single> inline Data* echo(const std::string& remote_path, Data* local_data)
{ return echo<DST>(defaultContextD(), remote_path, local_data); }
template<DataStructureType DST = single> inline Data* echo(const std::string& remote_path)
{ return echo<DST>(defaultContextD(), remote_path, defaultLocalD(defaultContextD(), remote_path)); }

template<DataStructureType DST = single> VE_API Data* publish(Data* context, const std::string& remote_path, Data* local_data);
template<DataStructureType DST = single> inline Data* publish(const std::string& remote_path, Data* local_data)
{ return publish<DST>(defaultContextD(), remote_path, local_data); }
template<DataStructureType DST = single> inline Data* publish(const std::string& remote_path)
{ return publish<DST>(defaultContextD(), remote_path, defaultLocalD(defaultContextD(), remote_path)); }
template<DataStructureType DST = single> inline Data* publish(const std::string& remote_path, const QVariant& var)
{
    auto ld = defaultLocalD(defaultContextD(), remote_path);
    ld->importFromVariant(nullptr, var);
    return publish<DST>(defaultContextD(), remote_path, ld);
}

template<DataStructureType DST = single> VE_API Data* subscribe(Data* context, const std::string& remote_path, Data* local_data);
template<DataStructureType DST = single> inline Data* subscribe(const std::string& remote_path, Data* local_data)
{ return subscribe<DST>(defaultContextD(), remote_path, local_data); }
template<DataStructureType DST = single> inline Data* subscribe(const std::string& remote_path)
{ return subscribe<DST>(defaultContextD(), remote_path, defaultLocalD(defaultContextD(), remote_path)); }


VE_API Data* unsubscribe(Data* context, const std::string& remote_path);
inline Data* unsubscribe(const std::string& remote_path)
{ return unsubscribe(defaultContextD(), remote_path); }

}

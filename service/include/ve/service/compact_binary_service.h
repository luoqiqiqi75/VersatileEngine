// ----------------------------------------------------------------------------
// compact_binary_service.h
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

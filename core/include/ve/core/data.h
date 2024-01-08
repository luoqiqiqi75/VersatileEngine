// ----------------------------------------------------------------------------
// data.h
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

#include "ve/global.h"

#include "imol/core/modulemanager.h"

#define VE_DATA_PATH_SEPARATOR "."

#define PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH_WITH_CONTEXT(Func) \
template<typename... Ts> inline Data* Func(QObject* context, const QString& path, const QString& sub_path, Ts&&... other_paths) \
{ return Func(context, path + VE_DATA_PATH_SEPARATOR + sub_path, std::forward<Ts>(other_paths)...); }
#define PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH(Func) \
template<typename... Ts> inline Data* Func(const QString& path, const QString& sub_path, Ts&&... other_paths) \
{ return Func(path + VE_DATA_PATH_SEPARATOR + sub_path, std::forward<Ts>(other_paths)...); }

namespace ve {

using Data = imol::ModuleObject;
using DataListener = imol::ModuleObject;

namespace data {

using Manager = imol::ModuleManager;
VE_API Manager& manager();

VE_API Data* create(QObject* context, const QString& path);
PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH_WITH_CONTEXT(create)
VE_API bool free(QObject* context, const QString& path);
PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH_WITH_CONTEXT(free)

VE_API Data* at(const QString& path);
PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH(at)

VE_API DataListener* listenerAt(const QString& path);
PRIVATE_VE_DATA_DECALRE_MULTIPLE_SUB_PATH(listenerAt)

}

Data* d(const QString& path);

}

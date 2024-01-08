// ----------------------------------------------------------------------------
// common.h
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

#include <QJsonValue>

#include "data.h"
#include "log.h"
#include "factory.h"
#include "terminal.h"

namespace ve {

VE_API QObject* global();

namespace version {
typedef imol::CreatorManager<int, int(void)> Manager;
VE_API Manager& manager();
VE_API int number(const QString &key, bool sum = false);
VE_API QString releaseString(const QString &key);
}

namespace entry {
VE_API void setup(const QString& cfg_path);
VE_API void init();
VE_API void deinit();
}

}

#define VE_REGISTER_VERSION(KEY, VER) VE_AUTO_RUN(ve::version::manager().regist(KEY, [] (void) -> int { return VER; }))

#define VE_REGISTER_RELEASE_VERSION(KEY, MAJOR, MINOR, BASELINE) VE_AUTO_RUN( \
ve::version::manager().regist(QString("@0_%1").arg(KEY), [] (void) -> int { return BASELINE; }); \
ve::version::manager().regist(QString("@1_%1").arg(KEY), [] (void) -> int { return MAJOR; }); \
ve::version::manager().regist(QString("@2_%1").arg(KEY), [] (void) -> int { return MINOR; }))

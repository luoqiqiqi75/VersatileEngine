// ----------------------------------------------------------------------------
// object.h — ve::Object (signal/slot, thread-safe)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"
#include "loop.h"

constexpr const char* VE_UNDEFINED_OBJECT_NAME = "@undefined";

namespace ve {

/**
* @brief Object — minimal controllable entity with signal/slot
*
* Thread-safe signal connection and dispatch.
* Callbacks are invoked outside the lock (no deadlock in trigger).
*
* Dispatch priority for trigger():
*   1. per-connection LoopRef  (if set in connect())
*   2. global default LoopRef  (if set via loop::setDefault())
*   3. direct call             (zero-overhead, default for pure C++)
*/
class VE_API Object
{
public:
    using SignalT = int;
    using ActionT = std::function<void()>;

    using MutexT = std::recursive_mutex;
    using LockT = std::lock_guard<MutexT>;

    explicit Object(const std::string& name = "");
    ~Object();

    const std::string& name() const;
    MutexT& mutex() const;

    enum Signal : SignalT { OBJECT_DELETED = 0xffff };

    bool hasConnection(SignalT signal, Object* observer);
    void connect(SignalT signal, Object* observer, const ActionT& action, LoopRef loop = {});
    void disconnect(SignalT signal, Object* observer);
    void disconnect(Object* observer);

    void trigger(SignalT signal);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

/**
* @brief Manager Class is a convenient object container
*/
class VE_API Manager : public Object, public Hash<Object*>
{
public:
    explicit Manager(const std::string& name);
    virtual ~Manager();

    Object* add(Object* obj, bool delete_if_failed = false);
    template<typename SubObj> std::enable_if_t<std::is_base_of_v<Object, SubObj>, SubObj*> add(SubObj* obj, bool delete_if_failed = false)
    { return add(static_cast<Object *>(obj), delete_if_failed) ? obj : nullptr; }

    bool remove(Object* obj, bool auto_delete = true);
    bool remove(const std::string& name, bool auto_delete = true);

    Object* get(const std::string& key) const;
    template<class SubObj> std::enable_if_t<std::is_base_of_v<Object, SubObj>, SubObj*> get(const std::string& key) const
    { return static_cast<SubObj*>(get(key)); }
};

} // namespace ve

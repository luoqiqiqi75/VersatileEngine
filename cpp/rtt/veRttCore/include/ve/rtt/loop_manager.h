#pragma once

#include <ve/rtt/loop_object.h>

namespace imol {

namespace loop {

inline Manager& mgr() {
    static Manager s_mgr("_imol_loop_mgr");
    return s_mgr;
}

LoopObject* get(const std::string& name, bool default_pool = true);

inline LoopObject* createPool(const std::string& name, int threads = 2) {
    auto* obj = new PoolLoopObject(name, threads);
    obj->start();
    mgr().add(obj);
    return obj;
}

} // namespace loop

namespace global {

inline LoopObject*& loop_main() { static LoopObject* ptr = nullptr; return ptr; }
inline LoopObject*& loop_rt()   { static LoopObject* ptr = nullptr; return ptr; }
inline LoopObject*& loop_calc() { static LoopObject* ptr = nullptr; return ptr; }

} // namespace global

} // namespace imol

// ----------------------------------------------------------------------------
// test_manager.cpp — ve::Manager object container
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

VE_TEST(manager_add_get) {
    Manager mgr("mgr");
    auto* obj = new Object("child");
    mgr.add(obj);
    VE_ASSERT_EQ(mgr.get("child"), obj);
    VE_ASSERT_EQ(obj->parent(), &mgr);
}

VE_TEST(manager_add_nullptr) {
    Manager mgr("mgr");
    VE_ASSERT(mgr.add(static_cast<Object*>(nullptr)) == nullptr);
}

VE_TEST(manager_add_duplicate) {
    Manager mgr("mgr");
    mgr.add(new Object("x"));
    auto* dup = new Object("x");
    auto* result = mgr.add(dup, true);  // delete_if_failed=true
    VE_ASSERT(result == nullptr);
    // dup should have been deleted — don't access it
}

VE_TEST(manager_remove_by_name) {
    Manager mgr("mgr");
    mgr.add(new Object("a"));
    VE_ASSERT(mgr.get("a") != nullptr);
    mgr.remove("a");
    VE_ASSERT(mgr.get("a") == nullptr);
}

VE_TEST(manager_remove_by_object) {
    Manager mgr("mgr");
    auto* obj = new Object("b");
    mgr.add(obj);
    mgr.remove(obj);
    VE_ASSERT(mgr.get("b") == nullptr);
}

VE_TEST(manager_remove_nonexistent) {
    Manager mgr("mgr");
    VE_ASSERT(!mgr.remove("ghost"));
}

VE_TEST(manager_destructor_deletes_children) {
    // We can't directly check deletion, but we can verify no crash
    auto* mgr = new Manager("mgr");
    mgr->add(new Object("c1"));
    mgr->add(new Object("c2"));
    delete mgr;  // should delete c1, c2 without crash
}

VE_TEST(manager_size) {
    Manager mgr("mgr");
    mgr.add(new Object("a"));
    mgr.add(new Object("b"));
    VE_ASSERT_EQ(mgr.sizeAsInt(), 2);
}

VE_TEST(manager_get_subclass) {
    struct SubObj : Object {
        SubObj() : Object("sub") {}
        int extra = 42;
    };

    Manager mgr("mgr");
    mgr.add(new SubObj());
    auto* s = mgr.get<SubObj>("sub");
    VE_ASSERT(s != nullptr);
    VE_ASSERT_EQ(s->extra, 42);
}

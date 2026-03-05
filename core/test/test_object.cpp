// ----------------------------------------------------------------------------
// test_object.cpp — ve::Object lifecycle + signal/slot
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

VE_TEST(object_default_name) {
    Object obj;
    VE_ASSERT_EQ(obj.name(), std::string(VE_UNDEFINED_OBJECT_NAME));
}

VE_TEST(object_custom_name) {
    Object obj("sensor");
    VE_ASSERT_EQ(obj.name(), "sensor");
}

VE_TEST(object_parent) {
    Object parent("p");
    Object child("c");
    child.setParent(&parent);
    VE_ASSERT_EQ(child.parent(), &parent);
}

VE_TEST(object_no_parent) {
    Object obj;
    VE_ASSERT(obj.parent() == nullptr);
}

// --- signal/slot ---

VE_TEST(object_connect_trigger) {
    Object src("src");
    Object obs("obs");
    int count = 0;
    src.connect(1, &obs, [&] { count++; });
    src.trigger(1);
    VE_ASSERT_EQ(count, 1);
    src.trigger(1);
    VE_ASSERT_EQ(count, 2);
}

VE_TEST(object_trigger_no_connection) {
    Object src;
    src.trigger(999);  // should not crash
}

VE_TEST(object_multiple_observers) {
    Object src;
    Object obs1("o1"), obs2("o2");
    int c1 = 0, c2 = 0;
    src.connect(1, &obs1, [&] { c1++; });
    src.connect(1, &obs2, [&] { c2++; });
    src.trigger(1);
    VE_ASSERT_EQ(c1, 1);
    VE_ASSERT_EQ(c2, 1);
}

VE_TEST(object_disconnect_signal_observer) {
    Object src;
    Object obs;
    int count = 0;
    src.connect(1, &obs, [&] { count++; });
    src.disconnect(1, &obs);
    src.trigger(1);
    VE_ASSERT_EQ(count, 0);
}

VE_TEST(object_disconnect_all_from_observer) {
    Object src;
    Object obs;
    int c1 = 0, c2 = 0;
    src.connect(1, &obs, [&] { c1++; });
    src.connect(2, &obs, [&] { c2++; });
    src.disconnect(&obs);
    src.trigger(1);
    src.trigger(2);
    VE_ASSERT_EQ(c1, 0);
    VE_ASSERT_EQ(c2, 0);
}

VE_TEST(object_hasConnection) {
    Object src;
    Object obs;
    VE_ASSERT(!src.hasConnection(1, &obs));
    src.connect(1, &obs, [] {});
    VE_ASSERT(src.hasConnection(1, &obs));
    src.disconnect(1, &obs);
    VE_ASSERT(!src.hasConnection(1, &obs));
}

VE_TEST(object_deleted_signal) {
    int count = 0;
    Object obs;
    {
        Object src("temp");
        src.connect(Object::OBJECT_DELETED, &obs, [&] { count++; });
    }
    // src destroyed — OBJECT_DELETED should have fired
    VE_ASSERT_EQ(count, 1);
}

// ----------------------------------------------------------------------------
// test_data.cpp — ve::AnyData<T> + DataManager
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/data.h"

using namespace ve;

VE_TEST(anydata_int_get_set) {
    AnyData<int> d(42);
    VE_ASSERT_EQ(d.get(), 42);
    d.set(100);
    VE_ASSERT_EQ(d.get(), 100);
}

VE_TEST(anydata_string_get_set) {
    AnyData<std::string> d("hello");
    VE_ASSERT_EQ(d.get(), "hello");
    d.set("world");
    VE_ASSERT_EQ(d.get(), "world");
}

VE_TEST(anydata_ref_ptr) {
    AnyData<int> d(5);
    d.ref() = 10;
    VE_ASSERT_EQ(d.get(), 10);
    *d.ptr() = 20;
    VE_ASSERT_EQ(d.get(), 20);
}

VE_TEST(anydata_void_update) {
    AnyData<void> d;
    // void data's update just triggers signals, should not crash
    d.control(AbstractData::CHANGEABLE);
    VE_ASSERT(d.update());
}

VE_TEST(anydata_update_triggers_signal) {
    AnyData<int> d(0);
    d.control(AbstractData::CHANGEABLE);
    int count = 0;
    d.listener()->connect(DATA_CHANGED, d.listener(), [&] { count++; });
    d.update(42);
    VE_ASSERT_EQ(count, 1);
    VE_ASSERT_EQ(d.get(), 42);
}

VE_TEST(anydata_updateIfDifferent) {
    AnyData<int> d(10);
    d.control(AbstractData::CHANGEABLE);
    int count = 0;
    d.listener()->connect(DATA_CHANGED, d.listener(), [&] { count++; });

    d.updateIfDifferent(10);  // same value
    VE_ASSERT_EQ(count, 0);

    d.updateIfDifferent(20);  // different value
    VE_ASSERT_EQ(count, 1);
    VE_ASSERT_EQ(d.get(), 20);
}

VE_TEST(anydata_bind) {
    AnyData<int> d(5);
    d.control(AbstractData::CHANGEABLE);
    int bound = 0;
    d.bind(&bound);
    VE_ASSERT_EQ(bound, 5);

    d.update(99);
    VE_ASSERT_EQ(bound, 99);
}

VE_TEST(anydata_dataType) {
    AnyData<int> di(0);
    VE_ASSERT(!di.dataType().empty());

    AnyData<void> dv;
    VE_ASSERT(!dv.dataType().empty());
}

// --- DataManager ---

VE_TEST(datamanager_insert_find) {
    DataManager mgr;
    auto* d = new AnyData<int>(42);
    mgr.insert("test.value", d);
    VE_ASSERT_EQ(mgr.find("test.value"), d);
}

VE_TEST(datamanager_find_missing) {
    DataManager mgr;
    VE_ASSERT(mgr.find("nonexistent") == nullptr);
}

VE_TEST(datamanager_remove) {
    DataManager mgr;
    mgr.insert("x", new AnyData<int>(1));
    VE_ASSERT(mgr.find("x") != nullptr);
    mgr.remove("x");
    VE_ASSERT(mgr.find("x") == nullptr);
}

VE_TEST(datamanager_keys) {
    DataManager mgr;
    mgr.insert("a.1", new AnyData<int>(1));
    mgr.insert("a.2", new AnyData<int>(2));
    mgr.insert("b.1", new AnyData<int>(3));

    auto all = mgr.keys();
    VE_ASSERT_EQ(all.sizeAsInt(), 3);

    auto prefixed = mgr.keys("a.");
    VE_ASSERT_EQ(prefixed.sizeAsInt(), 2);
}

VE_TEST(datamanager_insertNew_dedup) {
    DataManager mgr;
    bool ok1 = false, ok2 = false;
    auto* d1 = mgr.insertNew("k", new AnyData<int>(1), &ok1);
    auto* d2 = mgr.insertNew("k", new AnyData<int>(2), &ok2);
    VE_ASSERT(ok1);
    VE_ASSERT(!ok2);
    VE_ASSERT_EQ(d1, d2);  // second call returns existing
}

// --- DataList / DataDict ---

VE_TEST(datalist_appendRaw) {
    DataList list;
    list.appendRaw(1, 2.0, std::string("three"));
    VE_ASSERT_EQ(list.sizeAsInt(), 3);
    VE_ASSERT_EQ(list.dataAt<int>(0)->get(), 1);
    VE_ASSERT_EQ(list.getAt<std::string>(2), "three");
}

VE_TEST(datadict_insertRaw) {
    DataDict dict;
    dict.insertRaw("name", std::string("Alice"));
    dict.insertRaw("age", 30);
    VE_ASSERT_EQ(dict.sizeAsInt(), 2);
    VE_ASSERT_EQ(dict.getAt<std::string>("name"), "Alice");
}

// --- global data API ---

VE_TEST(data_create_get) {
    bool ok = false;
    data::create("_test.val", 42, &ok);
    VE_ASSERT(ok);
    VE_ASSERT_EQ(data::get<int>("_test.val"), 42);

    // cleanup
    globalDataManager().remove("_test.val");
}

VE_TEST(data_createTrigger) {
    int count = 0;
    auto* d = data::createTrigger("_test.trigger", [&] { count++; });
    VE_ASSERT(d != nullptr);

    d->update();
    VE_ASSERT_EQ(count, 1);

    // cleanup
    globalDataManager().remove("_test.trigger");
}

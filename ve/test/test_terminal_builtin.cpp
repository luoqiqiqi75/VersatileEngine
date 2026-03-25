// test_terminal_builtin.cpp - terminal builtin command tests

#include "ve_test.h"
#include "ve/core/command.h"
#include "ve/core/node.h"
#include "../src/service/terminal_builtins.h"

using namespace ve;

namespace {

static const char* kTerminalTestRoot = "__terminal_builtin_test__";

Node* resetTerminalTestRoot()
{
    auto* root = ve::node::root();
    root->remove(kTerminalTestRoot);
    return root->append(kTerminalTestRoot);
}

Var makeArgs(std::initializer_list<const char*> args)
{
    Var::ListV list;
    for (const char* s : args)
        list.push_back(Var(std::string(s)));
    return Var(std::move(list));
}

} // namespace

VE_TEST(terminal_builtin_cp_registered_and_iter_removed) {
    terminalBuiltinsEnsureRegistered();
    VE_ASSERT(command::has("cp"));
    VE_ASSERT(!command::has("iter"));
}

VE_TEST(terminal_builtin_cp_copies_full_tree) {
    terminalBuiltinsEnsureRegistered();
    Node* base = resetTerminalTestRoot();

    Node* src = base->append("src");
    src->set(1);
    src->append("keep")->set(7);
    src->append("dup")->set(10);
    src->append("dup")->set(20);

    Node* dst = base->append("dst");
    dst->set(0);
    dst->append("keep")->set(-1);
    dst->append("extra")->set(99);

    Result r = command::call("cp", makeArgs({"/__terminal_builtin_test__/dst", "/__terminal_builtin_test__/src", "--remove"}));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(dst->getInt(), 1);
    VE_ASSERT_EQ(dst->child("keep")->getInt(), 7);
    VE_ASSERT_EQ(dst->count("dup"), 2);
    VE_ASSERT_EQ(dst->child("dup", 0)->getInt(), 10);
    VE_ASSERT_EQ(dst->child("dup", 1)->getInt(), 20);
    VE_ASSERT(!dst->has("extra"));

    ve::node::root()->remove(kTerminalTestRoot);
}

VE_TEST(terminal_builtin_cp_no_insert_maps_to_copy_flag) {
    terminalBuiltinsEnsureRegistered();
    Node* base = resetTerminalTestRoot();

    Node* src = base->append("src");
    src->append("keep")->set(7);
    src->append("add")->set(3);

    Node* dst = base->append("dst");
    dst->append("keep")->set(-1);

    Result r = command::call("cp", makeArgs({"/__terminal_builtin_test__/dst", "/__terminal_builtin_test__/src", "--no-insert"}));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(dst->count(), 1);
    VE_ASSERT_EQ(dst->child("keep")->getInt(), 7);
    VE_ASSERT(!dst->has("add"));

    ve::node::root()->remove(kTerminalTestRoot);
}

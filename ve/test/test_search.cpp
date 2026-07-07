#include "ve_test.h"

#include "../src/service/node_commands.h"
#include "../src/service/cmd_commands.h"

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <ve/core/factory.h>
#include <ve/core/schema.h>
#include <ve/core/pipeline.h>
#include <ve/core/res.h>

using namespace ve;

VE_SETUP(search) {
    auto& f = factory::at("cmd");
    schema::JsonS::toNode(f.node(), std::string(res::read("ve/service/cmd.json")));
    service::registerCmdCommands(f);
}

// Envelope dispatch mirror of test_node_protocol runEnvelope, kept file-local
// so this test file compiles and runs independently.
static bool runEnvelope(service::Session* session, Pipeline& pipe)
{
    pipe.contextNode()->set("_session", Var::ptr(session));
    auto ref = service::resolveCmd(pipe.contextNode());
    if (!ref.factory) {
        pipe.contextNode()->set("code", int64_t(ref.key.empty() ? service::ERR_INVALID : service::ERR_NOT_FOUND));
        pipe.contextNode()->set("message", ref.key.empty() ? std::string("op or cmd required") : "unknown: " + ref.key);
        return true;
    }
    Command* c = pipe.add(command::create(*ref.factory, ref.key));
    c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
    pipe.sync();
    return service::finalizeReply(pipe);
}

VE_TEST(search_missing_pattern_returns_err_invalid) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(0), int(service::ERR_INVALID));
}

VE_TEST(search_registered_and_empty_call_ok) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "anything");
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
}

static Node* mkTree(Node& root)
{
    root.at("config/port")->set(int64_t(8080));
    root.at("config/host")->set(std::string("localhost"));
    root.at("config/nested/config")->set(int64_t(1));
    root.at("logs/error")->set(std::string("boom"));
    root.at("logs/info")->set(std::string("hi"));
    return &root;
}

VE_TEST(search_contains_matches_by_name) {
    Node root("root");
    mkTree(root);
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "config");
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    Node* matches = pipe.contextNode()->find("data/matches");
    VE_ASSERT(matches != nullptr);
    // "config" (top-level) + "config/nested/config" (leaf)
    VE_ASSERT_EQ(matches->count(), 2);
    VE_ASSERT_EQ(matches->child(0)->getString(), std::string("config"));
    VE_ASSERT_EQ(matches->child(1)->getString(), std::string("config/nested/config"));
    VE_ASSERT_EQ(pipe.contextNode()->get("data/count").toInt(-1), 2);
}

VE_TEST(search_contains_case_insensitive_by_default) {
    Node root("root");
    root.at("Config")->set(int64_t(0));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "CONFIG");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 1);
}

VE_TEST(search_root_narrowing) {
    Node root("root");
    mkTree(root);
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "config");
    pipe.contextNode()->at("params")->set("root", "logs");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 0);
}

VE_TEST(search_root_not_found) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "x");
    pipe.contextNode()->at("params")->set("root", "nope");
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(0), int(service::ERR_NOT_FOUND));
}

VE_TEST(search_top_limits_results) {
    Node root("root");
    for (int i = 0; i < 20; ++i) root.at("a" + std::to_string(i) + "_hit")->set(int64_t(i));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "hit");
    pipe.contextNode()->at("params")->set("top", int64_t(3));
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 3);
}

VE_TEST(search_top_zero_returns_err_invalid) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "x");
    pipe.contextNode()->at("params")->set("top", int64_t(0));
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(0), int(service::ERR_INVALID));
}

VE_TEST(search_glob_wildcards) {
    Node root("root");
    root.at("http_port")->set(int64_t(1));
    root.at("http_host")->set(std::string("x"));
    root.at("tcp_port")->set(int64_t(1));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "*_port");
    pipe.contextNode()->at("params")->set("mode", "glob");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 2);
}

VE_TEST(search_glob_single_char) {
    Node root("root");
    root.at("cat")->set(int64_t(0));
    root.at("cot")->set(int64_t(0));
    root.at("coat")->set(int64_t(0));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "c?t");
    pipe.contextNode()->at("params")->set("mode", "glob");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 2); // cat, cot; coat 长度不符
}

VE_TEST(search_exact_matches_full_name) {
    Node root("root");
    root.at("port")->set(int64_t(1));
    root.at("porting")->set(int64_t(1));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "port");
    pipe.contextNode()->at("params")->set("mode", "exact");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 1);
    VE_ASSERT_EQ(m->child(0)->getString(), std::string("port"));
}

VE_TEST(search_unknown_mode_err_invalid) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "x");
    pipe.contextNode()->at("params")->set("mode", "regex");
    runEnvelope(&session, pipe);

    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(0), int(service::ERR_INVALID));
}

VE_TEST(search_value_matches_string_values) {
    Node root("root");
    root.at("host")->set(std::string("localhost"));
    root.at("backup_host")->set(std::string("localhost.mirror"));
    root.at("port")->set(int64_t(8080));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "localhost");
    pipe.contextNode()->at("params")->set("target", "value");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 2);
}

VE_TEST(search_value_skips_null_values) {
    Node root("root");
    root.at("plain");            // no set → Null
    root.at("real")->set(std::string("hello"));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "hello");
    pipe.contextNode()->at("params")->set("target", "value");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 1);
    VE_ASSERT_EQ(m->child(0)->getString(), std::string("real"));
}

VE_TEST(search_value_matches_numeric_toString) {
    Node root("root");
    root.at("port")->set(int64_t(8080));
    root.at("other")->set(int64_t(9090));
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "search");
    pipe.contextNode()->at("params")->set("pattern", "8080");
    pipe.contextNode()->at("params")->set("target", "value");
    runEnvelope(&session, pipe);

    Node* m = pipe.contextNode()->find("data/matches");
    VE_ASSERT(m && m->count() == 1);
}

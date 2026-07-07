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

    // Skeleton returns ok even without matches; Task 2 will assert data.matches.
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
}

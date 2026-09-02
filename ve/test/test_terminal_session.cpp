// test_terminal_session.cpp — Terminal REPL command binding
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "../src/service/terminal_session.h"

#include <ve/core/command.h>
#include <ve/core/schema.h>

#include <memory>
#include <string>
#include <thread>

using namespace ve;

namespace {

struct ComplexCommandCapture
{
    int calls = 0;
    int deviceId = 0;
    std::string deviceName;
    std::string devicePath;
    bool enabled = false;
    double threshold = 0.0;
    int thirdValue = 0;
    std::string secondStepText;
    std::string firstTag;
    std::string unicode;
};

} // namespace

VE_TEST(terminal_custom_command_accepts_full_complex_json_body)
{
    auto capture = std::make_shared<ComplexCommandCapture>();
    Node* commandNode = command::reg("_test.terminal.complex",
        [capture](Node*, Node* in, Node* out) -> Result {
            ++capture->calls;
            capture->deviceId = in->get("request/device/id").toInt();
            capture->deviceName = in->get("request/device/name").toString();
            capture->devicePath = in->get("request/device/path").toString();
            capture->enabled = in->get("request/options/enabled").toBool();
            capture->threshold = in->get("request/options/threshold").toDouble();

            Node* steps = in->find("steps");
            Node* firstValues = steps && steps->child(0)
                ? steps->child(0)->find("values") : nullptr;
            if (firstValues) capture->thirdValue = firstValues->get(2).toInt();
            if (steps && steps->child(1))
                capture->secondStepText = steps->child(1)->get("text").toString();

            Node* tags = in->find("tags");
            if (tags) capture->firstTag = tags->get(0).toString();
            capture->unicode = in->get("unicode").toString();
            out->set("received", true);
            return Result::ok();
        });

    schema::JsonS::toNode(commandNode->at("instruction"), R"({
      "description":"Exercise full JSON command input.",
      "input_schema":{
        "type":"object",
        "properties":{
          "request":{"type":"object","properties":{
            "device":{"type":"object"},
            "options":{"type":"object"}
          }},
          "steps":{"type":"array","items":{"type":"object"}},
          "tags":{"type":"array","items":{"type":"string"}},
          "unicode":{"type":"string"}
        },
        "required":["request","steps"]
      }
    })");

    Node root("root");
    service::TerminalSession::Options options;
    options.prompt_color = false;
    service::TerminalSession session(&root, options);

    const std::string line =
        R"(_test terminal complex { "request": { "device": { "id": 7, "name": "axis \"A\"", "path": "C:\\robot\\arm" }, "options": { "enabled": true, "threshold": 1.25 } }, "steps": [ { "op": "move", "values": [1, 2, 3] }, { "op": "label", "text": "line\nbreak" } ], "tags": ["alpha beta", "gamma"], "unicode": "\u4f60\u597d" })";

    const std::string output = session.execute(line);

    VE_ASSERT(output.find("ok") != std::string::npos);
    VE_ASSERT_EQ(capture->calls, 1);
    VE_ASSERT_EQ(capture->deviceId, 7);
    VE_ASSERT_EQ(capture->deviceName, std::string("axis \"A\""));
    VE_ASSERT_EQ(capture->devicePath, std::string("C:\\robot\\arm"));
    VE_ASSERT(capture->enabled);
    VE_ASSERT_NEAR(capture->threshold, 1.25, 1e-12);
    VE_ASSERT_EQ(capture->thirdValue, 3);
    VE_ASSERT_EQ(capture->secondStepText, std::string("line\nbreak"));
    VE_ASSERT_EQ(capture->firstTag, std::string("alpha beta"));
    VE_ASSERT_EQ(capture->unicode, std::string(u8"你好"));

    const std::string invalid = session.execute(
        R"(_test terminal complex {"request":{"device":},"steps":[]})");
    VE_ASSERT_EQ(invalid, std::string("error: invalid JSON\n"));
    VE_ASSERT_EQ(capture->calls, 1);
}

VE_TEST(terminal_foreground_command_honors_registered_loop)
{
    AsioLoop command_loop("test.terminal.command");
    command_loop.start();

    const std::thread::id caller_thread = std::this_thread::get_id();
    auto command_thread = std::make_shared<std::thread::id>();
    Node* command_node = command::reg("_test.terminal.loop_affinity",
        [command_thread](Node*, Node*, Node*) -> Result {
            *command_thread = std::this_thread::get_id();
            return Result::ok();
        });
    command_node->set("loop", Var::ptr(static_cast<Loop*>(&command_loop)));

    Node root("root");
    service::TerminalSession::Options options;
    options.prompt_color = false;
    service::TerminalSession session(&root, options);

    const std::string output = session.execute("_test terminal loop_affinity");
    VE_ASSERT(output.find("ok") != std::string::npos);
    VE_ASSERT(*command_thread != std::thread::id{});
    VE_ASSERT(*command_thread != caller_thread);

    command_node->remove("loop");
    command_loop.stop();
}

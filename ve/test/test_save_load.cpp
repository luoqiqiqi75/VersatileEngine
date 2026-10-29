#include "ve_test.h"

#include "../src/service/terminal_session.h"

#include <ve/core/command.h>

#include <filesystem>
#include <cstdlib>
#include <fstream>

using namespace ve;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& ext, const std::filesystem::path& dir = std::filesystem::temp_directory_path())
        : path(dir /
               ("ve_save_load_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ext)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string quoted() const { return "\"" + path.generic_string() + "\""; }
};

static Command fileCommand(const std::string& name, service::Session* session,
                           const std::string& file = {}, const std::string& path = {},
                           const std::string& format = {})
{
    Command cmd = command::create(name);
    if (session) cmd.contextNode()->set("_session", Var::ptr(session));
    if (!file.empty()) cmd.inputNode()->set("file", file);
    if (!path.empty()) cmd.inputNode()->set("path", path);
    if (!format.empty()) cmd.inputNode()->set("format", format);
    cmd.run();
    return cmd;
}

} // namespace

VE_TEST(save_load_terminal_defaults_to_current_node)
{
    TempFile file(".json");
    Node root;
    Node* current = root.at("config");
    current->set("port", 8080);
    root.set("outside", "keep");
    service::TerminalSession::Options options;
    options.prompt_color = false;
    service::TerminalSession session(&root, options);
    session.current = current;

    VE_ASSERT(session.execute("save " + file.quoted()).find("ok") != std::string::npos);
    current->set("port", 0);
    VE_ASSERT(session.execute("load " + file.quoted()).find("ok") != std::string::npos);
    VE_ASSERT_EQ(current->get("port").toInt(), 8080);
    VE_ASSERT(!current->find("config"));
    VE_ASSERT(!root.find("port"));
    VE_ASSERT_EQ(root.get("outside").toString(), std::string("keep"));
    VE_ASSERT(session.current == current);

    root.set("other/port", 9090);
    VE_ASSERT(session.execute("save " + file.quoted() + " /other json").find("ok") != std::string::npos);
    VE_ASSERT(session.execute("load " + file.quoted() + " /target").find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("target/port").toInt(), 9090);
    VE_ASSERT_EQ(current->get("port").toInt(), 8080);

    current->set("relative/port", 7070);
    VE_ASSERT(session.execute("save --path relative --file " + file.quoted()).find("ok") != std::string::npos);
    VE_ASSERT(session.execute("load " + file.quoted() + " --path restored").find("ok") != std::string::npos);
    VE_ASSERT_EQ(current->get("restored/port").toInt(), 7070);
}

VE_TEST(save_load_format_from_extension)
{
    for (const auto& ext : {".json", ".xml", ".bin", ".md", ".JSON", ""}) {
        TempFile file(ext);
        Node root;
        root.set("port", 8080);
        service::Session session(&root, &root);

        Command save = fileCommand("save", &session, file.path.generic_string());
        VE_ASSERT(save.result().isSuccess());
        const std::string expected = std::string(ext).empty() ? "json" : std::string(ext) == ".JSON" ? "json" : std::string(ext).substr(1);
        VE_ASSERT_EQ(save.outputNode()->get("format").toString(), expected);
        VE_ASSERT_EQ(save.outputNode()->get("size").toInt64(), static_cast<int64_t>(std::filesystem::file_size(file.path)));

        root.set("port", 0);
        Command load = fileCommand("load", &session, file.path.generic_string());
        VE_ASSERT(load.result().isSuccess());
        if (expected != "md") VE_ASSERT_EQ(root.get("port").toInt(), 8080);
        VE_ASSERT_EQ(load.outputNode()->get("path").toString(), std::string{});
        VE_ASSERT_EQ(load.outputNode()->get("format").toString(), expected);
        VE_ASSERT_EQ(load.outputNode()->get("file").toString(), file.path.generic_string());
    }
}

VE_TEST(save_load_default_file_uses_node_name_and_json)
{
    TempFile file(".json", std::filesystem::current_path());
    const std::string name = file.path.stem().string();
    Node root;
    Node* current = root.at(name);
    current->set("port", 8080);
    service::TerminalSession session(&root);
    session.current = current;

    Command saved = fileCommand("save", &session);
    VE_ASSERT(saved.result().isSuccess());
    VE_ASSERT_EQ(saved.outputNode()->get("file").toString(), file.path.filename().string());
    VE_ASSERT_EQ(saved.outputNode()->get("format").toString(), std::string("json"));
    current->set("port", 0);
    VE_ASSERT(session.execute("load").find("ok") != std::string::npos);
    VE_ASSERT_EQ(current->get("port").toInt(), 8080);

    session.current = &root;
    VE_ASSERT(session.execute("load --path created/" + name).find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("created/" + name + "/port").toInt(), 8080);
}

VE_TEST(save_load_explicit_format_overrides_extension_and_default)
{
    TempFile file(".xml");
    Node root;
    root.set("port", 8080);
    service::TerminalSession session(&root);
    const auto saved = session.execute("save " + file.quoted() + " / json");
    VE_ASSERT(saved.find("ok") != std::string::npos);
    VE_ASSERT(saved.find("json") != std::string::npos);
    root.set("port", 0);
    VE_ASSERT(session.execute("load " + file.quoted() + " / json").find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("port").toInt(), 8080);

    TempFile defaultFile(".bin", std::filesystem::current_path());
    Node* current = root.at(defaultFile.path.stem().string());
    current->set("port", 9090);
    session.current = current;
    Command binSave = fileCommand("save", &session, {}, {}, "bin");
    VE_ASSERT(binSave.result().isSuccess());
    VE_ASSERT_EQ(binSave.outputNode()->get("file").toString(), defaultFile.path.filename().string());
    VE_ASSERT_EQ(binSave.outputNode()->get("format").toString(), std::string("bin"));
    current->set("port", 0);
    VE_ASSERT(fileCommand("load", &session, {}, {}, "bin").result().isSuccess());
    VE_ASSERT_EQ(current->get("port").toInt(), 9090);
}

VE_TEST(save_load_creates_parent_directories_and_target_nodes)
{
    TempFile location(".with.dot");
    const auto nested = location.path / "nested";
    TempFile file("", nested);
    Node root;
    root.set("port", 8080);
    service::Session session(&root, &root);

    const Command saved = fileCommand("save", &session, file.path.generic_string());
    VE_ASSERT(saved.result().isSuccess());
    VE_ASSERT(std::filesystem::is_regular_file(file.path));
    VE_ASSERT_EQ(saved.outputNode()->get("format").toString(), std::string("json"));
    const Command loaded = fileCommand("load", &session, file.path.generic_string(), "created/target");
    VE_ASSERT(loaded.result().isSuccess());
    VE_ASSERT_EQ(root.get("created/target/port").toInt(), 8080);
    VE_ASSERT_EQ(loaded.outputNode()->get("format").toString(), std::string("json"));

    std::error_code ec;
    std::filesystem::remove(file.path, ec);
    std::filesystem::remove(nested, ec);
}

VE_TEST(save_load_accepts_long_flags_and_mixed_positionals)
{
    TempFile file(".xml");
    Node root;
    root.set("source/port", 8080);
    service::TerminalSession::Options options;
    options.prompt_color = false;
    service::TerminalSession session(&root, options);

    const auto saved = session.execute("save --path /source --file " + file.quoted() + " --format json");
    VE_ASSERT(saved.find("ok") != std::string::npos);
    VE_ASSERT(saved.find("json") != std::string::npos);
    VE_ASSERT(session.execute("load --file " + file.quoted() + " --path /restored --format json").find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("restored/port").toInt(), 8080);

    root.set("source/port", 9090);
    VE_ASSERT(session.execute("save /source --file " + file.quoted() + " --format json").find("ok") != std::string::npos);
    VE_ASSERT(session.execute("load " + file.quoted() + " --path /mixed --format json").find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("mixed/port").toInt(), 9090);

    session.current = root.find("source");
    VE_ASSERT(session.execute("save --file " + file.quoted() + " --format json").find("ok") != std::string::npos);
    root.set("source/port", 0);
    VE_ASSERT(session.execute("load --file " + file.quoted() + " --format json").find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("source/port").toInt(), 9090);
}

VE_TEST(save_load_without_session_requires_explicit_node_path)
{
    for (const auto& name : {"save", "load"}) {
        Command cmd = fileCommand(name, nullptr);
        VE_ASSERT(cmd.result().isError());
        VE_ASSERT_EQ(cmd.result().message(), std::string("path required without session"));
    }

    TempFile file(".json");
    const std::string path = file.path.stem().string();
    Node* target = node::root()->at(path);
    target->set("port", 8080);
    Command saved = fileCommand("save", nullptr, file.path.generic_string(), "/" + path);
    VE_ASSERT(saved.result().isSuccess());
    VE_ASSERT_EQ(saved.outputNode()->get("format").toString(), std::string("json"));
    target->set("port", 0);
    VE_ASSERT(fileCommand("load", nullptr, file.path.generic_string(), path).result().isSuccess());
    VE_ASSERT_EQ(target->get("port").toInt(), 8080);
    node::root()->remove(target);
}

VE_TEST(save_load_rejects_unknown_formats_and_missing_files)
{
    TempFile file(".unsupported");
    Node root;
    service::Session session(&root, &root);
    Command saved = fileCommand("save", &session, file.path.generic_string());
    VE_ASSERT(saved.result().isError());
    VE_ASSERT_EQ(saved.result().message(), std::string("unknown format: unsupported"));
    VE_ASSERT(!std::filesystem::exists(file.path));
    Command loaded = fileCommand("load", &session, file.path.generic_string(), "missing/target", "json");
    VE_ASSERT(loaded.result().isError());
    VE_ASSERT(!root.find("missing"));
}

VE_TEST(save_load_expands_home_directory_in_file_paths)
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    VE_ASSERT(home && *home);
    if (!home || !*home) return;
    TempFile file(".json", home);
    const std::string input = "~/" + file.path.filename().generic_string();

    Node root;
    root.set("port", 8080);
    service::TerminalSession session(&root);
    VE_ASSERT(session.execute("save --file " + input).find("ok") != std::string::npos);
    VE_ASSERT(std::filesystem::exists(file.path));
    root.set("port", 0);
    VE_ASSERT(session.execute("load " + input).find("ok") != std::string::npos);
    VE_ASSERT_EQ(root.get("port").toInt(), 8080);
}

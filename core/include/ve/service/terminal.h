// terminal.h — ve::Terminal: embeddable Node tree REPL
#pragma once

#include "ve/core/node.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ve {

class VE_API Terminal
{
public:
    using Output  = std::function<void(const std::string&)>;
    using Args    = std::vector<std::string>;
    using Handler = std::function<void(Terminal&, const Args& args, const std::string& line)>;

    explicit Terminal(Node* root = nullptr);
    ~Terminal();

    void setOutput(Output cb);
    void print(const std::string& text);

    bool execute(const std::string& line);

    Node* root() const;
    Node* current() const;
    void  setCurrent(Node* node);
    std::string currentPath() const;
    std::string prompt() const;

    void registerCommand(const std::string& name, Handler handler, const std::string& help = "");

    const std::vector<std::string>& history() const;

    // JSON utilities (Node ↔ JSON string)
    static std::string nodeToJson(const Node* node, int indent = 2);

    // orphan management
    std::vector<Node*>& orphans();

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve

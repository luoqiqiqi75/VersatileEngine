// core/terminal/main.cpp — ve_terminal: interactive Node console
//
// Commands:
//   ls [path]          list children
//   cd <path>          navigate to node
//   pwd                print current path
//   tree [path]        dump subtree
//   mk <path>          ensure (create along path)
//   rm <path>          erase node at path
//   add <name> [N]     append N children with name (default 1)
//   mv <path> <name>   reparent node to current, with name
//   info               show current node details
//   root               go to root
//   help               show commands
//   quit / exit        exit

#include "ve/core/node.h"
#include "ve/core/log.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ve;

static Node* g_root = nullptr;
static Node* g_cur  = nullptr;

static std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static void cmd_help()
{
    std::cout
        << "Commands:\n"
        << "  ls [path]          list children\n"
        << "  cd <path>          navigate to node\n"
        << "  pwd                print current path from root\n"
        << "  tree [path]        dump subtree\n"
        << "  mk <path>          ensure nodes along path\n"
        << "  rm <path>          erase node at path\n"
        << "  add <name> [N]     append N children with name (default 1)\n"
        << "  mv <src> <name>    reparent resolved src to current, with name\n"
        << "  info               show current node details\n"
        << "  root               go to root\n"
        << "  help               show this help\n"
        << "  quit / exit        exit\n";
}

static void cmd_ls(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    auto names = node->childNames();
    int anon = node->count("");

    if (anon > 0) std::cout << "  (anon) x" << anon << "\n";
    for (auto& n : names) {
        int cnt = node->count(n);
        if (cnt == 1) std::cout << "  " << n << "\n";
        else          std::cout << "  " << n << " x" << cnt << "\n";
    }
    std::cout << "  (" << node->count() << " total)\n";
}

static void cmd_tree(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    std::cout << node->dump();
}

static void cmd_info(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    std::cout << "  name:     " << (node->name().empty() ? "(anon)" : node->name()) << "\n"
              << "  path:     " << node->path(g_root) << "\n"
              << "  parent:   " << (node->parent() ? node->parent()->name() : "(none)") << "\n"
              << "  children: " << node->childCount() << "\n"
              << "  shadow:   " << (node->shadow() ? node->shadow()->name() : "(none)") << "\n"
              << "  index:    " << node->indexInParent() << "\n";
}

static std::string prompt()
{
    if (g_cur == g_root) return "/> ";
    auto p = g_cur->path(g_root);
    return "/" + p + "> ";
}

int main()
{
    std::cout << "ve_terminal — interactive Node console\n"
              << "type 'help' for commands\n\n";

    g_root = new Node("root");
    g_cur  = g_root;

    std::string line;
    while (true) {
        std::cout << prompt();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto args = split(line);
        if (args.empty()) continue;
        auto& cmd = args[0];

        if (cmd == "quit" || cmd == "exit") break;

        else if (cmd == "help") cmd_help();

        else if (cmd == "root") { g_cur = g_root; }

        else if (cmd == "pwd") {
            auto p = g_cur->path(g_root);
            std::cout << "/" << p << "\n";
        }

        else if (cmd == "ls") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            cmd_ls(target);
        }

        else if (cmd == "tree") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            cmd_tree(target);
        }

        else if (cmd == "cd") {
            if (args.size() < 2) { std::cout << "usage: cd <path>\n"; continue; }
            if (args[1] == "..") {
                if (g_cur->parent()) g_cur = g_cur->parent();
            } else {
                auto* n = g_cur->resolve(args[1]);
                if (n) g_cur = n;
                else std::cout << "not found: " << args[1] << "\n";
            }
        }

        else if (cmd == "mk") {
            if (args.size() < 2) { std::cout << "usage: mk <path>\n"; continue; }
            auto* n = g_cur->ensure(args[1]);
            if (n) std::cout << "created: " << n->path(g_root) << "\n";
            else   std::cout << "failed\n";
        }

        else if (cmd == "rm") {
            if (args.size() < 2) { std::cout << "usage: rm <path>\n"; continue; }
            if (g_cur->erase(args[1])) std::cout << "removed\n";
            else std::cout << "failed\n";
        }

        else if (cmd == "add") {
            if (args.size() < 2) { std::cout << "usage: add <name> [N]\n"; continue; }
            int n = 1;
            if (args.size() > 2) n = std::stoi(args[2]);
            for (int i = 0; i < n; ++i)
                g_cur->append(args[1], new Node());
            std::cout << "added " << n << " '" << args[1] << "'\n";
        }

        else if (cmd == "mv") {
            if (args.size() < 3) { std::cout << "usage: mv <src_path> <name>\n"; continue; }
            auto* src = g_cur->resolve(args[1]);
            if (!src) { std::cout << "not found: " << args[1] << "\n"; continue; }
            g_cur->append(args[2], src);
            std::cout << "moved to " << src->path(g_root) << "\n";
        }

        else if (cmd == "info") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            cmd_info(target);
        }

        else {
            std::cout << "unknown: " << cmd << "  (type 'help')\n";
        }
    }

    delete g_root;
    std::cout << "bye\n";
    return 0;
}

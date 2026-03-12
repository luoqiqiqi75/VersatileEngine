// core/terminal/main.cpp — ve_terminal: interactive Node console
//
// === Navigation ===
//   cd <path>              navigate to node (supports "..")
//   pwd                    print current path from root
//   root                   go to root
//   up [N]                 go up N parent levels (default 1)
//   first / last           go to first/last child
//   prev / next            go to prev/next sibling
//   sibling <N>            go to sibling at offset N
//
// === Viewing ===
//   ls [path]              list children in insertion order
//   tree [path]            dump subtree
//   info [path]            show node details
//   names [path]           list unique child names
//
// === Child Access ===
//   child <index>          get child by global index (negative ok)
//   child <name> [overlap] get child by name + overlap
//   at <key>               childAt(key)
//   has <name|index>       check existence
//   count [name]           count children (or by name)
//   indexof <child_path>   indexOf child in current node
//
// === Child Management ===
//   add <name> [overlap]   append node(s) with name (overlap = extra copies)
//   addi [overlap]         append anonymous node(s)
//   ins <name> <index>     create named node, insert at index
//   mv <src_path> [index]  reparent resolved node to current
//   take <index|path>      detach child (keep alive, becomes orphan)
//   rm <path>              erase node at path (delete)
//   rmi <index>            remove by global index
//   rmn <name> [overlap]   remove by name + overlap
//   rmall <name>           remove all children with name
//   clear                  clear all children
//
// === Key System ===
//   key <child_path>       keyOf(child)
//   iskey <str>            test if string is valid key
//   keyidx <str>           extract numeric index from key
//
// === Shadow ===
//   shadow                 show current shadow
//   setshadow <path>       set shadow to resolved node
//   unshadow               clear shadow
//
// === Path ===
//   mk <path>              ensure nodes along path
//   resolve <path>         resolve path (with shadow)
//   find <path>            resolve path (without shadow)
//   erase <path>           erase node at path
//
// === Iterator ===
//   iter [path]            iterate children forward
//   riter [path]           iterate children reverse
//
// === Schema / JSON ===
//   schema <f1> [f2 ...]   build schema with named fields
//   loadjson <file>         load JSON file, build tree under current node
//   savejson <file> [path]  save subtree as JSON file
//   showjson [path]         print subtree as JSON
//
// === Other ===
//   help                   show commands
//   quit / exit            exit

#include "ve/core/node.h"
#include "ve/core/log.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

using json = nlohmann::ordered_json;  // preserve key order

using namespace ve;

static Node* g_root = nullptr;
static Node* g_cur  = nullptr;

// orphan pool — nodes taken but not deleted
static std::vector<Node*> g_orphans;

static std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    return std::all_of(s.begin() + start, s.end(), ::isdigit);
}

static std::string nodeSummary(const Node* n)
{
    if (!n) return "(null)";
    auto nm = n->name().empty() ? "(anon)" : n->name();
    return nm;
}

// ============================================================================
// JSON ↔ Node
// ============================================================================
//
// JSON format:
//   {                                     →  named children
//     "camera": {                         →  child "camera" with sub-children
//       "resolution": null,               →  leaf child "resolution"
//       "fps": null                       →  leaf child "fps"
//     },
//     "lidar": null,                      →  leaf child "lidar"
//     "item": [                           →  array → N children all named "item"
//       { "name": null, "value": null },  →  item#0 with sub-children
//       { "name": null, "value": null }   →  item#1 with sub-children
//     ],
//     "": [null, null, null]              →  3 anonymous children
//   }
//   number N (as value)                   →  N leaf copies of that name
//
// savejson: reverse — Node tree → JSON

static void json_build(Node* parent, const json& j)
{
    if (j.is_object()) {
        // Phase 1: collect all children for this level
        Node::Nodes batch;
        for (auto& [key, val] : j.items()) {
            if (val.is_array()) {
                for (auto& elem : val)
                    batch.push_back(new Node(key));
            } else {
                batch.push_back(new Node(key));
            }
        }
        // Phase 2: single batch insert
        parent->insert(batch);
        // Phase 3: recurse into children that have sub-content
        int idx = 0;
        for (auto& [key, val] : j.items()) {
            if (val.is_array()) {
                for (auto& elem : val) {
                    if (elem.is_object() && !elem.empty())
                        json_build(batch[idx], elem);
                    ++idx;
                }
            } else if (val.is_object() && !val.empty()) {
                json_build(batch[idx], val);
                ++idx;
            } else {
                ++idx;
            }
        }
    } else if (j.is_array()) {
        // Phase 1: collect all anonymous children
        Node::Nodes batch;
        for (size_t i = 0; i < j.size(); ++i)
            batch.push_back(new Node(""));
        // Phase 2: single batch insert
        parent->insert(batch);
        // Phase 3: recurse
        for (size_t i = 0; i < j.size(); ++i) {
            if (j[i].is_object() && !j[i].empty())
                json_build(batch[i], j[i]);
        }
    }
}

static json node_to_json(const Node* node)
{
    if (node->count() == 0) return nullptr;

    // check if all children are anonymous
    bool allAnon = true;
    for (auto* c : *node) {
        if (!c->name().empty()) { allAnon = false; break; }
    }

    if (allAnon) {
        // → JSON array of children
        json arr = json::array();
        for (auto* c : *node) arr.push_back(node_to_json(c));
        return arr;
    }

    // → JSON object, group consecutive same-name children
    json obj = json::object();
    auto names = node->childNames();

    // track which names have arrays (multiple children)
    for (auto& nm : names) {
        int cnt = node->count(nm);
        if (cnt == 1) {
            auto* ch = node->child(nm, 0);
            obj[nm] = node_to_json(ch);
        } else {
            // multiple → array
            json arr = json::array();
            for (int i = 0; i < cnt; ++i)
                arr.push_back(node_to_json(node->child(nm, i)));
            obj[nm] = arr;
        }
    }

    // also handle anonymous children mixed in
    int anonCnt = 0;
    for (auto* c : *node) if (c->name().empty()) ++anonCnt;
    if (anonCnt > 0) {
        json arr = json::array();
        for (auto* c : *node)
            if (c->name().empty()) arr.push_back(node_to_json(c));
        obj[""] = arr;
    }

    return obj;
}

// ============================================================================
// Commands
// ============================================================================

static void cmd_help()
{
    std::cout <<
        "=== Navigation ===\n"
        "  cd <path>              navigate (supports '..')\n"
        "  pwd                    print current path\n"
        "  root                   go to root\n"
        "  up [N]                 go up N levels (default 1)\n"
        "  first / last           go to first/last child\n"
        "  prev / next            go to prev/next sibling\n"
        "  sibling <N>            go to sibling at offset N\n"
        "\n"
        "=== Viewing ===\n"
        "  ls [path]              list children in order\n"
        "  tree [path]            dump subtree\n"
        "  info [path]            show node details\n"
        "  names [path]           list unique child names\n"
        "\n"
        "=== Child Access ===\n"
        "  child <index>          get child by global index (-N ok)\n"
        "  child <name> [overlap] get child by name + overlap\n"
        "  at <key>               childAt(key)\n"
        "  has <name|index>       check existence\n"
        "  count [name]           count children (or by name)\n"
        "  indexof <child_path>   indexOf resolved child\n"
        "\n"
        "=== Child Management ===\n"
        "  add <name> [overlap]   append node(s), overlap = extra copies\n"
        "  addi [overlap]         append anonymous node(s)\n"
        "  ins <name> <index>     create named node, insert at index\n"
        "  mv <src> [index]       reparent resolved node to current\n"
        "  take <index|path>      detach child (kept in orphan pool)\n"
        "  rm <path>              erase at path\n"
        "  rmi <index>            remove by global index\n"
        "  rmn <name> [overlap]   remove by name + overlap\n"
        "  rmall <name>           remove all with name\n"
        "  clear                  clear all children\n"
        "  orphans                list orphan pool\n"
        "  adopt <N>              insert orphan N into current\n"
        "\n"
        "=== Key System ===\n"
        "  key <child_path>       keyOf(child)\n"
        "  iskey <str>            test if valid key\n"
        "  keyidx <str>           extract index from key\n"
        "\n"
        "=== Shadow ===\n"
        "  shadow                 show current shadow\n"
        "  setshadow <path>       set shadow\n"
        "  unshadow               clear shadow\n"
        "\n"
        "=== Path ===\n"
        "  mk <path>              ensure nodes along path\n"
        "  resolve <path>         resolve with shadow\n"
        "  find <path>            resolve without shadow\n"
        "  erase <path>           erase at path\n"
        "\n"
        "=== Iterator ===\n"
        "  iter [path]            iterate children forward\n"
        "  riter [path]           iterate children reverse\n"
        "\n"
        "=== Schema / JSON ===\n"
        "  schema <f1> [f2 ...]   build schema with fields\n"
        "  loadjson <file>        load JSON file, build under current node\n"
        "  savejson <file>        save current subtree as JSON\n"
        "  showjson               print current subtree as JSON\n"
        "  benchjson <file> [N]   benchmark JSON→Node build (N reps, default 10)\n"
        "\n"
        "=== Other ===\n"
        "  help                   show this help\n"
        "  quit / exit            exit\n";
}

static void cmd_ls(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    int total = node->count();
    if (total == 0) { std::cout << "  (empty)\n"; return; }

    for (int i = 0; i < total; ++i) {
        auto* c = node->child(i);
        auto nm = c->name().empty() ? "(anon)" : c->name();
        std::cout << "  [" << i << "] " << nm;
        // show key if it differs from name
        auto k = node->keyOf(c);
        if (k != nm && k != "(anon)") std::cout << "  (key: " << k << ")";
        std::cout << "\n";
    }
    std::cout << "  (" << total << " total)\n";
}

static void cmd_tree(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    std::cout << node->dump();
}

static void cmd_info(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    auto nm = node->name().empty() ? "(anon)" : node->name();

    std::cout << "  name:      " << nm << "\n"
              << "  path:      /" << node->path(g_root) << "\n"
              << "  parent:    " << (node->parent() ? nodeSummary(node->parent()) : "(none)") << "\n"
              << "  children:  " << node->count() << "\n"
              << "  empty:     " << (node->empty() ? "yes" : "no") << "\n"
              << "  shadow:    " << (node->shadow() ? nodeSummary(node->shadow()) : "(none)") << "\n";

    if (node->parent()) {
        std::cout << "  indexOf:   " << node->parent()->indexOf(node) << "\n"
                  << "  keyOf:     " << node->parent()->keyOf(node) << "\n";
    }

    auto* f = node->first();
    auto* l = node->last();
    auto* p = node->prev();
    auto* n = node->next();
    std::cout << "  first:     " << nodeSummary(f) << "\n"
              << "  last:      " << nodeSummary(l) << "\n"
              << "  prev:      " << (p ? nodeSummary(p) : "(none)") << "\n"
              << "  next:      " << (n ? nodeSummary(n) : "(none)") << "\n";
}

static void cmd_names(Node* node)
{
    if (!node) { std::cout << "(null)\n"; return; }
    auto names = node->childNames();
    int anon = node->count() - (int)node->children().size(); // approximate
    // actually count anon by checking empty names
    int anonCnt = 0;
    for (auto* c : *node) if (c->name().empty()) ++anonCnt;

    if (anonCnt > 0) std::cout << "  (anon) x" << anonCnt << "\n";
    for (auto& n : names) {
        int cnt = node->count(n);
        if (cnt == 1) std::cout << "  " << n << "\n";
        else          std::cout << "  " << n << " x" << cnt << "\n";
    }
}

static std::string prompt()
{
    if (g_cur == g_root) return "/> ";
    auto p = g_cur->path(g_root);
    return "/" + p + "> ";
}

int main()
{
    std::cout << "ve_terminal — interactive Node console (v2)\n"
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

        // ========== Exit ==========
        if (cmd == "quit" || cmd == "exit") break;

        // ========== Help ==========
        else if (cmd == "help") cmd_help();

        // ========== Navigation ==========
        else if (cmd == "root") { g_cur = g_root; }

        else if (cmd == "pwd") {
            std::cout << "/" << g_cur->path(g_root) << "\n";
        }

        else if (cmd == "up") {
            int n = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 1;
            // parent(level): 0=parent, 1=grandparent, ...
            auto* p = g_cur->parent(n - 1);
            if (p) g_cur = p;
            else std::cout << "cannot go up " << n << " levels\n";
        }

        else if (cmd == "cd") {
            if (args.size() < 2) { std::cout << "usage: cd <path>\n"; continue; }
            if (args[1] == "..") {
                if (g_cur->parent()) g_cur = g_cur->parent();
                else std::cout << "already at root\n";
            } else {
                auto* n = g_cur->resolve(args[1]);
                if (n) g_cur = n;
                else std::cout << "not found: " << args[1] << "\n";
            }
        }

        else if (cmd == "first") {
            auto* f = g_cur->first();
            if (f) { g_cur = f; std::cout << "→ " << nodeSummary(f) << "\n"; }
            else std::cout << "(no children)\n";
        }

        else if (cmd == "last") {
            auto* l = g_cur->last();
            if (l) { g_cur = l; std::cout << "→ " << nodeSummary(l) << "\n"; }
            else std::cout << "(no children)\n";
        }

        else if (cmd == "prev") {
            auto* p = g_cur->prev();
            if (p) { g_cur = p; std::cout << "→ " << nodeSummary(p) << "\n"; }
            else std::cout << "(no prev sibling)\n";
        }

        else if (cmd == "next") {
            auto* n = g_cur->next();
            if (n) { g_cur = n; std::cout << "→ " << nodeSummary(n) << "\n"; }
            else std::cout << "(no next sibling)\n";
        }

        else if (cmd == "sibling") {
            if (args.size() < 2) { std::cout << "usage: sibling <offset>\n"; continue; }
            int off = std::stoi(args[1]);
            auto* s = g_cur->sibling(off);
            if (s) { g_cur = s; std::cout << "→ " << nodeSummary(s) << "\n"; }
            else std::cout << "no sibling at offset " << off << "\n";
        }

        // ========== Viewing ==========
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

        else if (cmd == "info") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            cmd_info(target);
        }

        else if (cmd == "names") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            cmd_names(target);
        }

        // ========== Child Access ==========
        else if (cmd == "child") {
            if (args.size() < 2) { std::cout << "usage: child <index> | child <name> [overlap]\n"; continue; }
            Node* c = nullptr;
            if (isInt(args[1])) {
                int idx = std::stoi(args[1]);
                c = g_cur->child(idx);
                if (c) std::cout << "[" << idx << "] " << nodeSummary(c)
                                 << "  (key: " << g_cur->keyOf(c) << ")\n";
                else   std::cout << "no child at index " << idx << "\n";
            } else {
                int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
                c = g_cur->child(args[1], overlap);
                if (c) std::cout << nodeSummary(c) << "  (index: " << g_cur->indexOf(c) << ")\n";
                else   std::cout << "no child '" << args[1] << "' overlap " << overlap << "\n";
            }
        }

        else if (cmd == "at") {
            if (args.size() < 2) { std::cout << "usage: at <key>\n"; continue; }
            auto* c = g_cur->childAt(args[1]);
            if (c) std::cout << nodeSummary(c) << "  (index: " << g_cur->indexOf(c) << ")\n";
            else   std::cout << "no child at key '" << args[1] << "'\n";
        }

        else if (cmd == "has") {
            if (args.size() < 2) { std::cout << "usage: has <name|index>\n"; continue; }
            if (isInt(args[1])) {
                int idx = std::stoi(args[1]);
                std::cout << (g_cur->has(idx) ? "true" : "false") << "\n";
            } else {
                int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
                std::cout << (g_cur->has(args[1], overlap) ? "true" : "false") << "\n";
            }
        }

        else if (cmd == "count") {
            if (args.size() > 1) {
                int c = g_cur->count(args[1]);
                std::cout << "count(\"" << args[1] << "\") = " << c << "\n";
            } else {
                std::cout << "count() = " << g_cur->count() << "\n";
            }
        }

        else if (cmd == "indexof") {
            if (args.size() < 2) { std::cout << "usage: indexof <child_path>\n"; continue; }
            auto* c = g_cur->resolve(args[1]);
            if (!c) { std::cout << "not found: " << args[1] << "\n"; continue; }
            int idx = g_cur->indexOf(c);
            if (idx >= 0) std::cout << "indexOf = " << idx << "\n";
            else          std::cout << "not a direct child of current node\n";
        }

        // ========== Child Management ==========
        else if (cmd == "add") {
            if (args.size() < 2) { std::cout << "usage: add <name> [overlap]\n"; continue; }
            int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
            auto* n = g_cur->append(args[1], overlap);
            if (n) std::cout << "appended " << (1 + overlap) << " '" << args[1] << "' → last: " << g_cur->keyOf(n) << "\n";
            else   std::cout << "failed\n";
        }

        else if (cmd == "addi") {
            int overlap = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 0;
            auto* n = g_cur->append(overlap);
            if (n) std::cout << "appended " << (1 + overlap) << " anon → index: " << g_cur->indexOf(n) << "\n";
            else   std::cout << "failed\n";
        }

        else if (cmd == "ins") {
            if (args.size() < 3) { std::cout << "usage: ins <name> <index>\n"; continue; }
            int idx = std::stoi(args[2]);
            auto* c = new Node(args[1]);
            if (g_cur->insert(c, idx))
                std::cout << "inserted '" << args[1] << "' at [" << idx << "]\n";
            else {
                std::cout << "insert failed (index " << idx << ", count " << g_cur->count() << ")\n";
                delete c;
            }
        }

        else if (cmd == "mv") {
            if (args.size() < 2) { std::cout << "usage: mv <src_path> [index]\n"; continue; }
            auto* src = g_cur->resolve(args[1]);
            if (!src) { std::cout << "not found: " << args[1] << "\n"; continue; }
            if (src == g_cur) { std::cout << "cannot move node into itself\n"; continue; }
            if (args.size() > 2 && isInt(args[2])) {
                int idx = std::stoi(args[2]);
                if (g_cur->insert(src, idx))
                    std::cout << "moved to [" << idx << "] " << src->path(g_root) << "\n";
                else
                    std::cout << "insert at index " << idx << " failed\n";
            } else {
                g_cur->insert(src);
                std::cout << "moved to " << src->path(g_root) << "\n";
            }
        }

        else if (cmd == "take") {
            if (args.size() < 2) { std::cout << "usage: take <index|path>\n"; continue; }
            Node* taken = nullptr;
            if (isInt(args[1])) {
                int idx = std::stoi(args[1]);
                taken = g_cur->take(idx);
            } else {
                auto* c = g_cur->resolve(args[1], false);
                if (c && c->parent() == g_cur) taken = g_cur->take(c);
                else if (c) std::cout << "not a direct child\n";
                else        std::cout << "not found: " << args[1] << "\n";
            }
            if (taken) {
                g_orphans.push_back(taken);
                std::cout << "taken: " << nodeSummary(taken) << " → orphan pool [" << (g_orphans.size() - 1) << "]\n";
            } else if (isInt(args[1])) {
                std::cout << "no child at index " << args[1] << "\n";
            }
        }

        else if (cmd == "orphans") {
            if (g_orphans.empty()) { std::cout << "(empty)\n"; continue; }
            for (size_t i = 0; i < g_orphans.size(); ++i)
                std::cout << "  [" << i << "] " << nodeSummary(g_orphans[i]) << " (" << g_orphans[i]->count() << " children)\n";
        }

        else if (cmd == "adopt") {
            if (args.size() < 2) { std::cout << "usage: adopt <orphan_index>\n"; continue; }
            int idx = std::stoi(args[1]);
            if (idx < 0 || idx >= (int)g_orphans.size()) { std::cout << "invalid orphan index\n"; continue; }
            auto* n = g_orphans[idx];
            g_orphans.erase(g_orphans.begin() + idx);
            g_cur->insert(n);
            std::cout << "adopted: " << n->path(g_root) << "\n";
        }

        else if (cmd == "rm") {
            if (args.size() < 2) { std::cout << "usage: rm <path>\n"; continue; }
            if (g_cur->erase(args[1])) std::cout << "removed\n";
            else std::cout << "failed (not found or is root)\n";
        }

        else if (cmd == "rmi") {
            if (args.size() < 2) { std::cout << "usage: rmi <index>\n"; continue; }
            int idx = std::stoi(args[1]);
            if (g_cur->remove(idx)) std::cout << "removed [" << idx << "]\n";
            else std::cout << "no child at index " << idx << "\n";
        }

        else if (cmd == "rmn") {
            if (args.size() < 2) { std::cout << "usage: rmn <name> [overlap]\n"; continue; }
            if (args.size() > 2 && isInt(args[2])) {
                int overlap = std::stoi(args[2]);
                if (g_cur->remove(args[1], overlap)) std::cout << "removed '" << args[1] << "' overlap " << overlap << "\n";
                else std::cout << "not found\n";
            } else {
                // remove(name) with single arg removes ALL with that name
                // but remove(name, overlap) removes one. Use rmall for all.
                if (g_cur->remove(args[1], 0)) std::cout << "removed '" << args[1] << "' #0\n";
                else std::cout << "not found\n";
            }
        }

        else if (cmd == "rmall") {
            if (args.size() < 2) { std::cout << "usage: rmall <name>\n"; continue; }
            if (g_cur->remove(args[1])) std::cout << "removed all '" << args[1] << "'\n";
            else std::cout << "none found\n";
        }

        else if (cmd == "clear") {
            int n = g_cur->count();
            g_cur->clear();
            std::cout << "cleared " << n << " children\n";
        }

        // ========== Key System ==========
        else if (cmd == "key") {
            if (args.size() < 2) { std::cout << "usage: key <child_path>\n"; continue; }
            auto* c = g_cur->resolve(args[1]);
            if (!c) { std::cout << "not found: " << args[1] << "\n"; continue; }
            if (c->parent() == g_cur)
                std::cout << "keyOf = " << g_cur->keyOf(c) << "\n";
            else if (c->parent())
                std::cout << "keyOf (in parent) = " << c->parent()->keyOf(c) << "\n";
            else
                std::cout << "(no parent)\n";
        }

        else if (cmd == "iskey") {
            if (args.size() < 2) { std::cout << "usage: iskey <str>\n"; continue; }
            std::cout << (Node::isKey(args[1]) ? "true" : "false") << "\n";
        }

        else if (cmd == "keyidx") {
            if (args.size() < 2) { std::cout << "usage: keyidx <str>\n"; continue; }
            int idx = Node::keyIndex(args[1]);
            std::cout << "keyIndex = " << idx << "\n";
        }

        // ========== Shadow ==========
        else if (cmd == "shadow") {
            auto* s = g_cur->shadow();
            if (s) std::cout << "shadow: " << nodeSummary(s) << " (/" << s->path(g_root) << ")\n";
            else   std::cout << "(no shadow)\n";
        }

        else if (cmd == "setshadow") {
            if (args.size() < 2) { std::cout << "usage: setshadow <path>\n"; continue; }
            auto* s = g_root->resolve(args[1]);
            if (!s) { std::cout << "not found: " << args[1] << "\n"; continue; }
            g_cur->setShadow(s);
            std::cout << "shadow set to: " << s->path(g_root) << "\n";
        }

        else if (cmd == "unshadow") {
            g_cur->setShadow(nullptr);
            std::cout << "shadow cleared\n";
        }

        // ========== Path ==========
        else if (cmd == "mk") {
            if (args.size() < 2) { std::cout << "usage: mk <path>\n"; continue; }
            auto* n = g_cur->ensure(args[1]);
            if (n) std::cout << "ensured: /" << n->path(g_root) << "\n";
            else   std::cout << "failed\n";
        }

        else if (cmd == "resolve") {
            if (args.size() < 2) { std::cout << "usage: resolve <path>\n"; continue; }
            auto* n = g_cur->resolve(args[1], true);  // with shadow
            if (n) std::cout << "found: /" << n->path(g_root) << "\n";
            else   std::cout << "not found\n";
        }

        else if (cmd == "find") {
            if (args.size() < 2) { std::cout << "usage: find <path>\n"; continue; }
            auto* n = g_cur->resolve(args[1], false);  // no shadow
            if (n) std::cout << "found: /" << n->path(g_root) << "\n";
            else   std::cout << "not found (without shadow)\n";
        }

        else if (cmd == "erase") {
            if (args.size() < 2) { std::cout << "usage: erase <path>\n"; continue; }
            if (g_cur->erase(args[1])) std::cout << "erased\n";
            else std::cout << "failed (not found or is root)\n";
        }

        // ========== Iterator ==========
        else if (cmd == "iter") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            if (!target) { std::cout << "not found\n"; continue; }
            int i = 0;
            for (auto* c : *target) {
                auto nm = c->name().empty() ? "(anon)" : c->name();
                std::cout << "  [" << i++ << "] " << nm << "\n";
            }
            if (i == 0) std::cout << "  (empty)\n";
        }

        else if (cmd == "riter") {
            Node* target = g_cur;
            if (args.size() > 1) target = g_cur->resolve(args[1]);
            if (!target) { std::cout << "not found\n"; continue; }
            int total = target->count();
            int i = total;
            for (auto it = target->rbegin(); it != target->rend(); ++it) {
                auto* c = *it;
                auto nm = c->name().empty() ? "(anon)" : c->name();
                std::cout << "  [" << --i << "] " << nm << "\n";
            }
            if (total == 0) std::cout << "  (empty)\n";
        }

        // ========== Schema / JSON ==========
        else if (cmd == "schema") {
            if (args.size() < 2) { std::cout << "usage: schema <field1> [field2 ...]\n"; continue; }
            for (size_t i = 1; i < args.size(); ++i)
                g_cur->append(args[i]);
            std::cout << "built schema with " << (args.size() - 1) << " fields on " << nodeSummary(g_cur) << "\n";
        }

        else if (cmd == "loadjson") {
            if (args.size() < 2) { std::cout << "usage: loadjson <file>\n"; continue; }
            std::ifstream ifs(args[1]);
            if (!ifs.is_open()) { std::cout << "cannot open: " << args[1] << "\n"; continue; }
            try {
                json j = json::parse(ifs);
                int before = g_cur->count();
                json_build(g_cur, j);
                int added = g_cur->count() - before;
                std::cout << "loaded " << added << " children from " << args[1] << "\n";
            } catch (const json::exception& e) {
                std::cout << "JSON error: " << e.what() << "\n";
            }
        }

        else if (cmd == "savejson") {
            if (args.size() < 2) { std::cout << "usage: savejson <file>\n"; continue; }
            Node* target = g_cur;
            if (args.size() > 2) {
                target = g_cur->resolve(args[2]);
                if (!target) { std::cout << "not found: " << args[2] << "\n"; continue; }
            }
            json j = node_to_json(target);
            std::ofstream ofs(args[1]);
            if (!ofs.is_open()) { std::cout << "cannot write: " << args[1] << "\n"; continue; }
            ofs << j.dump(2) << "\n";
            std::cout << "saved to " << args[1] << "\n";
        }

        else if (cmd == "benchjson") {
            // benchjson <file> [reps]  — benchmark JSON → Node tree build
            if (args.size() < 2) { std::cout << "usage: benchjson <file> [reps]\n"; continue; }
            int reps = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 10;

            // Phase 1: file read + JSON parse (once)
            std::ifstream ifs(args[1]);
            if (!ifs.is_open()) { std::cout << "cannot open: " << args[1] << "\n"; continue; }
            json j;
            try {
                auto t0 = std::chrono::steady_clock::now();
                j = json::parse(ifs);
                auto t1 = std::chrono::steady_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::cout << "[bench] JSON parse: " << ms << " ms\n";
            } catch (const json::exception& e) {
                std::cout << "JSON error: " << e.what() << "\n"; continue;
            }
            ifs.close();

            // Phase 2: build tree N times (pure Node construction)
            {
                int totalNodes = 0;
                auto t0 = std::chrono::steady_clock::now();
                for (int r = 0; r < reps; ++r) {
                    Node tmp("bench_root");
                    json_build(&tmp, j);
                    totalNodes = tmp.count();
                    // ~Node destructor handles cleanup
                }
                auto t1 = std::chrono::steady_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::cout << "[bench] ve::Node build+destroy x" << reps << ": " << ms << " ms"
                          << "  (avg " << (ms / reps) << " ms, " << totalNodes << " top-level children)\n";
            }

            // Phase 3: build once and count total nodes recursively
            {
                Node tmp("bench_root");
                json_build(&tmp, j);
                // count all nodes recursively
                std::function<int(const Node*)> countAll = [&](const Node* n) -> int {
                    int c = 1;
                    for (auto* ch : *n) c += countAll(ch);
                    return c;
                };
                int total = countAll(&tmp) - 1;  // exclude root
                std::cout << "[bench] total nodes: " << total << "\n";
            }
        }

        else if (cmd == "showjson") {
            Node* target = g_cur;
            if (args.size() > 1) {
                target = g_cur->resolve(args[1]);
                if (!target) { std::cout << "not found: " << args[1] << "\n"; continue; }
            }
            json j = node_to_json(target);
            std::cout << j.dump(2) << "\n";
        }

        // ========== Unknown ==========
        else {
            std::cout << "unknown: " << cmd << "  (type 'help')\n";
        }
    }

    // clean orphans
    for (auto* o : g_orphans) delete o;
    g_orphans.clear();

    delete g_root;
    std::cout << "bye\n";
    return 0;
}

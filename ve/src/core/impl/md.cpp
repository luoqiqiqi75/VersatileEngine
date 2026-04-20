// ----------------------------------------------------------------------------
// md.cpp — Markdown export/import for Node
// ----------------------------------------------------------------------------
#include "ve/core/impl/md.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"

#include <sstream>
#include <algorithm>
#include <cctype>

namespace ve {
namespace impl::md {

// ============================================================================
// Helper: Clean node name for MD heading
// ============================================================================

static std::string cleanHeadingText(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (char c : text) {
        if (c == '*' || c == '_' || c == '`' || c == '[' || c == ']' ||
            c == '(' || c == ')' || c == '#' || c == '\\') {
            continue;
        }
        if (c == '/' || c == '|' || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '?' || c == '\t' || c == '\r' || c == '\n') {
            result += ' ';
        } else {
            result += c;
        }
    }

    // Trim leading/trailing spaces
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos) {
        return "";
    }
    size_t end = result.find_last_not_of(' ');
    return result.substr(start, end - start + 1);
}

static std::string stripNodeNameSuffix(const std::string& name)
{
    // Strip #N suffix (e.g., "Feature 1#0" → "Feature 1")
    size_t pos = name.find('#');
    if (pos != std::string::npos) {
        return name.substr(0, pos);
    }
    return name;
}

// ============================================================================
// Export: Node → MD
// ============================================================================

static void exportNodeRecursive(const Node* node, std::ostringstream& oss, int parentLevel,
                                const schema::ExportOptions& options)
{
    if (!node) {
        return;
    }

    // Determine actual level
    int actualLevel = parentLevel + 1;
    if (const Node* levelNode = node->find("_level")) {
        actualLevel = levelNode->get().toInt(actualLevel);
    }
    actualLevel = std::min(actualLevel, 6);  // MD max 6 levels

    // Write heading
    if (actualLevel > 0) {
        std::string heading(actualLevel, '#');

        // Get title: prefer _title, fallback to name
        std::string title;
        if (const Node* titleNode = node->find("_title")) {
            title = titleNode->get().toString();
        } else {
            title = stripNodeNameSuffix(node->name());
        }

        if (!title.empty()) {
            oss << heading << " " << title << "\n";
        }
    }

    // Write content from node value
    if (!node->get().isNull()) {
        std::string content = node->get().toString();
        if (!content.empty()) {
            oss << content;
            if (content.back() != '\n') {
                oss << "\n";
            }
        }
    }

    // Add blank line after heading+content
    if (actualLevel > 0) {
        oss << "\n";
    }

    // Export children (skip _ prefixed meta nodes)
    for (const Node* child : node->children()) {
        if (!child) {
            continue;
        }
        std::string childName = child->name();
        if (!childName.empty() && childName[0] == '_') {
            continue;
        }

        exportNodeRecursive(child, oss, actualLevel, options);
    }
}

std::string exportTree(const Node* node, int indent)
{
    schema::ExportOptions options;
    options.indent = indent;
    return exportTree(node, options);
}

std::string exportTree(const Node* node, const schema::ExportOptions& options)
{
    if (!node) {
        return "";
    }

    std::ostringstream oss;

    // Root node value as preamble (before first heading)
    if (!node->get().isNull() && !node->find("_level")) {
        std::string preamble = node->get().toString();
        if (!preamble.empty()) {
            oss << preamble;
            if (preamble.back() != '\n') {
                oss << "\n";
            }
            oss << "\n";
        }
    }

    // Export children as headings
    for (const Node* child : node->children()) {
        if (!child) {
            continue;
        }
        std::string childName = child->name();
        if (!childName.empty() && childName[0] == '_') {
            continue;
        }

        exportNodeRecursive(child, oss, 0, options);
    }

    return oss.str();
}

// ============================================================================
// Import: MD → Node
// ============================================================================

// Lightweight heading-based parser. No third-party dependency needed.
// Only ATX headings (# ... ######) are structural; everything else is content.

struct HeadingLine {
    int level;              // 1-6
    std::string rawTitle;   // original text (with inline formatting)
    std::string cleanTitle; // stripped for node name
};

static bool parseHeadingLine(const std::string& line, HeadingLine& out)
{
    size_t i = 0;
    while (i < line.size() && line[i] == ' ' && i < 3) {
        ++i;
    }
    if (i >= line.size() || line[i] != '#') {
        return false;
    }

    int level = 0;
    while (i < line.size() && line[i] == '#') {
        ++level;
        ++i;
    }
    if (level < 1 || level > 6) {
        return false;
    }
    // Must be followed by space or end of line
    if (i < line.size() && line[i] != ' ' && line[i] != '\t') {
        return false;
    }

    // Skip leading whitespace after #
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }

    std::string title = line.substr(i);
    // Strip trailing # (closing ATX heading)
    size_t end = title.find_last_not_of(" \t");
    if (end != std::string::npos) {
        size_t hashEnd = end;
        while (hashEnd > 0 && title[hashEnd] == '#') {
            --hashEnd;
        }
        if (hashEnd < end && (title[hashEnd] == ' ' || title[hashEnd] == '\t')) {
            title = title.substr(0, hashEnd + 1);
            end = title.find_last_not_of(" \t");
        }
    }
    if (end != std::string::npos) {
        title = title.substr(0, end + 1);
    } else {
        title.clear();
    }

    out.level = level;
    out.rawTitle = title;
    out.cleanTitle = cleanHeadingText(title);
    return true;
}

static bool isInsideCodeFence(const std::string& line, bool currentlyInFence, std::string& fenceMarker)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t') && i < 3) {
        ++i;
    }

    char fenceChar = 0;
    size_t fenceLen = 0;
    if (i < line.size() && (line[i] == '`' || line[i] == '~')) {
        fenceChar = line[i];
        while (i + fenceLen < line.size() && line[i + fenceLen] == fenceChar) {
            ++fenceLen;
        }
    }

    if (fenceLen < 3) {
        return currentlyInFence;
    }

    if (!currentlyInFence) {
        fenceMarker = std::string(fenceLen, fenceChar);
        return true;
    }

    // Closing fence must use same char and at least same length
    if (fenceChar == fenceMarker[0] && fenceLen >= fenceMarker.size()) {
        fenceMarker.clear();
        return false;
    }
    return true;
}

static std::string trimTrailingWhitespace(const std::string& s)
{
    size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return "";
    }
    return s.substr(0, end + 1);
}

static void flushContent(Node* target, const std::string& content)
{
    std::string trimmed = trimTrailingWhitespace(content);
    if (!trimmed.empty()) {
        target->set(Var(trimmed));
    }
}

static void importDirect(Node* root, const std::string& md)
{
    std::istringstream stream(md);
    std::string line;

    // levelStack[i] = {node, actual_md_level}
    struct StackEntry {
        Node* node;
        int level;
    };
    std::vector<StackEntry> levelStack;
    levelStack.push_back({root, 0});

    Node* currentNode = root;
    int currentLevel = 0;
    std::ostringstream contentBuf;
    bool inCodeFence = false;
    std::string fenceMarker;

    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Track code fences
        bool wasInFence = inCodeFence;
        inCodeFence = isInsideCodeFence(line, inCodeFence, fenceMarker);
        if (wasInFence || inCodeFence) {
            contentBuf << line << "\n";
            continue;
        }

        HeadingLine heading;
        if (parseHeadingLine(line, heading)) {
            // Flush accumulated content to current node
            flushContent(currentNode, contentBuf.str());
            contentBuf.str("");
            contentBuf.clear();

            // Find parent: pop stack until we find a level < heading.level
            while (levelStack.size() > 1 && levelStack.back().level >= heading.level) {
                levelStack.pop_back();
            }

            Node* parent = levelStack.back().node;
            int parentLevel = levelStack.back().level;
            int expectedLevel = parentLevel + 1;

            // Create new node
            Node* newNode = parent->append(heading.cleanTitle);

            // Store original title only if cleaned
            if (heading.cleanTitle != heading.rawTitle) {
                newNode->append("_title")->set(Var(heading.rawTitle));
            }

            // Store level only if jumped
            if (heading.level != expectedLevel) {
                newNode->append("_level")->set(Var(heading.level));
            }

            levelStack.push_back({newNode, heading.level});
            currentNode = newNode;
            currentLevel = heading.level;
        } else {
            contentBuf << line << "\n";
        }
    }

    // Flush remaining content
    flushContent(currentNode, contentBuf.str());
}

bool importTree(Node* node, const std::string& md)
{
    if (!node || md.empty()) {
        return false;
    }
    importDirect(node, md);
    return true;
}

bool importTree(Node* node, const std::string& md, const schema::ImportOptions& options)
{
    if (!node || md.empty()) {
        return false;
    }

    if (!options.auto_insert && !options.auto_remove && !options.auto_update) {
        importDirect(node, md);
        return true;
    }

    Node parsed("md_import");
    importDirect(&parsed, md);
    node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_update);
    return true;
}

} // namespace md
} // namespace ve

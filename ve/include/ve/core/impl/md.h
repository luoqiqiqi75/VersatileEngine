// ----------------------------------------------------------------------------
// ve/core/impl/md.h — Markdown serialization for Node
// ----------------------------------------------------------------------------
// Markdown <-> Node tree bidirectional conversion for AI-friendly document storage.
//
// Mapping rules:
// - MD heading → Node (name=cleaned title, value=raw title)
// - Heading level → _level child node (value: 1-6)
// - Content after heading → _content child node (value: text until next heading)
// - Special chars in heading → replaced with space in name
// - Inline formatting (**bold**, *italic*) → stripped from name, kept in value
//
// Example:
//   # Database Configuration
//   Main database configuration
//
//   ## MySQL
//   Production settings
//
// Converts to:
//   /Database Configuration (value: "Database Configuration")
//     /_level (value: 1)
//     /_content (value: "Main database configuration")
//     /MySQL (value: "MySQL")
//       /_level (value: 2)
//       /_content (value: "Production settings")
//
// Uses lightweight heading-based parser for import, custom generator for export.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"
#include <string>

namespace ve {

class Node;
namespace schema {
struct ImportOptions;
struct ExportOptions;
}

namespace impl::md {

// Node tree → MD string
VE_API std::string exportTree(const Node* node, int indent = 2);
VE_API std::string exportTree(const Node* node, const schema::ExportOptions& options);

// MD string → Node tree
VE_API bool importTree(Node* node, const std::string& md);
VE_API bool importTree(Node* node, const std::string& md, const schema::ImportOptions& options);

} // namespace md
} // namespace ve

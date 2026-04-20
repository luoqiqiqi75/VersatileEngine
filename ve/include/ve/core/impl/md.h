// ----------------------------------------------------------------------------
// ve/core/impl/md.h — Markdown serialization for Node
// ----------------------------------------------------------------------------
// Markdown <-> Node tree bidirectional conversion for AI-friendly document storage.
//
// Mapping rules:
// - MD heading → Node (name=cleaned title, value=content after heading)
// - Original title → _title child (only if name was cleaned)
// - Heading level → _level child (only if level jumped, stores actual level)
// - Special chars in heading → replaced with space in name
// - Inline formatting (**bold**, *italic*) → stripped from name
//
// Example:
//   # Database Configuration
//   Main database configuration
//
//   ## MySQL
//   Production settings
//
//   # Title1
//   ### Title3
//   Deep content
//
// Converts to:
//   /Database Configuration (value: "Main database configuration")
//     /MySQL (value: "Production settings")
//   /Title1 (value: null)
//     /Title3 (value: "Deep content")
//       /_level (value: 3)  # Jumped from level 1 to 3
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

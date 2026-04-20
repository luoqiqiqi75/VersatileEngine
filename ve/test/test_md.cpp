// test_md.cpp - Markdown Schema (MdS) export/import tests
#include "ve_test.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/impl/md.h"

using namespace ve;

// ============================================================================
// Import: basic heading parsing
// ============================================================================

VE_TEST(md_import_single_heading) {
    std::string md = "# Hello World\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));
    VE_ASSERT_EQ(root.count(), 1);

    Node* h = root.child("Hello World");
    VE_ASSERT(h != nullptr);
    VE_ASSERT_EQ(h->get().toString(), "Hello World");
    VE_ASSERT(h->find("_level") != nullptr);
    VE_ASSERT_EQ(h->find("_level")->get().toInt(), 1);
}

VE_TEST(md_import_heading_with_content) {
    std::string md =
        "# Section\n"
        "Some content here.\n"
        "More content.\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    Node* s = root.child("Section");
    VE_ASSERT(s != nullptr);
    VE_ASSERT(s->find("_content") != nullptr);
    std::string content = s->find("_content")->get().toString();
    VE_ASSERT(content.find("Some content here.") != std::string::npos);
    VE_ASSERT(content.find("More content.") != std::string::npos);
}

VE_TEST(md_import_nested_headings) {
    std::string md =
        "# Level 1\n"
        "## Level 2\n"
        "### Level 3\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    VE_ASSERT_EQ(root.count(), 1);
    Node* l1 = root.child("Level 1");
    VE_ASSERT(l1 != nullptr);
    VE_ASSERT_EQ(l1->find("_level")->get().toInt(), 1);

    Node* l2 = l1->child("Level 2");
    VE_ASSERT(l2 != nullptr);
    VE_ASSERT_EQ(l2->find("_level")->get().toInt(), 2);

    Node* l3 = l2->child("Level 3");
    VE_ASSERT(l3 != nullptr);
    VE_ASSERT_EQ(l3->find("_level")->get().toInt(), 3);
}

// ============================================================================
// Import: same-name nodes
// ============================================================================

VE_TEST(md_import_same_name) {
    std::string md =
        "# Feature\n"
        "First\n"
        "# Feature\n"
        "Second\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    VE_ASSERT_EQ(root.count("Feature"), 2);
    VE_ASSERT_EQ(root.child("Feature", 0)->find("_content")->get().toString(), "First");
    VE_ASSERT_EQ(root.child("Feature", 1)->find("_content")->get().toString(), "Second");
}

// ============================================================================
// Import: level jumps
// ============================================================================

VE_TEST(md_import_level_jump) {
    std::string md =
        "# A\n"
        "### B\n"
        "Content B\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    Node* a = root.child("A");
    VE_ASSERT(a != nullptr);
    VE_ASSERT_EQ(a->find("_level")->get().toInt(), 1);

    // B should be child of A despite level jump
    Node* b = a->child("B");
    VE_ASSERT(b != nullptr);
    VE_ASSERT_EQ(b->find("_level")->get().toInt(), 3);
    VE_ASSERT_EQ(b->find("_content")->get().toString(), "Content B");
}

// ============================================================================
// Import: preamble (content before first heading)
// ============================================================================

VE_TEST(md_import_preamble) {
    std::string md =
        "This is preamble text.\n"
        "\n"
        "# Section\n"
        "Content\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    // Preamble goes to root's _content
    Node* preamble = root.find("_content");
    VE_ASSERT(preamble != nullptr);
    VE_ASSERT(preamble->get().toString().find("preamble") != std::string::npos);

    Node* s = root.child("Section");
    VE_ASSERT(s != nullptr);
}

// ============================================================================
// Import: code fence protection
// ============================================================================

VE_TEST(md_import_code_fence) {
    std::string md =
        "# Real Heading\n"
        "```\n"
        "# Not a heading\n"
        "## Also not\n"
        "```\n"
        "After code\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    VE_ASSERT_EQ(root.count(), 1);
    Node* h = root.child("Real Heading");
    VE_ASSERT(h != nullptr);
    std::string content = h->find("_content")->get().toString();
    VE_ASSERT(content.find("# Not a heading") != std::string::npos);
    VE_ASSERT(content.find("After code") != std::string::npos);
}

// ============================================================================
// Import: inline formatting stripped from name
// ============================================================================

VE_TEST(md_import_strip_formatting) {
    std::string md = "# **Important** Feature\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    Node* h = root.child("Important Feature");
    VE_ASSERT(h != nullptr);
    VE_ASSERT_EQ(h->get().toString(), "**Important** Feature");
}

VE_TEST(md_import_slash_in_heading) {
    std::string md = "# TCP/IP Stack\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    Node* h = root.child("TCP IP Stack");
    VE_ASSERT(h != nullptr);
    VE_ASSERT_EQ(h->get().toString(), "TCP/IP Stack");
}

// ============================================================================
// Import: anonymous heading
// ============================================================================

VE_TEST(md_import_empty_heading) {
    std::string md =
        "#\n"
        "Content under empty heading\n";
    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    // Empty heading -> anonymous node
    VE_ASSERT_EQ(root.count(), 1);
}

// ============================================================================
// Export: basic
// ============================================================================

VE_TEST(md_export_basic) {
    Node root("root");
    Node* s1 = root.append("Section1");
    s1->set(Var("Section1"));
    s1->append("_level")->set(Var(1));
    s1->append("_content")->set(Var("Content 1"));

    Node* s2 = s1->append("Sub1");
    s2->set(Var("Sub1"));
    s2->append("_level")->set(Var(2));
    s2->append("_content")->set(Var("Content 1.1"));

    std::string md = impl::md::exportTree(&root);
    VE_ASSERT(md.find("# Section1") != std::string::npos);
    VE_ASSERT(md.find("Content 1") != std::string::npos);
    VE_ASSERT(md.find("## Sub1") != std::string::npos);
    VE_ASSERT(md.find("Content 1.1") != std::string::npos);
}

VE_TEST(md_export_preserves_level) {
    Node root("root");
    Node* a = root.append("A");
    a->set(Var("A"));
    a->append("_level")->set(Var(1));

    Node* b = a->append("B");
    b->set(Var("B"));
    b->append("_level")->set(Var(3));

    std::string md = impl::md::exportTree(&root);
    VE_ASSERT(md.find("# A") != std::string::npos);
    VE_ASSERT(md.find("### B") != std::string::npos);
}

// ============================================================================
// Roundtrip: MD -> Node -> MD -> Node
// ============================================================================

VE_TEST(md_roundtrip) {
    std::string md1 =
        "# Feature 1\n"
        "Description 1\n"
        "\n"
        "## Detail 1.1\n"
        "Content 1.1\n"
        "\n"
        "# Feature 2\n"
        "Description 2\n";

    Node root1("root1");
    VE_ASSERT(impl::md::importTree(&root1, md1));

    std::string md2 = impl::md::exportTree(&root1);

    Node root2("root2");
    VE_ASSERT(impl::md::importTree(&root2, md2));

    // Structure should match
    VE_ASSERT_EQ(root1.count("Feature 1"), root2.count("Feature 1"));
    VE_ASSERT_EQ(root1.count("Feature 2"), root2.count("Feature 2"));

    Node* f1a = root1.child("Feature 1");
    Node* f1b = root2.child("Feature 1");
    VE_ASSERT(f1a != nullptr);
    VE_ASSERT(f1b != nullptr);
    VE_ASSERT_EQ(f1a->find("_content")->get().toString(),
                 f1b->find("_content")->get().toString());
}

// ============================================================================
// SchemaTraits<MdS> integration
// ============================================================================

VE_TEST(md_schema_traits) {
    std::string md =
        "# Test\n"
        "Content\n";

    Node root("root");
    VE_ASSERT(schema::importAs<schema::MdS>(&root, md));

    Node* t = root.child("Test");
    VE_ASSERT(t != nullptr);

    std::string exported = schema::exportAs<schema::MdS>(&root);
    VE_ASSERT(exported.find("# Test") != std::string::npos);
    VE_ASSERT(exported.find("Content") != std::string::npos);
}

// ============================================================================
// Complex document
// ============================================================================

VE_TEST(md_import_complex_doc) {
    std::string md =
        "# VE HTTP Service Enhancement Plan\n"
        "\n"
        "## Context\n"
        "VersatileEngine provides basic Node tree access.\n"
        "\n"
        "## Design Principles\n"
        "\n"
        "### URL Parameters\n"
        "Keep path segments as primary.\n"
        "\n"
        "### Search Complexity\n"
        "Keep glob + substring.\n"
        "\n"
        "## Features\n"
        "\n"
        "### Feature 1\n"
        "Batch node read.\n"
        "\n"
        "### Feature 2\n"
        "Tree depth control.\n";

    Node root("root");
    VE_ASSERT(impl::md::importTree(&root, md));

    Node* plan = root.child("VE HTTP Service Enhancement Plan");
    VE_ASSERT(plan != nullptr);

    Node* ctx = plan->child("Context");
    VE_ASSERT(ctx != nullptr);
    VE_ASSERT(ctx->find("_content")->get().toString().find("basic Node tree") != std::string::npos);

    Node* principles = plan->child("Design Principles");
    VE_ASSERT(principles != nullptr);
    VE_ASSERT(principles->child("URL Parameters") != nullptr);
    VE_ASSERT(principles->child("Search Complexity") != nullptr);

    Node* features = plan->child("Features");
    VE_ASSERT(features != nullptr);
    VE_ASSERT(features->child("Feature 1") != nullptr);
    VE_ASSERT(features->child("Feature 2") != nullptr);
    VE_ASSERT_EQ(features->child("Feature 1")->find("_content")->get().toString(), "Batch node read.");
}

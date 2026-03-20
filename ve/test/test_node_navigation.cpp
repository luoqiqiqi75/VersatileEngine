// test_node_navigation.cpp — parent, indexOf, sibling, prev/next, first/last, isAncestorOf
#include "ve_test.h"
#include "ve/core/node.h"

using namespace ve;

// ============================================================================
// first / last
// ============================================================================

VE_TEST(node_first_last) {
    Node root("root");
    Node* a = root.append("a");
    root.append("b");
    Node* c = root.append("c");

    VE_ASSERT_EQ(root.first(), a);
    VE_ASSERT_EQ(root.last(), c);

    Node empty("empty");
    VE_ASSERT(empty.first() == nullptr);
    VE_ASSERT(empty.last() == nullptr);
}

// ============================================================================
// Parent navigation
// ============================================================================

VE_TEST(node_parent) {
    Node root("root");
    Node* a = root.append("a");
    VE_ASSERT(a->parent() == &root);
    VE_ASSERT(root.parent() == nullptr);
}

VE_TEST(node_parent_level) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    VE_ASSERT(c->parent() == b);
    VE_ASSERT(c->parent(0) == b);
    VE_ASSERT(c->parent(1) == a);
    VE_ASSERT(c->parent(2) == &root);
    VE_ASSERT(c->parent(3) == nullptr);
}

// ============================================================================
// indexOf
// ============================================================================

VE_TEST(node_indexOf_global) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");
    Node* c = root.append("c");

    VE_ASSERT_EQ(root.indexOf(a), 0);
    VE_ASSERT_EQ(root.indexOf(b), 1);
    VE_ASSERT_EQ(root.indexOf(c), 2);

    Node other("other");
    VE_ASSERT_EQ(root.indexOf(&other), -1);
    VE_ASSERT_EQ(root.indexOf(nullptr), -1);
}

VE_TEST(node_indexOf_same_name) {
    Node root("root");
    root.append("");           // anon, index 0
    root.append("");           // anon, index 1
    Node* x0 = root.append("x");  // index 2
    Node* x1 = root.append("x");  // index 3
    Node* x2 = root.append("x");  // index 4

    // indexOf returns global index (flat vector position)
    VE_ASSERT_EQ(root.indexOf(x0), 2);
    VE_ASSERT_EQ(root.indexOf(x1), 3);
    VE_ASSERT_EQ(root.indexOf(x2), 4);
}

VE_TEST(node_indexOf_mixed) {
    Node root("root");
    Node* a0 = root.append("");   // index 0
    Node* a1 = root.append("");   // index 1
    Node* n0 = root.append("n");  // index 2
    Node* a2 = root.append("");   // index 3

    // true insertion order: a0, a1, n0, a2
    VE_ASSERT_EQ(root.indexOf(a0), 0);
    VE_ASSERT_EQ(root.indexOf(a1), 1);
    VE_ASSERT_EQ(root.indexOf(n0), 2);
    VE_ASSERT_EQ(root.indexOf(a2), 3);
}

// ============================================================================
// Sibling
// ============================================================================

VE_TEST(node_sibling_global) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");
    Node* c = root.append("c");

    VE_ASSERT_EQ(a->sibling(1), b);
    VE_ASSERT_EQ(a->sibling(2), c);
    VE_ASSERT_EQ(c->sibling(-1), b);
    VE_ASSERT_EQ(c->sibling(-2), a);
    VE_ASSERT(a->sibling(-1) == nullptr);
    VE_ASSERT(c->sibling(1) == nullptr);
}

VE_TEST(node_sibling_same_name) {
    Node root("root");
    Node* x0 = root.append("x");
    Node* x1 = root.append("x");
    Node* x2 = root.append("x");

    // consecutive same-name nodes — sibling(1) is the next in flat vector
    VE_ASSERT_EQ(x0->sibling(1), x1);
    VE_ASSERT_EQ(x1->sibling(1), x2);
    VE_ASSERT_EQ(x2->sibling(-1), x1);
    VE_ASSERT(x0->sibling(-1) == nullptr);
    VE_ASSERT(x2->sibling(1) == nullptr);
}

VE_TEST(node_sibling_cross_group) {
    Node root("root");
    Node* a = root.append("");
    Node* b = root.append("");
    Node* x = root.append("x");

    // flat vector: [a, b, x] → global indices 0, 1, 2
    VE_ASSERT_EQ(b->sibling(1), x);    // cross name
    VE_ASSERT_EQ(x->sibling(-1), b);   // cross name
    VE_ASSERT(a->sibling(-1) == nullptr);
    VE_ASSERT(x->sibling(1) == nullptr);
}

// ============================================================================
// prev / next
// ============================================================================

VE_TEST(node_prev_next) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");
    Node* c = root.append("c");

    VE_ASSERT(a->prev() == nullptr);
    VE_ASSERT_EQ(a->next(), b);
    VE_ASSERT_EQ(b->prev(), a);
    VE_ASSERT_EQ(b->next(), c);
    VE_ASSERT_EQ(c->prev(), b);
    VE_ASSERT(c->next() == nullptr);
}

VE_TEST(node_prev_next_mixed) {
    Node root("root");
    Node* a0 = root.append("");    // index 0
    Node* x0 = root.append("x");  // index 1
    Node* x1 = root.append("x");  // index 2

    // flat order: a0, x0, x1
    VE_ASSERT(a0->prev() == nullptr);
    VE_ASSERT_EQ(a0->next(), x0);
    VE_ASSERT_EQ(x0->prev(), a0);
    VE_ASSERT_EQ(x0->next(), x1);
    VE_ASSERT_EQ(x1->prev(), x0);
    VE_ASSERT(x1->next() == nullptr);
}

VE_TEST(node_sibling_no_parent) {
    Node n("n");
    VE_ASSERT(n.sibling(1) == nullptr);
    VE_ASSERT(n.prev() == nullptr);
    VE_ASSERT(n.next() == nullptr);
}

// ============================================================================
// isAncestorOf
// ============================================================================

VE_TEST(node_isAncestorOf) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    VE_ASSERT(root.isAncestorOf(a));
    VE_ASSERT(root.isAncestorOf(b));
    VE_ASSERT(root.isAncestorOf(c));
    VE_ASSERT(a->isAncestorOf(b));
    VE_ASSERT(a->isAncestorOf(c));
    VE_ASSERT(b->isAncestorOf(c));

    VE_ASSERT(!c->isAncestorOf(a));
    VE_ASSERT(!b->isAncestorOf(a));
    VE_ASSERT(!a->isAncestorOf(&root));
    VE_ASSERT(!root.isAncestorOf(&root));
    VE_ASSERT(!root.isAncestorOf(nullptr));
}

VE_TEST(node_isAncestorOf_sibling) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT(!a->isAncestorOf(b));
    VE_ASSERT(!b->isAncestorOf(a));
}

// From the software distribution accompanying the textbook
// "A Practical Introduction to Data Structures and Algorithm Analysis,
// Third Edition (C++)" by Clifford A. Shaffer.
// Source code Copyright (C) 2007-2011 by Clifford A. Shaffer.

// HuffTree is a template of two parameters: the element
//  type being coded and a comparator for two such elements.

#pragma once
#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>
#include "huffnode.h"

template<typename E>
class HuffTree {
private:
    std::unique_ptr<HuffNode<E> > Root; // Tree root
public:
    using NodePtr = std::unique_ptr<HuffNode<E> >;

    HuffTree(const E &val, int freq) // Leaf constructor
        : Root(std::make_unique<LeafNode<E> >(val, freq)) {
    }

    explicit HuffTree(NodePtr root)
        : Root(std::move(root)) {
        if (Root == nullptr) {
            throw std::invalid_argument("HuffTree: root cannot be null");
        }
    }

    // Internal node constructor
    HuffTree(
        std::unique_ptr<HuffTree<E> > l,
        std::unique_ptr<HuffTree<E> > r
    ) {
        if (l == nullptr || r == nullptr ||
            l->Root == nullptr || r->Root == nullptr) {
            throw std::invalid_argument(
                "HuffTree: child tree cannot be null"
            );
        }

        // 将子树的根节点所有权转移到新树，从而子树可以自由析构
        Root = std::make_unique<IntlNode<E> >(
            std::move(l->Root),
            std::move(r->Root)
        );
    }

    ~HuffTree() = default; // Destructor
    HuffTree(const HuffTree &) = delete;

    HuffTree &operator=(const HuffTree &) = delete;

    HuffTree(HuffTree &&) noexcept = default;

    HuffTree &operator=(HuffTree &&) noexcept = default;

    HuffNode<E> *root() const { return Root.get(); } // Get root
    [[nodiscard]] int weight() const {
        assert(Root != nullptr);
        return Root->weight();
    } // Root weight
};

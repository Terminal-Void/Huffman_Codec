// From the software distribution accompanying the textbook
// "A Practical Introduction to Data Structures and Algorithm Analysis,
// Third Edition (C++)" by Clifford A. Shaffer.
// Source code Copyright (C) 2007-2011 by Clifford A. Shaffer.

// Huffman tree node abstract base class
#pragma once
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

template<typename E>
class HuffNode {
public:
    virtual ~HuffNode() = default; // Base destructor
    virtual int weight() = 0; // Return frequency
    virtual bool isLeaf() = 0; // Determine type
};


template<typename E> // Internal node subclass
class IntlNode : public HuffNode<E> {
private:
    using NodePtr = std::unique_ptr<HuffNode<E> >;
    NodePtr lc; // Left child
    NodePtr rc; // Right child
    int wgt; // Subtree weight

    static int sum_weights(const NodePtr &l, const NodePtr &r) {
        if (l == nullptr || r == nullptr) {
            throw std::invalid_argument("IntlNode: child node cannot be null");
        }
        const int lw = l->weight();
        const int rw = r->weight();
        if ((lw >= 0 && rw > std::numeric_limits<int>::max() - lw) ||
            (lw < 0 && rw < std::numeric_limits<int>::min() - lw)) {
            throw std::overflow_error("IntlNode: weight overflow");
        }
        return lw + rw;
    }

public:
    IntlNode(NodePtr l, NodePtr r)
        : lc(std::move(l)),
          rc(std::move(r)),
          wgt(sum_weights(lc, rc)) {
    }

    int weight() override { return wgt; }
    bool isLeaf() override { return false; }
    HuffNode<E> *left() const { return lc.get(); }
    void setLeft(NodePtr b) { lc = std::move(b); }
    HuffNode<E> *right() const { return rc.get(); }
    void setRight(NodePtr b) { rc = std::move(b); }
};

template<typename E> // Leaf node subclass
class LeafNode : public HuffNode<E> {
private:
    E it; // Value
    int wgt; // Weight
public:
    LeafNode(const E &val, const int freq) // Constructor
        : it(val), wgt(freq) {
    }

    int weight() override { return wgt; }
    E val() const { return it; }
    bool isLeaf() override { return true; }
};

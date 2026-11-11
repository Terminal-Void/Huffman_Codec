// From the software distribution accompanying the textbook
// "A Practical Introduction to Data Structures and Algorithm Analysis,
// Third Edition (C++)" by Clifford A. Shaffer.
// Source code Copyright (C) 2007-2011 by Clifford A. Shaffer.

// Huffman tree node abstract base class
#pragma once
#include <memory>
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
public:
    IntlNode(NodePtr l, NodePtr r) : lc(std::move(l)),
                                     rc(std::move(r)),
                                     wgt(lc->weight() + rc->weight()) {
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
    {
        it = val;
        wgt = freq;
    }

    int weight() override { return wgt; }
    E val() const { return it; }
    bool isLeaf() override { return true; }
};

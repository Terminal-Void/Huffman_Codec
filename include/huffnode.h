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

// 说明：
// 1. 生命周期与所有权改造：作业所给定的 HuffNode/IntlNode/HuffTree 实现全程使用裸指针，
//    且 ~HuffTree() 为空、IntlNode 无析构函数，整棵树一旦使用完成便会直接内存泄漏；
//    同时原版合并子树时直接共享 l->root()/r->root() 而不置空原句柄，若仅简单补写 delete 又会触发双重释放
//    故在保留类层次与 virtual weight()/isLeaf() 接口的前提下，配合 HuffTree::Root 将 lc/rc 统一改为 std::unique_ptr 独占管理：
//    子树合并、setLeft/setRight 覆盖旧子节点以及整树析构均由 RAII 自动完成所有权转移与释放；
//    left()/right() 返回非所有权观察裸指针（保持非 const，以便遍历时继续调用非 const 的 weight()/isLeaf()），不转移所有权。
// 2. 整型权重与空指针检查：基类接口规定 weight() 返回 32 位有符号 int（教材预设场景为普通文本），
//    树节点能表示的最大总字节频次受限于 INT_MAX（约 2 GiB）；为兼容作业指定的节点接口未改动返回类型
//    另原版 IntlNode 构造时直接解引用 l/r 且 setLeft/setRight 未重算 wgt；
//    本实现经 sum_weights 统一检查空子指针、有符号 int 加法溢出，并在替换左右孩子时同步更新 wgt
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

    // 计算左右子树权重和并检测溢出：
    // 因接口规定 weight() 返回有符号 int，而 C++ 标准中有符号整数溢出属于未定义行为（UB）
    // 故在构造合并前显式检查空指针以及正负双向加法溢出
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
    void setLeft(NodePtr b) {
        const int new_weight = sum_weights(b, rc);
        lc = std::move(b);
        wgt = new_weight;
    }
    HuffNode<E> *right() const { return rc.get(); }
    void setRight(NodePtr b) {
        const int new_weight = sum_weights(lc, b);
        rc = std::move(b);
        wgt = new_weight;
    }
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

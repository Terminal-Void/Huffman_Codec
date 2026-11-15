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
// 1. 内存所有权改造：原版 IntlNode 持有裸指针 lc/rc，且未按现代 C++ 管理子节点生命周期
//    在建树失败抛出异常或树重建时容易引发内存泄漏与浅拷贝双重释放
//    故在保留原有类层次与虚函数接口的前提下，将左右子节点替换为 std::unique_ptr
//    使整棵哈夫曼树满足 RAII 自动递归析构；left()/right() 仅返回只读观察裸指针用于遍历，不转移所有权
// 2. 整型权重限制：基类接口规定 weight() 返回 32 位有符号 int（教材预设场景为普通文本）
//    因此树节点能表示的最大总字节频次受限于 INT_MAX（约 2 GiB）；因需兼容作业指定的节点接口而未改动返回类型
//    只在合并节点时显式拦截溢出
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

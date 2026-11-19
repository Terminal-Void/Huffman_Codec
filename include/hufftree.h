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

// 说明：
// 1. 所有权与析构修复：作业要求提供的实现中 Root 为裸指针且 ~HuffTree() 为空、合并构造 HuffTree(HuffTree*, HuffTree*)
//    直接将 l->root()/r->root() 挂入新 IntlNode 而不转移或置空原句柄，外层 delete 子树壳子完全依赖析构为空，导致整树内存泄漏；
//    因此将 Root 改为 std::unique_ptr<HuffNode<E>>，~HuffTree() = default 递归析构，合并构造改为接收 unique_ptr<HuffTree<E>>
//    并通过 std::move(l->Root)/std::move(r->Root) 显式移交所有权；同时禁用拷贝、启用默认移动语义，防止浅拷贝冲突。
//    root() 仅返回非所有权观察裸指针，非 const 以兼容基类非 const 虚函数调用
// 2. 构造接口完善：原版叶子构造 HuffTree(E& val, int freq) 使用非 const 引用导致无法直接传入临时右值，改为 const E&；
//    并补充 explicit HuffTree(NodePtr root)，供解压端从配置文件拼装好底层节点后直接打包为 HuffTree 对象
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
        // 正常构造的树 Root 必非空；若对象已被 std::move 转移所有权（例如子树合并入新父节点后），
        // 其内部 Root 已置为 nullptr，此时不可再访问 weight()
        assert(Root != nullptr);
        return Root->weight();
    } // Root weight
};

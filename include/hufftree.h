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

// 说明
// 1. 补充 explicit HuffTree(NodePtr root) 构造函数：教材原本只提供“单叶构造”和“双子树合并构造”，仅够编码建树使用；
//    解压端从配置文件由底向上拼装出底层节点树后，需要直接将根节点句柄打包为 HuffTree 对象，故增加该构造入口
// 2. 禁用拷贝、启用移动语义：HuffTree 独占整棵节点树的唯一所有权，禁用拷贝可杜绝整树深拷贝开销与所有权模糊；
//    保留默认移动构造/移动赋值，支持树对象放入 std::vector<TreeHandle> 构成的小根堆中低成本交换所有权
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

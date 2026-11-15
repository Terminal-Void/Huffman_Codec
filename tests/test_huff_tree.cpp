#include "catch_amalgamated.hpp"

#include "huffman_types.h"
#include "huffnode.h"
#include "hufftree.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

TEST_CASE("哈夫曼节点基本操作与异常检查", "[hufftree]") {
    SECTION("叶节点与内部节点构造及子节点替换") {
        auto left = std::make_unique<LeafNode<Byte>>(Byte{'A'}, 12);
        auto right = std::make_unique<LeafNode<Byte>>(Byte{'B'}, 30);

        REQUIRE(left->isLeaf());
        REQUIRE(left->val() == Byte{'A'});
        REQUIRE(left->weight() == 12);

        IntlNode<Byte> parent(std::move(left), std::move(right));
        REQUIRE_FALSE(parent.isLeaf());
        REQUIRE(parent.weight() == 42);
        REQUIRE(parent.left()->weight() == 12);
        REQUIRE(parent.right()->weight() == 30);

        // 替换子节点时由 unique_ptr 自动释放旧节点
        parent.setLeft(std::make_unique<LeafNode<Byte>>(Byte{'C'}, 8));
        parent.setRight(std::make_unique<LeafNode<Byte>>(Byte{'D'}, 9));
        REQUIRE(parent.left()->weight() == 8);
        REQUIRE(parent.right()->weight() == 9);
    }

    SECTION("空子节点与权重溢出异常") {
        auto leaf = std::make_unique<LeafNode<Byte>>(Byte{'A'}, 1);
        REQUIRE_THROWS_AS(
            (IntlNode<Byte>(nullptr, std::move(leaf))),
            std::invalid_argument
        );
        REQUIRE_THROWS_AS(
            (IntlNode<Byte>(std::make_unique<LeafNode<Byte>>(Byte{'B'}, 1), nullptr)),
            std::invalid_argument
        );

        auto max_node = std::make_unique<LeafNode<Byte>>(
            Byte{'A'}, std::numeric_limits<int>::max()
        );
        auto plus_one = std::make_unique<LeafNode<Byte>>(Byte{'B'}, 1);
        REQUIRE_THROWS_AS(
            (IntlNode<Byte>(std::move(max_node), std::move(plus_one))),
            std::overflow_error
        );
    }
}

TEST_CASE("HuffTree 构造、合并与移动", "[hufftree]") {
    auto t1 = std::make_unique<HuffTree<Byte>>(Byte{'E'}, 120);
    auto t2 = std::make_unique<HuffTree<Byte>>(Byte{'U'}, 37);
    REQUIRE(t1->weight() == 120);
    REQUIRE(t1->root()->isLeaf());

    // 合并两棵树
    HuffTree<Byte> merged(std::move(t1), std::move(t2));
    REQUIRE(merged.weight() == 157);
    REQUIRE_FALSE(merged.root()->isLeaf());

    // 测试移动构造与移动赋值
    HuffTree<Byte> moved(std::move(merged));
    REQUIRE(moved.weight() == 157);
    HuffTree<Byte> assigned(Byte{'X'}, 1);
    assigned = std::move(moved);
    REQUIRE(assigned.weight() == 157);

    REQUIRE_THROWS_AS(HuffTree<Byte>(HuffTree<Byte>::NodePtr{nullptr}), std::invalid_argument);
    REQUIRE_THROWS_AS(
        HuffTree<Byte>(nullptr, std::make_unique<HuffTree<Byte>>(Byte{'A'}, 1)),
        std::invalid_argument
    );
}

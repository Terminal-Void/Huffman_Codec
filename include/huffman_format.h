#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iosfwd>
#include <memory>

#include "huffman_types.h"
#include "hufftree.h"

using TreeHandle = std::unique_ptr<HuffTree<Byte> >;

// 压缩或解压任务相关文件路径组
struct ArchivePaths {
    std::filesystem::path archive; // 压缩后的位流文件：<原文件>.huf
    std::filesystem::path shape;   // 树形先序结构文件：<原文件>.config1.txt（'1' 内部节点，'0' 叶节点）
    std::filesystem::path leaves;  // 叶节点字符序列文件：<原文件>.config2.txt
    std::filesystem::path decoded; // 解压输出文件：<stem>.decoded<ext>

    static ArchivePaths from_input(const std::filesystem::path &input);

    static ArchivePaths from_archive(const std::filesystem::path &archive);
};

// 压缩文件头（固定 20 字节）：4 字节魔数 "HUF1" + 8 字节小端 original_size + 8 字节小端 payload_bits
//
// 哈夫曼编码长度以“位（bit）”为单位，而操作系统文件以“字节（8 bits）”对齐存储，末字节往往需要补零凑整
// 若无原文件长度信息，解压端读取末尾字节时无法分辨有效编码位与填充零位——
// 当某符号码字恰好全为 0（例如高频字符编码 "0" 或单字符文件）时，尾部填充位会被误解码出 1~7 个多余字符
// 因此文件头记录 original_size 作为解码精确停机条件，同时记录 payload_bits 用于核对读取总位数与文件体字节长度是否自洽
struct ArchiveHeader {
    std::uint64_t original_size = 0;
    std::uint64_t payload_bits = 0;

    void write(std::ostream &output) const;

    static ArchiveHeader read(std::istream &input);
};

// 按先序遍历将哈夫曼树导出至结构文件与叶节点文件
void write_tree_config(const HuffTree<Byte> &tree, std::ostream &shape,
                       std::ostream &leaves);

// 以二进制只读方式打开两个配置文件，调用 GenHuffTreeByConfigFile 重建哈夫曼树
TreeHandle load_tree_config(const std::filesystem::path &shape_path,
                            const std::filesystem::path &leaves_path);

// 根据两个已打开的树配置文件流重建哈夫曼树
// 作业要求原型为 BNode* GenHuffTreeByConfigFile(FILE* fp1, FILE* fp2)
// 一个是要求使用的 huffnode / hufftree 实现不是 BNode，另一个是该实现的所有权语义完全不规范，默认析构函数泄露内存
// 这里用智能指针 TreeHandle (std::unique_ptr<HuffTree<Byte> >) 返回并接管整棵树的所有权
// 配合对 huffnode / hufftree 的智能指针和所有权改造，实现内存安全
// fp1 fp2 更名为 shape_file leaves_file 以加强可读性

TreeHandle GenHuffTreeByConfigFile(FILE *shape_file, FILE *leaves_file);

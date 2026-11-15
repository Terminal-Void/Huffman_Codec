#pragma once

#include <filesystem>

#include "huffman_format.h"

// 压缩指定普通文件，输出 <原文件>.huf 及同前缀的 .config1.txt / .config2.txt
// 采用两次流式处理以保持 O(1) 额外内存（不将整文件载入内存）
// 第一趟统计 256 字节频率并建树写出头部与配置文件，第二趟查表进行流式编码
// 注：空文件不存在任何有效字符分布，无法构造合法 HuffTree，将抛出 std::runtime_error 拒绝处理
ArchivePaths compress_file(const std::filesystem::path &input_path);

// 校验 20 字节 .huf 文件头，读取同前缀的 .config1.txt / .config2.txt 重建解码树，并解压缩至 <stem>.decoded<ext>
ArchivePaths decompress_file(const std::filesystem::path &archive_path);

#pragma once

#include <array>
#include <cstdint>
#include <vector>

// 使用 8 位无符号单字节（0~255）
// 若直接以 char 作为频率表或码表下标，在 char 默认有符号的平台上遇到 >= 0x80 的字节（如 UTF-8 中文或二进制数据）会产生负索引越界；
// 符号域固定为 256 个字节后，频率表与码表均可采用定长 std::array 实现 O(1) 直接寻址，且兼容文本与任意二进制文件
using Byte = std::uint8_t;

// 频率统计采用 64 位无符号整数，保证统计阶段本身可处理大文件；
// 但受限于教材提供的 HuffNode::weight() 签名返回有符号 int，建树合并前仍需额外检查总频次不超过 INT_MAX
using Frequency = std::uint64_t;
using FrequencyTable = std::array<Frequency, 256>;

// 单个字节对应的变长前缀编码序列：false 表示左分支 '0'，true 表示右分支 '1'
// std::vector<bool> 按位紧缩存储；256 种符号在极端偏斜频率分布下最长码长可达 255 位，
// 写出阶段会分批打包进 uint64_t 后交由 BitWriter 写入字节流
struct Codeword {
    std::vector<bool> bits;
};

using CodeTable = std::array<Codeword, 256>;

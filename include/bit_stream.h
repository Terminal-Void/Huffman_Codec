//
// Created by Terminal Void on 2026/11/9.
//

#pragma once

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <istream>
#include <ostream>
#include <stdexcept>

// 分支预测提示宏
#if defined(__GNUC__) || defined(__clang__)
#define BITIO_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define BITIO_UNLIKELY(x) (x)
#endif

namespace bitio {
    // 位序定义 (BitOrder)
    enum class BitOrder {
        LSB, // 低位优先
        MSB // 高位优先
    };

    // BasicBitWriter: 编译期多态按位写入器
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitWriter {
    private:
        std::ostream &out_;
        uint64_t buffer_ = 0; // 64位 buffer
        int buffer_bit_count_ = 0; // 当前 buffer 中残余的有效位数 [0, 7]

    public:
        explicit BasicBitWriter(std::ostream &out) noexcept : out_(out) {
        }

        // 写入器不拥有 out_；析构时只做尽力刷新。
        // 正常流程仍应显式调用 flush()，这样写入错误可以传回调用方。
        ~BasicBitWriter() noexcept {
            try {
                flush();
            } catch (...) {
            }
        }

        BasicBitWriter(const BasicBitWriter &) = delete;

        BasicBitWriter &operator=(const BasicBitWriter &) = delete;

        // 热路径接口：写入 0 ~ 56 位
        void write_bits_fast(uint64_t value, const int num_bits) {
            assert(num_bits >= 0 && num_bits <= 56 && "write_bits_fast: num_bits must be in [0, 56]");
            if (num_bits == 0) return;

            // 掩码清理：消除高于 num_bits 的脏数据
            // 因 num_bits <= 56 而不会触发 << 64 的 UB
            value &= (1ULL << num_bits) - 1;

            if constexpr (Order == BitOrder::LSB) {
                buffer_ |= (value << buffer_bit_count_);
            } else {
                buffer_ |= (value << (64 - buffer_bit_count_ - num_bits));
            }
            buffer_bit_count_ += num_bits;

            if (buffer_bit_count_ >= 8) {
                flush_bytes();
            }
        }

        // 通用接口：支持 0 ~ 64 位
        // 超过 56 位时自动走冷分支拆分
        void write_bits(const uint64_t value, const int num_bits) {
            assert(num_bits >= 0 && num_bits <= 64 && "write_bits: num_bits must be in [0, 64]");

            if (BITIO_UNLIKELY(num_bits > 56)) {
                if constexpr (Order == BitOrder::LSB) {
                    write_bits_fast(value, 32);
                    write_bits_fast(value >> 32, num_bits - 32);
                } else {
                    int high_bits = num_bits - 32;
                    write_bits_fast(value >> 32, high_bits);
                    write_bits_fast(value, 32);
                }
                return;
            }

            write_bits_fast(value, num_bits);
        }

        // 写入单个 bit (0 或 1)
        void write_bit(const bool bit) {
            if constexpr (Order == BitOrder::LSB) {
                buffer_ |= (static_cast<uint64_t>(bit) << buffer_bit_count_);
            } else {
                buffer_ |= (static_cast<uint64_t>(bit) << (63 - buffer_bit_count_));
            }
            buffer_bit_count_ += 1;

            if (buffer_bit_count_ >= 8) {
                flush_bytes();
            }
        }

        // 写入完整的 1 个字节（无需当前处于字节对齐状态）
        void write_byte(const uint8_t byte) {
            write_bits_fast(byte, 8);
        }

        // 字节对齐：将不足 8 位的残余 bit 补 0 凑整并推入输出流（不强制刷盘）
        void align_to_byte() {
            flush_bytes();
            if (buffer_bit_count_ > 0) {
                uint8_t byte = 0;
                if constexpr (Order == BitOrder::LSB) {
                    byte = static_cast<uint8_t>(buffer_ & 0xFF);
                } else {
                    byte = static_cast<uint8_t>(buffer_ >> 56);
                }
                out_.put(static_cast<char>(byte));
                check_output("bitio::BitWriter: failed to write aligned byte");
                buffer_ = 0;
                buffer_bit_count_ = 0;
            }
        }

        // 对齐残余位并刷新底层文件流
        void flush() {
            align_to_byte();
            out_.flush();
            check_output("bitio::BitWriter: failed to flush output stream");
        }

        // 查询当前蓄水池中尚未落流的残余位数 (0 ~ 7)
        [[nodiscard]] int pending_bits() const noexcept {
            return buffer_bit_count_;
        }

        static constexpr BitOrder bit_order() noexcept {
            return Order;
        }

    private:
        void flush_bytes() {
            while (buffer_bit_count_ >= 8) {
                uint8_t byte = 0;
                if constexpr (Order == BitOrder::LSB) {
                    byte = static_cast<uint8_t>(buffer_ & 0xFF);
                    buffer_ >>= 8;
                } else {
                    byte = static_cast<uint8_t>(buffer_ >> 56);
                    buffer_ <<= 8;
                }
                out_.put(static_cast<char>(byte));
                check_output("bitio::BitWriter: failed to write byte");
                buffer_bit_count_ -= 8;
            }
        }

        void check_output(const char *message) const {
            if (BITIO_UNLIKELY(!out_)) {
                throw std::ios_base::failure(message);
            }
        }
    };


    // BasicBitReader: 编译期多态 按位读取器
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitReader {
    private:
        std::istream &in_;
        uint64_t buffer_ = 0; // 64位寄存器蓄水池
        int buffer_bit_count_ = 0; // 当前蓄水池中可读的有效位数

    public:
        explicit BasicBitReader(std::istream &in) noexcept : in_(in) {
        }

        BasicBitReader(const BasicBitReader &) = delete;

        BasicBitReader &operator=(const BasicBitReader &) = delete;


        // 核心热路径接口：读取 0 ~ 56 位

        uint64_t read_bits_fast(const int num_bits) {
            const uint64_t result = peek_bits(num_bits);
            consume_bits(num_bits);
            return result;
        }


        // 通用安全接口：完整支持 0 ~ 64 位（超过 56 位时自动走冷分支拼接）
        uint64_t read_bits(const int num_bits) {
            assert(num_bits >= 0 && num_bits <= 64 && "read_bits: num_bits must be in [0, 64]");

            if (BITIO_UNLIKELY(num_bits > 56)) {
                if constexpr (Order == BitOrder::LSB) {
                    const uint64_t low = read_bits_fast(32);
                    const uint64_t high = read_bits_fast(num_bits - 32);
                    return low | (high << 32);
                } else {
                    const int high_bits = num_bits - 32;
                    const uint64_t high = read_bits_fast(high_bits);
                    const uint64_t low = read_bits_fast(32);
                    return (high << 32) | low;
                }
            }

            return read_bits_fast(num_bits);
        }

        // 读取单个 bit
        bool read_bit() {
            if (buffer_bit_count_ == 0) {
                refill(1);
            }
            bool bit = false;
            if constexpr (Order == BitOrder::LSB) {
                bit = (buffer_ & 1ULL) != 0;
                buffer_ >>= 1;
            } else {
                bit = (buffer_ >> 63) != 0;
                buffer_ <<= 1;
            }
            buffer_bit_count_ -= 1;
            return bit;
        }

        // 读取完整的 1 个字节（无需当前处于字节对齐状态）
        uint8_t read_byte() {
            return static_cast<uint8_t>(read_bits_fast(8));
        }

        // 预览接下来的 num_bits 位（0 ~ 56），但不消耗蓄水池（用于查表法快速解码）
        uint64_t peek_bits(const int num_bits) {
            assert(num_bits >= 0 && num_bits <= 56 && "peek_bits: num_bits must be in [0, 56]");
            if (num_bits == 0) return 0;

            if (buffer_bit_count_ < num_bits) {
                refill(num_bits);
            }

            if constexpr (Order == BitOrder::LSB) {
                return buffer_ & ((1ULL << num_bits) - 1);
            } else {
                return buffer_ >> (64 - num_bits);
            }
        }

        // 配合 peek_bits 使用：丢弃已预览的 num_bits 位
        void consume_bits(const int num_bits) {
            assert(num_bits >= 0 && num_bits <= buffer_bit_count_ && "consume_bits: not enough buffered bits");
            if (num_bits == 0) return;

            if constexpr (Order == BitOrder::LSB) {
                buffer_ >>= num_bits;
            } else {
                buffer_ <<= num_bits;
            }
            buffer_bit_count_ -= num_bits;
        }

        // 丢弃当前字节中未读完的残余填充位，对齐到下一个整字节边界
        void align_to_byte() noexcept {
            const int skip = buffer_bit_count_ % 8;
            if (skip > 0) {
                consume_bits(skip);
            }
        }

        // 查询当前蓄水池中已缓存的 bit 数
        [[nodiscard]] int buffered_bits() const noexcept {
            return buffer_bit_count_;
        }

        static constexpr BitOrder bit_order() noexcept {
            return Order;
        }

    private:
        void refill(const int needed_bits) {
            while (buffer_bit_count_ < needed_bits) {
                const int byte = in_.get(); // 必须用 int 接收以识别 EOF (-1)
                if (BITIO_UNLIKELY(byte == EOF)) {
                    if (in_.bad()) {
                        throw std::ios_base::failure(
                            "bitio::BitReader: failed to read input stream");
                    }
                    if (in_.eof()) {
                        throw std::runtime_error(
                            "bitio::BitReader: unexpected EOF while reading bit stream");
                    }
                    throw std::ios_base::failure(
                        "bitio::BitReader: input stream is not readable");
                }
                if constexpr (Order == BitOrder::LSB) {
                    buffer_ |= (static_cast<uint64_t>(byte) << buffer_bit_count_);
                } else {
                    buffer_ |= (static_cast<uint64_t>(byte) << (56 - buffer_bit_count_));
                }
                buffer_bit_count_ += 8;
            }
        }
    };


    // 导出标准类型别名 (默认采用 LSB-first)

    using BitWriter = BasicBitWriter<BitOrder::LSB>;
    using BitReader = BasicBitReader<BitOrder::LSB>;

    using BitWriterLSB = BasicBitWriter<BitOrder::LSB>;
    using BitReaderLSB = BasicBitReader<BitOrder::LSB>;

    using BitWriterMSB = BasicBitWriter<BitOrder::MSB>;
    using BitReaderMSB = BasicBitReader<BitOrder::MSB>;
} // namespace bitio

#undef BITIO_UNLIKELY

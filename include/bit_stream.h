#pragma once

#include <cassert>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace bitio {
    // LSB/MSB 小/大端序
    // 写入文件时，字节内第一位的方向
    // 对于压缩文件来说，通常使用 LSB
    enum class BitOrder {
        LSB,
        MSB
    };

    namespace detail {
        // 构造低 bit_count 位全为 1 的位掩码
        // C++ 标准规定无符号整数左移位数 >= 类型位宽（即 uint64_t{1} << 64）属于未定义行为（UB），
        // 开启优化后编译器可能会省略移位结果导致掩码错误，必须在移位前特判 >= 64 的边界
        [[nodiscard]] constexpr std::uint64_t low_mask(
            const unsigned bit_count
        ) noexcept {
            if (bit_count == 0) {
                return 0;
            }
            if (bit_count >= 64) {
                return UINT64_MAX;
            }
            return (std::uint64_t{1} << bit_count) - 1;
        }
    }

    // output 由调用者持有；写入器只负责批量写入和刷新
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitWriter {
    public:
        explicit BasicBitWriter(std::ostream &output) noexcept
            : output_(output) {
        }

        BasicBitWriter(const BasicBitWriter &) = delete;

        BasicBitWriter &operator=(const BasicBitWriter &) = delete;

        void write_bit(const bool bit) {
            write_bits(bit ? 1U : 0U, 1);
        }

        void write_byte(const std::uint8_t byte) {
            write_bits(byte, 8);
        }

        // 写入 value 的低 bit_count 位累积器满 32 位时立即追加四个字节
        void write_bits(std::uint64_t value, unsigned bit_count) {
            if (bit_count > 64) {
                throw std::invalid_argument(
                    "bitio::BitWriter: bit count must not exceed 64"
                );
            }
            if (bit_count == 0) {
                return;
            }

            while (bit_count != 0) {
                // 采用 accumulator_ 低 32 位作为位拼接窗口，满 32 位即拆分为 4 个字节写入 64 KiB 输出缓冲；
                // 每次截取 min(bit_count, room)，避免写入超出低 32 位边界
                // （尤其避免 MSB 下 32U - buffered_bits_ - take 无符号下溢）
                const unsigned room = 32U - buffered_bits_;
                const unsigned take = bit_count < room ? bit_count : room;

                if constexpr (Order == BitOrder::LSB) {
                    // LSB（小端位序）：低位比特先进入当前字节的低位（bit 0），新读取的低位切片左移拼接在更高位；
                    // 解码端 BasicBitReader<LSB> 按从低位到高位的顺序移出时即可原样恢复码字
                    accumulator_ |=
                            (value & detail::low_mask(take)) << buffered_bits_;
                    value >>= take;
                } else {
                    // MSB（大端位序）：先取 value 尚未写入的最高 take 位，从 32 位窗口高位（bit 31）向下对齐写入；
                    // 对应解码端 BasicBitReader<MSB> 从高位到低位逐次左移取出
                    const unsigned shift = bit_count - take;
                    const std::uint64_t part =
                            (value >> shift) & detail::low_mask(take);
                    accumulator_ |= part << (32U - buffered_bits_ - take);
                }

                buffered_bits_ += take;
                bit_count -= take;

                if (buffered_bits_ == 32) {
                    flush_word();
                }
            }
        }

        // 结束当前位段；最后一个字节不足的高位（或低位）补零
        void flush() {
            if (buffered_bits_ >= 8) {
                flush_bytes();
            }

            if (buffered_bits_ != 0) {
                // accumulator_ 在构造及每次 flush_word/flush_bytes 移位清空后，剩余未填充比特位均维持为 0，
                // 因此直接写出该末字节即等效于按字节对齐向后补零；这些填充位会在解码阶段由 original_size 自动截断
                if constexpr (Order == BitOrder::LSB) {
                    append_byte(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                } else {
                    append_byte(
                        static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                    );
                }
                accumulator_ = 0;
                buffered_bits_ = 0;
            }

            flush_output_buffer();
            output_.flush();
            check_output("bitio::BitWriter: failed to flush output stream");
        }

        [[nodiscard]] unsigned pending_bits() const noexcept {
            return buffered_bits_;
        }

        [[nodiscard]] std::size_t written_bytes() const noexcept {
            return bytes_written_;
        }

        static constexpr BitOrder bit_order() noexcept {
            return Order;
        }

    private:
        static constexpr std::size_t kOutputBufferSize = 64U * 1024U;

        void append_byte(const std::uint8_t byte) {
            output_buffer_[output_used_++] = byte;
            ++bytes_written_;
            if (output_used_ == output_buffer_.size()) {
                flush_output_buffer();
            }
        }

        void flush_output_buffer() {
            if (output_used_ == 0) {
                return;
            }

            output_.write(
                reinterpret_cast<const char *>(output_buffer_.data()),
                static_cast<std::streamsize>(output_used_)
            );
            check_output("bitio::BitWriter: failed to write output buffer");
            output_used_ = 0;
        }

        void check_output(const char *message) const {
            if (!output_) {
                throw std::ios_base::failure(message);
            }
        }

        void flush_word() {
            if constexpr (Order == BitOrder::LSB) {
                append_byte(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                append_byte(static_cast<std::uint8_t>((accumulator_ >> 8U) & 0xffU));
                append_byte(static_cast<std::uint8_t>((accumulator_ >> 16U) & 0xffU));
                append_byte(static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU));
                accumulator_ >>= 32U;
            } else {
                append_byte(
                    static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                );
                append_byte(
                    static_cast<std::uint8_t>((accumulator_ >> 16U) & 0xffU)
                );
                append_byte(
                    static_cast<std::uint8_t>((accumulator_ >> 8U) & 0xffU)
                );
                append_byte(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                accumulator_ <<= 32U;
            }
            buffered_bits_ = 0;
        }

        void flush_bytes() {
            while (buffered_bits_ >= 8) {
                if constexpr (Order == BitOrder::LSB) {
                    append_byte(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                    accumulator_ >>= 8U;
                } else {
                    append_byte(
                        static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                    );
                    accumulator_ <<= 8U;
                }
                buffered_bits_ -= 8;
            }
        }

        std::ostream &output_;
        std::array<std::uint8_t, kOutputBufferSize> output_buffer_{};
        std::size_t output_used_ = 0;
        std::size_t bytes_written_ = 0;
        std::uint64_t accumulator_ = 0;
        unsigned buffered_bits_ = 0;
    };

    // 读取器借用输入流，不负责打开或关闭流
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitReader {
    public:
        // byte_count 限制本次读取范围，避免读到载荷之后的内容
        BasicBitReader(std::istream &input, const std::size_t byte_count) noexcept
            : source_(&input), source_remaining_(byte_count) {
        }

        [[nodiscard]] bool read_bit() {
            return read_bits(1) != 0;
        }

        [[nodiscard]] std::uint8_t read_byte() {
            return static_cast<std::uint8_t>(read_bits(8));
        }

        // 一次读取超过 56 位时分两段处理，避免累积器移位越界
        [[nodiscard]] std::uint64_t read_bits(const unsigned bit_count) {
            if (bit_count > 64) {
                throw std::invalid_argument(
                    "bitio::BitReader: bit count must not exceed 64"
                );
            }
            if (bit_count == 0) {
                return 0;
            }

            // 累加器不变式说明：ensure() 每次从输入缓冲以“整字节（8 位）”为单位向 uint64_t 补齐数据
            // 由于读取前 accumulator_ 内可能仍残留 1~7 个未读完的余位，
            // 仅当 buffered_bits_ <= 56 时再并入 1 字节才不会超出 64 位上限，
            // 故 ensure() 最多只能安全保证累加器中存在 56 位连续数据；单次请求 57~64 位时须拆成两次读取再按位序合并
            if (bit_count > 56) {
                if constexpr (Order == BitOrder::LSB) {
                    const std::uint64_t low = read_bits(32);
                    const std::uint64_t high = read_bits(bit_count - 32);
                    return low | (high << 32U);
                } else {
                    const unsigned high_count = bit_count - 32;
                    const std::uint64_t high = read_bits(high_count);
                    const std::uint64_t low = read_bits(32);
                    return (high << 32U) | low;
                }
            }

            ensure(bit_count);
            std::uint64_t result = 0;
            if constexpr (Order == BitOrder::LSB) {
                result = accumulator_ & detail::low_mask(bit_count);
                accumulator_ >>= bit_count;
            } else {
                result = accumulator_ >> (64U - bit_count);
                accumulator_ <<= bit_count;
            }
            buffered_bits_ -= bit_count;
            return result;
        }

        // 预览后续位；补充缓存不会推进逻辑读取位置
        [[nodiscard]] std::uint64_t peek_bits(const unsigned bit_count) {
            if (bit_count > 56) {
                throw std::invalid_argument(
                    "bitio::BitReader: peek count must not exceed 56"
                );
            }
            if (bit_count == 0) {
                return 0;
            }
            ensure(bit_count);
            if constexpr (Order == BitOrder::LSB) {
                return accumulator_ & detail::low_mask(bit_count);
            } else {
                return accumulator_ >> (64U - bit_count);
            }
        }

        void consume_bits(const unsigned bit_count) {
            if (bit_count > buffered_bits_) {
                throw std::runtime_error(
                    "bitio::BitReader: consume exceeds buffered bits"
                );
            }
            if constexpr (Order == BitOrder::LSB) {
                accumulator_ >>= bit_count;
            } else if (bit_count != 0) {
                accumulator_ <<= bit_count;
            }
            buffered_bits_ -= bit_count;
        }

        void align_to_byte() {
            if (const unsigned remainder = buffered_bits_ & 7U; remainder != 0) {
                consume_bits(remainder);
            }
        }

        [[nodiscard]] unsigned buffered_bits() const noexcept {
            return buffered_bits_;
        }

        static constexpr BitOrder bit_order() noexcept {
            return Order;
        }

    private:
        static constexpr std::size_t kInputBufferSize = 64U * 1024U;

        // 从输入流分块预读至多 64 KiB 进入内部缓冲区，大幅减少逐字节读流的系统调用与虚函数开销；
        // 单次请求字节数受限于 source_remaining_
        // 读取边界隔离在本次压缩数据段以内
        void refill_source() {
            assert(source_ != nullptr);
            if (source_remaining_ == 0 || source_short_) {
                throw std::runtime_error(
                    "bitio::BitReader: unexpected end of bit stream"
                );
            }

            const std::size_t request =
                    source_remaining_ < kInputBufferSize
                        ? source_remaining_
                        : kInputBufferSize;
            source_->read(
                reinterpret_cast<char *>(source_buffer_.data()),
                static_cast<std::streamsize>(request)
            );
            const std::streamsize received = source_->gcount();

            if (source_->bad() || received <= 0) {
                throw std::ios_base::failure(
                    "bitio::BitReader: failed to read input stream"
                );
            }

            const auto received_size = static_cast<std::size_t>(received);
            source_remaining_ -= received_size;
            source_short_ = received_size < request;
            current_ = source_buffer_.data();
            end_ = current_ + received_size;

            // 短读得到的字节仍可消费；下一次补充时报告输入截断
            if (source_short_ && !source_->bad()) {
                source_->clear();
            }
        }

        // 保证累加器中至少存有 required 个待读比特（required <= 56）
        // 每次从输入缓冲压入 1 字节前必满足 buffered_bits_ <= 56，从而保证累加器左/右移不会溢出 64 位宽度
        void ensure(const unsigned required) {
            while (buffered_bits_ < required) {
                if (current_ == end_) {
                    refill_source();
                }

                assert(buffered_bits_ <= 56);

                const std::uint8_t byte = *current_++;
                if constexpr (Order == BitOrder::LSB) {
                    accumulator_ |=
                            static_cast<std::uint64_t>(byte) << buffered_bits_;
                } else {
                    accumulator_ |=
                            static_cast<std::uint64_t>(byte)
                            << (56U - buffered_bits_);
                }
                buffered_bits_ += 8;
            }
        }

        const std::uint8_t *current_ = nullptr;
        const std::uint8_t *end_ = nullptr;
        std::istream *source_ = nullptr;
        std::size_t source_remaining_ = 0;
        bool source_short_ = false;
        std::array<std::uint8_t, kInputBufferSize> source_buffer_{};
        std::uint64_t accumulator_ = 0;
        unsigned buffered_bits_ = 0;
    };


    using BitWriter = BasicBitWriter<>;
    using BitReader = BasicBitReader<>;
    using BitWriterLSB = BasicBitWriter<BitOrder::LSB>;
    using BitReaderLSB = BasicBitReader<BitOrder::LSB>;
    using BitWriterMSB = BasicBitWriter<BitOrder::MSB>;
    using BitReaderMSB = BasicBitReader<BitOrder::MSB>;
} // namespace bitio

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

    // output 由调用者持有；写入器只负责批量写入和刷新。
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

        // 写入 value 的低 bit_count 位。累积器满 32 位时立即追加四个字节。
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
                const unsigned room = 32U - buffered_bits_;
                const unsigned take = bit_count < room ? bit_count : room;

                if constexpr (Order == BitOrder::LSB) {
                    accumulator_ |=
                            (value & detail::low_mask(take)) << buffered_bits_;
                    value >>= take;
                } else {
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

        // 结束当前位段；最后一个字节不足的高位（或低位）补零。
        void flush() {
            if (buffered_bits_ >= 8) {
                flush_bytes();
            }

            if (buffered_bits_ != 0) {
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

    // 读取器借用输入流，不负责打开或关闭流。
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitReader {
    public:
        // byte_count 限制本次读取范围，避免读到载荷之后的内容。
        BasicBitReader(std::istream &input, const std::size_t byte_count) noexcept
            : source_(&input), source_remaining_(byte_count) {
        }

        [[nodiscard]] bool read_bit() {
            return read_bits(1) != 0;
        }

        [[nodiscard]] std::uint8_t read_byte() {
            return static_cast<std::uint8_t>(read_bits(8));
        }

        // 一次读取超过 56 位时分两段处理，避免累积器移位越界。
        [[nodiscard]] std::uint64_t read_bits(const unsigned bit_count) {
            if (bit_count > 64) {
                throw std::invalid_argument(
                    "bitio::BitReader: bit count must not exceed 64"
                );
            }
            if (bit_count == 0) {
                return 0;
            }

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

        // 预览后续位；补充缓存不会推进逻辑读取位置。
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

            // 短读得到的字节仍可消费；下一次补充时报告输入截断。
            if (source_short_ && !source_->bad()) {
                source_->clear();
            }
        }

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

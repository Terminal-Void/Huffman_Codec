#pragma once

#include <cassert>
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <stdexcept>
#include <vector>

namespace bitio {
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

    // 位写入器借用 output，不拥有也不释放 output 的内存。
    // 累积器最多保留 31 位，满 32 位后批量排出四个字节。
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitWriter {
    public:
        explicit BasicBitWriter(std::vector<std::uint8_t> &output) noexcept
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

        // 写入 value 的低 bit_count 位；MSB 模式下这些位按高位到低位输出。
        void write_bits(std::uint64_t value, unsigned bit_count) {
            assert(bit_count <= 64);
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

        // 补零到字节边界并写入最后一个字节。重复调用不会重复追加数据。
        void flush() {
            if (buffered_bits_ >= 8) {
                flush_bytes();
            }

            if (buffered_bits_ != 0) {
                if constexpr (Order == BitOrder::LSB) {
                    output_.push_back(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                } else {
                    output_.push_back(
                        static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                    );
                }
                accumulator_ = 0;
                buffered_bits_ = 0;
            }
        }

        [[nodiscard]] unsigned pending_bits() const noexcept {
            return buffered_bits_;
        }

        static constexpr BitOrder bit_order() noexcept {
            return Order;
        }

    private:
        void flush_word() {
            if constexpr (Order == BitOrder::LSB) {
                output_.push_back(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                output_.push_back(static_cast<std::uint8_t>((accumulator_ >> 8U) & 0xffU));
                output_.push_back(static_cast<std::uint8_t>((accumulator_ >> 16U) & 0xffU));
                output_.push_back(static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU));
                accumulator_ >>= 32U;
            } else {
                output_.push_back(
                    static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                );
                output_.push_back(
                    static_cast<std::uint8_t>((accumulator_ >> 16U) & 0xffU)
                );
                output_.push_back(
                    static_cast<std::uint8_t>((accumulator_ >> 8U) & 0xffU)
                );
                output_.push_back(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                accumulator_ <<= 32U;
            }
            buffered_bits_ = 0;
        }

        void flush_bytes() {
            while (buffered_bits_ >= 8) {
                if constexpr (Order == BitOrder::LSB) {
                    output_.push_back(static_cast<std::uint8_t>(accumulator_ & 0xffU));
                    accumulator_ >>= 8U;
                } else {
                    output_.push_back(
                        static_cast<std::uint8_t>((accumulator_ >> 24U) & 0xffU)
                    );
                    accumulator_ <<= 8U;
                }
                buffered_bits_ -= 8;
            }
        }

        std::vector<std::uint8_t> &output_;
        std::uint64_t accumulator_ = 0;
        unsigned buffered_bits_ = 0;
    };

    // 位读取器借用连续内存，调用者必须保证 input 在读取期间有效。
    template<BitOrder Order = BitOrder::LSB>
    class BasicBitReader {
    public:
        BasicBitReader(const std::uint8_t *input, const std::size_t size)
            : current_(&empty_byte_), end_(&empty_byte_) {
            if (input == nullptr) {
                if (size != 0) {
                    throw std::invalid_argument(
                        "bitio::BitReader: null input with non-zero size"
                    );
                }
                return;
            }

            current_ = input;
            end_ = input + size;
        }

        explicit BasicBitReader(const std::vector<std::uint8_t> &input)
            : BasicBitReader(input.data(), input.size()) {
        }

        // 流式读取器只保留固定大小的输入缓冲，不会把整个文件载入内存。
        BasicBitReader(std::istream &input, const std::size_t byte_count) noexcept
            : source_(&input), source_remaining_(byte_count) {
        }

        [[nodiscard]] bool read_bit() {
            return read_bits(1) != 0;
        }

        [[nodiscard]] std::uint8_t read_byte() {
            return static_cast<std::uint8_t>(read_bits(8));
        }

        [[nodiscard]] std::uint64_t read_bits(unsigned bit_count) {
            assert(bit_count <= 64);
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

        // 预览后续位，不改变逻辑读取位置；必要时只补充内部缓存。
        [[nodiscard]] std::uint64_t peek_bits(const unsigned bit_count) {
            assert(bit_count <= 56);
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
            const unsigned remainder = buffered_bits_ & 7U;
            if (remainder != 0) {
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
        inline static constexpr std::uint8_t empty_byte_ = 0;
        static constexpr std::size_t kInputBufferSize = 64U * 1024U;

        void refill_source() {
            if (source_ == nullptr) {
                throw std::runtime_error(
                    "bitio::BitReader: input buffer is exhausted"
                );
            }
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

            // std::istream::read 在短读时可能设置 eofbit/failbit；已经取得的
            // 字节仍然可以消费，下一次 refill_source() 会报告截断。
            if (source_short_ && !source_->bad()) {
                source_->clear();
            }
        }

        void ensure(const unsigned required) {
            while (buffered_bits_ < required) {
                if (current_ == end_) {
                    if (source_ != nullptr) {
                        refill_source();
                    } else {
                        throw std::runtime_error(
                            "bitio::BitReader: unexpected end of bit stream"
                        );
                    }
                }

                // 读取 56 位以内时，最多再装入一个字节，累积器不会溢出。
                if (buffered_bits_ > 56) {
                    throw std::logic_error(
                        "bitio::BitReader: accumulator invariant violated"
                    );
                }

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

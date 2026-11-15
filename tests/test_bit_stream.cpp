#include "catch_amalgamated.hpp"

#include "bit_stream.h"

#include <cstdint>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    // 按指定位序连续写入多种位宽，再逐项读出核对
    template<bitio::BitOrder Order>
    void verify_mixed_bit_widths() {
        struct Chunk {
            std::uint64_t value;
            unsigned width;
        };

        const std::vector<Chunk> chunks = {
            {0ULL, 0U},
            {1ULL, 1U},
            {0b101ULL, 3U},
            {0x5AULL, 8U},
            {0x12345ULL, 17U},
            {0xABCDEF01ULL, 32U},
            {0xFEDCBA9876543210ULL, 64U},
            {0b11ULL, 2U}
        };

        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        bitio::BasicBitWriter<Order> writer(stream);

        std::size_t total_bits = 0;
        for (const Chunk &c : chunks) {
            writer.write_bits(c.value, c.width);
            total_bits += c.width;
        }
        writer.flush();
        REQUIRE(writer.pending_bits() == 0);
        REQUIRE(writer.written_bytes() == (total_bits + 7) / 8);

        stream.seekg(0, std::ios::beg);
        bitio::BasicBitReader<Order> reader(stream, writer.written_bytes());
        for (const Chunk &c : chunks) {
            REQUIRE(reader.read_bits(c.width) == c.value);
        }
    }
} // namespace

TEST_CASE("BitWriter 与 BitReader 基本位读写", "[bitio]") {
    SECTION("LSB 混合位宽读写") {
        verify_mixed_bit_widths<bitio::BitOrder::LSB>();
    }

    SECTION("MSB 混合位宽读写") {
        verify_mixed_bit_widths<bitio::BitOrder::MSB>();
    }

    SECTION("跨越 64KB 缓冲区的读写与末尾补位") {
        // 写入超过 64KB 缓冲区的数据并追加 3 个未对齐比特
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        bitio::BitWriter writer(stream);

        constexpr std::size_t byte_count = 70000;
        for (std::size_t i = 0; i < byte_count; ++i) {
            writer.write_byte(static_cast<std::uint8_t>((i * 131U + 17U) & 0xffU));
        }
        writer.write_bit(true);
        writer.write_bit(false);
        writer.write_bit(true);
        REQUIRE(writer.pending_bits() == 3);

        writer.flush();
        REQUIRE(writer.written_bytes() == byte_count + 1);

        stream.seekg(0, std::ios::beg);
        bitio::BitReader reader(stream, writer.written_bytes());
        for (std::size_t i = 0; i < byte_count; ++i) {
            REQUIRE(reader.read_byte() == static_cast<std::uint8_t>((i * 131U + 17U) & 0xffU));
        }
        REQUIRE(reader.read_bit() == true);
        REQUIRE(reader.read_bit() == false);
        REQUIRE(reader.read_bit() == true);
    }
}

TEST_CASE("BitReader 辅助操作与越界检查", "[bitio]") {
    SECTION("peek_bits、consume_bits 与 align_to_byte") {
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        bitio::BitWriter writer(stream);
        writer.write_bits(0b11010101ULL, 8);
        writer.write_bits(0xBEEFULL, 16);
        writer.flush();

        stream.seekg(0, std::ios::beg);
        bitio::BitReader reader(stream, writer.written_bytes());

        // peek_bits 不消耗缓冲比特
        REQUIRE(reader.peek_bits(5) == 0b10101ULL);
        REQUIRE(reader.peek_bits(5) == 0b10101ULL);
        reader.consume_bits(5);
        REQUIRE(reader.buffered_bits() == 3);

        // 跳过当前字节剩余比特并对齐读取后续数据
        reader.align_to_byte();
        REQUIRE(reader.buffered_bits() == 0);
        REQUIRE(reader.read_bits(16) == 0xBEEFULL);
    }

    SECTION("位宽越界与流提前结束异常") {
        std::stringstream out_stream(std::ios::in | std::ios::out | std::ios::binary);
        bitio::BitWriter writer(out_stream);
        REQUIRE_THROWS_AS(writer.write_bits(0, 65), std::invalid_argument);

        writer.write_byte(0xAAU);
        writer.flush();

        out_stream.seekg(0, std::ios::beg);
        bitio::BitReader reader(out_stream, 1);
        REQUIRE_THROWS_AS((void)reader.read_bits(65), std::invalid_argument);
        REQUIRE_THROWS_AS((void)reader.peek_bits(57), std::invalid_argument);

        // 超出声明字节数后继续读取应抛出异常
        REQUIRE(reader.read_byte() == 0xAAU);
        REQUIRE_THROWS_AS((void)reader.read_bit(), std::runtime_error);

        // 声明读取长度大于实际流长度
        std::stringstream short_stream(std::string(1, '\x55'), std::ios::in | std::ios::binary);
        bitio::BitReader short_reader(short_stream, 4);
        REQUIRE(short_reader.read_byte() == 0x55U);
        REQUIRE_THROWS_AS((void)short_reader.read_byte(), std::runtime_error);
    }
}

#pragma once

#include "huffman_types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace testutil {
    namespace fs = std::filesystem;

    // 独立测试临时目录，析构时自动删除
    class TempDir {
    public:
        TempDir() {
            static const std::uint32_t prefix = std::random_device{}();
            static std::atomic<std::uint64_t> counter{0};
            const auto seq = counter.fetch_add(1, std::memory_order_relaxed);
            const std::string name = "project2_test_" + std::to_string(prefix) +
                                     "_" + std::to_string(seq);
            path_ = fs::temp_directory_path() / name;
            fs::create_directories(path_);
        }

        ~TempDir() noexcept {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }

        TempDir(const TempDir &) = delete;
        TempDir &operator=(const TempDir &) = delete;

        [[nodiscard]] const fs::path &path() const noexcept {
            return path_;
        }

        [[nodiscard]] fs::path child(const std::string &filename) const {
            return path_ / filename;
        }

    private:
        fs::path path_;
    };

    // 统一按二进制模式读写测试文件，避免换行符转换
    inline void write_bytes(const fs::path &path, const std::vector<Byte> &data) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open output test file");
        }
        if (!data.empty()) {
            out.write(
                reinterpret_cast<const char *>(data.data()),
                static_cast<std::streamsize>(data.size())
            );
        }
        if (!out) {
            throw std::runtime_error("failed to write test bytes");
        }
    }

    inline void write_text(const fs::path &path, const std::string_view text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open output text file");
        }
        if (!text.empty()) {
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        if (!out) {
            throw std::runtime_error("failed to write test text");
        }
    }

    inline std::vector<Byte> read_bytes(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("failed to open input test file");
        }
        return {
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };
    }

    inline std::string read_text(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("failed to open input text file");
        }
        return {
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };
    }

    // 将字符串按无符号字节转换为 Byte 序列
    inline std::vector<Byte> to_bytes(const std::string_view text) {
        std::vector<Byte> out;
        out.reserve(text.size());
        for (const char ch : text) {
            out.push_back(static_cast<Byte>(static_cast<unsigned char>(ch)));
        }
        return out;
    }
} // namespace testutil

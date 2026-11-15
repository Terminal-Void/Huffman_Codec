#include "catch_amalgamated.hpp"

#include "huffman_format.h"
#include "test_helpers.h"

#include <cstdio>
#include <ios>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
    // FILE* 自动关闭封装
    struct CFileCloser {
        void operator()(FILE *fp) const noexcept {
            if (fp != nullptr) {
                std::fclose(fp);
            }
        }
    };
    using CFilePtr = std::unique_ptr<FILE, CFileCloser>;

    // 创建临时文件并以二进制模式打开
    CFilePtr open_temp_file_with_bytes(const fs::path &path, const std::string &content) {
        testutil::write_text(path, content);
        CFilePtr fp(std::fopen(path.string().c_str(), "rb"));
        REQUIRE(fp != nullptr);
        return fp;
    }
} // namespace

TEST_CASE("ArchivePaths 与 ArchiveHeader 格式读写", "[format]") {
    SECTION("路径派生与非法扩展名") {
        const ArchivePaths p = ArchivePaths::from_input("dir/sample.txt");
        REQUIRE(p.archive == fs::path("dir/sample.txt.huf"));
        REQUIRE(p.shape == fs::path("dir/sample.txt.config1.txt"));
        REQUIRE(p.leaves == fs::path("dir/sample.txt.config2.txt"));
        REQUIRE(p.decoded == fs::path("dir/sample.decoded.txt"));

        REQUIRE(ArchivePaths::from_archive("README.huf").decoded == fs::path("README.decoded"));
        REQUIRE(
            ArchivePaths::from_archive("archive.tar.gz.huf").decoded ==
            fs::path("archive.tar.decoded.gz")
        );

        REQUIRE_THROWS_AS(ArchivePaths::from_archive("data.zip"), std::runtime_error);
        REQUIRE_THROWS_AS(ArchivePaths::from_archive(".huf"), std::runtime_error);
    }

    SECTION("文件头读写与错误魔数/截断检测") {
        const ArchiveHeader expected{
            0x0123456789ABCDEFULL,
            std::numeric_limits<std::uint64_t>::max()
        };

        std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        expected.write(ss);

        const std::string raw = ss.str();
        REQUIRE(raw.size() == 20);
        REQUIRE(raw.substr(0, 4) == "HUF1");

        ss.seekg(0, std::ios::beg);
        const ArchiveHeader actual = ArchiveHeader::read(ss);
        REQUIRE(actual.original_size == expected.original_size);
        REQUIRE(actual.payload_bits == expected.payload_bits);

        // 错误魔数或截断文件头应报错
        std::stringstream bad_magic("BAD1" + raw.substr(4), std::ios::in | std::ios::binary);
        REQUIRE_THROWS_AS(ArchiveHeader::read(bad_magic), std::runtime_error);

        std::stringstream truncated(raw.substr(0, 12), std::ios::in | std::ios::binary);
        REQUIRE_THROWS_AS(ArchiveHeader::read(truncated), std::runtime_error);
    }
}

TEST_CASE("GenHuffTreeByConfigFile 重建哈夫曼树", "[format]") {
    testutil::TempDir tmp;

    SECTION("常规文本与单叶节点/特殊字符重建") {
        const auto fp1 = open_temp_file_with_bytes(tmp.child("c1.txt"), "1010100");
        const auto fp2 = open_temp_file_with_bytes(tmp.child("c2.txt"), "ABCD");
        const TreeHandle tree = GenHuffTreeByConfigFile(fp1.get(), fp2.get());
        REQUIRE(tree != nullptr);

        std::ostringstream shape_out(std::ios::binary);
        std::ostringstream leaves_out(std::ios::binary);
        write_tree_config(*tree, shape_out, leaves_out);
        REQUIRE(shape_out.str() == "1010100");
        REQUIRE(leaves_out.str() == "ABCD");

        // 验证含换行、NUL、0x1A 等特殊字节时的二进制读写
        const std::string bin_leaves("\n\r\0\x1A\xFF", 5);
        const auto b1 = open_temp_file_with_bytes(tmp.child("bin1.txt"), "110011000");
        const auto b2 = open_temp_file_with_bytes(tmp.child("bin2.txt"), bin_leaves);
        const TreeHandle bin_tree = GenHuffTreeByConfigFile(b1.get(), b2.get());

        std::ostringstream bin_shape_out(std::ios::binary);
        std::ostringstream bin_leaves_out(std::ios::binary);
        write_tree_config(*bin_tree, bin_shape_out, bin_leaves_out);
        REQUIRE(bin_shape_out.str() == "110011000");
        REQUIRE(bin_leaves_out.str() == bin_leaves);
    }

    SECTION("256 种字节的最深左斜树") {
        // 255 个内部节点与 256 个叶节点
        std::string shape(255, '1');
        shape.append(256, '0');
        std::string leaves(256, '\0');
        for (int i = 0; i < 256; ++i) {
            leaves[static_cast<std::size_t>(i)] = static_cast<char>(i);
        }

        const auto fp1 = open_temp_file_with_bytes(tmp.child("max1.txt"), shape);
        const auto fp2 = open_temp_file_with_bytes(tmp.child("max2.txt"), leaves);
        const TreeHandle tree = GenHuffTreeByConfigFile(fp1.get(), fp2.get());

        std::ostringstream shape_out(std::ios::binary);
        std::ostringstream leaves_out(std::ios::binary);
        write_tree_config(*tree, shape_out, leaves_out);
        REQUIRE(shape_out.str() == shape);
        REQUIRE(leaves_out.str() == leaves);
    }
}

TEST_CASE("配置文件异常输入检查", "[format]") {
    testutil::TempDir tmp;

    SECTION("空指针与文件不存在") {
        const fs::path ok = tmp.child("ok.txt");
        const auto fp = open_temp_file_with_bytes(ok, "0");
        REQUIRE_THROWS_AS(GenHuffTreeByConfigFile(nullptr, fp.get()), std::invalid_argument);
        REQUIRE_THROWS_AS(GenHuffTreeByConfigFile(fp.get(), nullptr), std::invalid_argument);
        REQUIRE_THROWS_AS(load_tree_config(tmp.child("missing.txt"), ok), std::runtime_error);
    }

    SECTION("格式错误的 config1 与 config2") {
        struct BadCase {
            std::string name;
            std::string shape;
            std::string leaves;
        };

        // 检查空结构、截断、非法字符及超长连续 '1'（防止递归栈溢出）
        const std::vector<BadCase> cases = {
            {"空结构文件", "", "A"},
            {"config1 提前结束", "10", "AB"},
            {"config2 字节不足", "100", "A"},
            {"config1 含非法字符", "102", "AB"},
            {"config2 多余字节", "100", "ABC"},
            {"超长连续 '1'", std::string(200000, '1'), "A"},
        };

        for (const auto &bc : cases) {
            INFO("异常用例: " << bc.name);
            const fs::path p1 = tmp.child("shape.txt");
            const fs::path p2 = tmp.child("leaves.txt");
            testutil::write_text(p1, bc.shape);
            testutil::write_text(p2, bc.leaves);
            REQUIRE_THROWS_AS(load_tree_config(p1, p2), std::runtime_error);
        }
    }
}

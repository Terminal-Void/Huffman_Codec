#include "catch_amalgamated.hpp"

#include "huffman_codec.h"
#include "huffman_format.h"
#include "test_helpers.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {
    // 执行压缩与解压，核对配置副文件及还原内容
    void verify_roundtrip_bytes(
        const testutil::TempDir &tmp,
        const std::string &filename,
        const std::vector<Byte> &data
    ) {
        const fs::path input_path = tmp.child(filename);
        testutil::write_bytes(input_path, data);

        const ArchivePaths comp = compress_file(input_path);
        REQUIRE(fs::exists(comp.archive));
        REQUIRE(fs::exists(comp.shape));
        REQUIRE(fs::exists(comp.leaves));

        const std::string shape = testutil::read_text(comp.shape);
        const std::vector<Byte> leaves = testutil::read_bytes(comp.leaves);

        // 校验 config1 字符合法性与正则二叉树节点数量关系
        std::size_t zeros = 0;
        std::size_t ones = 0;
        for (const char ch : shape) {
            REQUIRE((ch == '0' || ch == '1'));
            if (ch == '0') {
                ++zeros;
            } else {
                ++ones;
            }
        }
        REQUIRE(zeros == leaves.size());
        REQUIRE(ones + 1 == zeros);

        // 校验 config2 叶节点去重后与原文件字节集合一致
        const std::set<Byte> input_unique(data.begin(), data.end());
        const std::set<Byte> leaf_unique(leaves.begin(), leaves.end());
        REQUIRE(leaf_unique.size() == leaves.size());
        REQUIRE(leaf_unique == input_unique);

        const ArchivePaths dec = decompress_file(comp.archive);
        REQUIRE(fs::exists(dec.decoded));
        REQUIRE(testutil::read_bytes(dec.decoded) == data);
    }

    // 调用命令行子进程并返回退出码
    int run_shell_command(const std::string &cmd) {
#ifdef _WIN32
        const std::string wrapped = "\"" + cmd + "\"";
        return std::system(wrapped.c_str());
#else
        const int rc = std::system(cmd.c_str());
        if (WIFEXITED(rc)) {
            return WEXITSTATUS(rc);
        }
        return -1;
#endif
    }
} // namespace

TEST_CASE("常见文本与退化频率分布压缩解压", "[codec]") {
    testutil::TempDir tmp;

    SECTION("一般英文样例") {
        verify_roundtrip_bytes(tmp, "slide.txt", testutil::to_bytes("AAABBCD"));

        const std::string mixed = std::string(400, 'E') +
                                  std::string(120, 'U') +
                                  std::string(60, 'D') +
                                  std::string(30, 'L') +
                                  std::string(15, 'C') +
                                  std::string(8, 'Z') +
                                  std::string(4, 'K') +
                                  std::string(2, 'M');
        verify_roundtrip_bytes(tmp, "letters.txt", testutil::to_bytes(mixed));
        REQUIRE(
            fs::file_size(tmp.child("letters.txt.huf")) <
            fs::file_size(tmp.child("letters.txt"))
        );
    }

    SECTION("单字符与等频字符分布") {
        // 退化为单叶节点的哈夫曼树
        verify_roundtrip_bytes(tmp, "one.txt", {'A'});
        verify_roundtrip_bytes(tmp, "repeat.txt", std::vector<Byte>(1000, Byte{'Z'}));

        REQUIRE(testutil::read_text(tmp.child("repeat.txt.config1.txt")) == "0");
        REQUIRE(testutil::read_text(tmp.child("repeat.txt.config2.txt")) == "Z");
        std::ifstream huf(tmp.child("repeat.txt.huf"), std::ios::binary);
        const ArchiveHeader hdr = ArchiveHeader::read(huf);
        REQUIRE(hdr.original_size == 1000);
        REQUIRE(hdr.payload_bits == 1000);

        verify_roundtrip_bytes(tmp, "equal.txt", testutil::to_bytes("AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH"));
    }
}

TEST_CASE("二进制字节、缓冲区边界与中文路径", "[codec]") {
    testutil::TempDir tmp;

    SECTION("全 0~255 字节与特殊控制字符") {
        std::vector<Byte> data;
        for (int b = 0; b < 256; ++b) {
            data.push_back(static_cast<Byte>(b));
        }
        const std::vector<Byte> special = {'\r', '\n', 0x1A, 0x00, 0xFF};
        data.insert(data.end(), special.begin(), special.end());
        verify_roundtrip_bytes(tmp, "all_bytes.bin", data);
    }

    SECTION("跨 64KB 缓冲区长度与随机数据") {
        // 测试超过 64KB (65536 字节) 缓冲区时的连续读写
        std::vector<Byte> boundary(65537);
        for (std::size_t i = 0; i < boundary.size(); ++i) {
            boundary[i] = static_cast<Byte>((i % 13 == 0) ? '\n' : ('A' + (i % 7)));
        }
        verify_roundtrip_bytes(tmp, "b65537.bin", boundary);

        // 固定随机种子生成测试数据
        std::mt19937 rng(42U);
        std::vector<Byte> random_data(50000);
        for (Byte &b : random_data) {
            b = static_cast<Byte>(rng() & 0xFFU);
        }
        verify_roundtrip_bytes(tmp, "random.bin", random_data);
    }

    SECTION("UTF-8 中文内容与带空格中文文件名") {
        const std::string chinese =
            "数据结构实验二：Huffman 编码压缩与解压缩测试文本\n"
            "数据结构实验二：Huffman 编码压缩与解压缩测试文本\n";
        verify_roundtrip_bytes(tmp, "测试 文本.txt", testutil::to_bytes(chinese));
    }
}

TEST_CASE("非法输入与损坏压缩包异常检查", "[codec]") {
    testutil::TempDir tmp;

    SECTION("空文件、不存在路径与目录路径") {
        const fs::path empty = tmp.child("empty.txt");
        testutil::write_bytes(empty, {});
        REQUIRE_THROWS_AS(compress_file(empty), std::runtime_error);
        REQUIRE_THROWS_AS(compress_file(tmp.child("does_not_exist.txt")), std::runtime_error);
        REQUIRE_THROWS_AS(compress_file(tmp.path()), std::runtime_error);
        REQUIRE_THROWS_AS(decompress_file(tmp.child("missing.huf")), std::runtime_error);
    }

    SECTION("文件头或编码数据损坏") {
        const fs::path sample = tmp.child("sample.txt");
        testutil::write_text(sample, "AAABBCD");
        const ArchivePaths paths = compress_file(sample);
        const std::vector<Byte> valid_huf = testutil::read_bytes(paths.archive);

        // 篡改头部 original_size 为 0
        {
            std::vector<Byte> bad = valid_huf;
            for (std::size_t i = 4; i < 12; ++i) {
                bad[i] = 0;
            }
            testutil::write_bytes(paths.archive, bad);
            REQUIRE_THROWS_AS(decompress_file(paths.archive), std::runtime_error);
        }

        // 篡改 payload_bits 导致提前结束或残留多余编码位
        {
            std::vector<Byte> bad = valid_huf;
            bad[12] = static_cast<Byte>(bad[12] + 1);
            testutil::write_bytes(paths.archive, bad);
            REQUIRE_THROWS_AS(decompress_file(paths.archive), std::runtime_error);
        }

        // 截断载荷字节
        {
            std::vector<Byte> bad(valid_huf.begin(), valid_huf.begin() + 20);
            testutil::write_bytes(paths.archive, bad);
            REQUIRE_THROWS_AS(decompress_file(paths.archive), std::runtime_error);
        }

        // 单字符文件若编码比特为 1 应报错
        const fs::path single = tmp.child("single.txt");
        testutil::write_text(single, "AAAA");
        const ArchivePaths sp = compress_file(single);
        std::vector<Byte> bad_single = testutil::read_bytes(sp.archive);
        bad_single[20] = 0x01;
        testutil::write_bytes(sp.archive, bad_single);
        REQUIRE_THROWS_AS(decompress_file(sp.archive), std::runtime_error);
    }
}

#ifdef PROJECT2_CLI_PATH
TEST_CASE("命令行程序交互测试", "[cli]") {
    const fs::path cli_path(PROJECT2_CLI_PATH);
    REQUIRE(fs::exists(cli_path));

    testutil::TempDir tmp;
    const fs::path src = tmp.child("cli input.txt");
    const std::string content = "EEEEEUDDLLLCCZZKKKKKKKMMMMMMMM";
    testutil::write_text(src, content);

    const fs::path huf = tmp.child("cli input.txt.huf");
    const fs::path dec = tmp.child("cli input.decoded.txt");

    const fs::path stdin_file = tmp.child("stdin.txt");
    const fs::path stdout_file = tmp.child("stdout.txt");
    const fs::path stderr_file = tmp.child("stderr.txt");

    // 模拟命令行输入与引号路径
    const std::string commands =
        "h\n"
        "unknown_cmd\n"
        "\n"
        "  c  \n"
        "  '" + src.string() + "'  \n"
        "x\n"
        "  \"" + huf.string() + "\"  \n"
        "q\n";
    testutil::write_text(stdin_file, commands);

    const std::string cmd = "\"" + cli_path.string() + "\" < \"" +
                            stdin_file.string() + "\" > \"" +
                            stdout_file.string() + "\" 2> \"" +
                            stderr_file.string() + "\"";
    REQUIRE(run_shell_command(cmd) == 0);
    REQUIRE(testutil::read_text(stderr_file).empty());
    REQUIRE(testutil::read_text(stdout_file).find("Invalid Command.") != std::string::npos);
    REQUIRE(testutil::read_text(dec) == content);

    // 命令后直接遇到 EOF 应正常退出
    testutil::write_text(stdin_file, "c");
    REQUIRE(run_shell_command(cmd) == 0);
    REQUIRE(testutil::read_text(stderr_file).empty());
}
#endif

#include <assert.h>

#include <bitset>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <sys/time.h>
#include <vector>

#include "code_assigner_factory.hpp"
#include "double_char_encoder.hpp"
#include "symbol_selector_factory.hpp"

namespace hope {

namespace doublecharencodertest {

static const char kWikiFilePath[] = "../../workload/ads.txt";
static const int kWikiTestSize = 34037518;
static std::vector<std::string> wikis;
static const int kLongestCodeLen = 4096;

int GetByteLen(const int bitlen) { return ((bitlen + 7) & ~7) / 8; }

void Print(const std::string& str) {
    for (auto c : str) {
        std::cout << std::bitset<8>(c) << " ";
    }
    std::cout << std::endl;
}

std::string Uint64ToString(uint64_t key) {
    uint64_t endian_swapped_key = __builtin_bswap64(key);
    return std::string(reinterpret_cast<const char*>(&endian_swapped_key), 8);
}

void TEST_MAIN() {
    DoubleCharEncoder* encoder = new DoubleCharEncoder();
    encoder->build(wikis, 65536);
    auto buffer = new uint8_t[kLongestCodeLen];
    // encode single key each time

    struct timeval tv1, tv2;
    uint64_t checkSum = 0;
    gettimeofday(&tv1, NULL);
    for (int i = 0; i < static_cast<int>(wikis.size()) - 1; i++) {
        int len = encoder->encode(wikis[i], buffer);
        std::string str1 = std::string((const char*)buffer, GetByteLen(len));
        checkSum += str1.length();
    }
    gettimeofday(&tv2, NULL);
    double us =
        1000000. * (tv2.tv_sec - tv1.tv_sec) + (tv2.tv_usec - tv1.tv_usec);
    double avg_ns = 1000. * us / (static_cast<int>(wikis.size()) - 1);

    std::cout << "Average Nano Second Per String is " << avg_ns << " ns"
              << std::endl;
}

void LoadWikis() {
    std::ifstream infile(kWikiFilePath);
    std::string key;
    int count = 0;
    while (infile.good() && count < kWikiTestSize) {
        infile >> key;
        wikis.push_back(key);
        count++;
    }
}
} // namespace doublecharencodertest

} // namespace hope

int main(int argc, char** argv) {
    hope::doublecharencodertest::LoadWikis();
    hope::doublecharencodertest::TEST_MAIN();
    return 0;
}

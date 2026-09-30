#include <iostream>
#include <sys/time.h>

#include "include/rss_impl.hpp"

std::vector<kv_pair> Str2Vec(const std::string* datas, const int len)
{
    std::vector<kv_pair> kvs;
    for (int i = 0; i < len; ++i) {
        kvs.push_back({ datas[i], 1 });
    }
    return kvs;
}

std::vector<kv_pair> Str2Vec(const std::vector<std::string>& keys)
{
    std::vector<kv_pair> kvs;
    for (int i = 0; i < keys.size(); ++i) {
        kvs.push_back({ keys[i], 1 });
    }
    return kvs;
}

void RSS_TEST(const char* fname)
{
    hope::DoubleCharEncoder* coder = new hope::DoubleCharEncoder();

    std::cout << "Load data from " << fname << std::endl;
    auto raw_keys = LoadAllData(fname);
    auto cod_keys = codeKeys(raw_keys, *coder);
    std::cout << "Keys size: " << raw_keys.size() << std::endl;
    auto kvs = Str2Vec(cod_keys);
    RSS index(kvs);

    const uint64_t test_size = 2e7;

    std::vector<std::string> test_data(test_size);
    for (int i = 0; i < test_size; ++i) {
        test_data[i] = raw_keys[rand() % raw_keys.size()];
    }

    raw_keys.clear();

    struct timeval tv1, tv2;
    int founds = 0;
    size_t dd = 0;

    auto buffer = new uint8_t[4096];
    gettimeofday(&tv1, NULL);
    for (int i = 0; i < test_size; ++i) {
        int len = coder->encode(test_data[i], buffer);
        std::string str1 = std::string((const char*)buffer, GetByteLen(len));
        founds += index.lookup(str1, dd);
        // founds += index.lookup(test_data[i]);
    }
    gettimeofday(&tv2, NULL);

    double us = 1000000. * (tv2.tv_sec - tv1.tv_sec) + (tv2.tv_usec - tv1.tv_usec);
    double ns = 1000. * us / test_size;

    std::cout << "Index Size: " << index.GetSize() << std::endl;
    std::cout << "Throughput: " << 1000. / ns << "Mops" << std::endl;
    std::cout << "Average Depth: " << (double)dd / test_size << std::endl;
    std::cout << "Average Search Time: " << ns << std::endl;
    std::cout << "Founds: " << founds << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Usage: "<< argv[0]<< " <data_file>" << std::endl;
        return 0;
    }
    srand(time(NULL));
    RSS_TEST(argv[1]);
}

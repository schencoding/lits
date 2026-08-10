#include "genId.hpp"

#include "lits/lits.hpp"

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <sys/time.h>
#include <vector>

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"

typedef enum {
    idcards = 0,
    randstr = 1,
} strType;

// Randomly generated keys (idcard)
std::vector<std::string> keys;
const int default_key_cnt = 2e6;
const int default_search_cnt = 1e6;
const int default_scan_cnt = 1e5;
const int default_scan_range = 100;

// std::string will store strings with a length less than 16 locally. To ensure
// unified memory access for the data, we store all the data in a buffer.

// For bulk load
char *bulk_data;
char **bulk_keys;
uint64_t *bulk_vals;
int num_of_bulk;

// For search
char *search_data;
char **search_keys;
int num_of_search;

// For insert
char *insert_data;
char **insert_keys;
uint64_t dummy_value = 982;
int num_of_insert;

void generateKeys(strType t) {
    keys = IdGenerator::getKeys(default_key_cnt, t);
}

void _Myfree(void *&addr) {
    if (addr) {
        free(addr);
        addr = NULL;
    }
}

void freeData() {
    _Myfree((void *&)bulk_data);
    _Myfree((void *&)bulk_keys);
    _Myfree((void *&)bulk_vals);
    _Myfree((void *&)search_data);
    _Myfree((void *&)search_keys);
    _Myfree((void *&)insert_data);
    _Myfree((void *&)insert_keys);
}

void prepareSearchQuerys() {
    std::cout << "[Info]: Preparing search queries ..." << std::endl;

    // Prepare 20M keys (100% bulk load)
    int key_cnt = default_key_cnt;
    uint64_t bulk_byte_size = 0, bulk_ofs = 0;
    uint64_t search_byte_size = 0, search_ofs = 0;

    // Prepare the bulk load key number
    num_of_bulk = key_cnt;

    for (int i = 0; i < keys.size(); i++) {
        bulk_byte_size += keys[i].length() + 1;
    }

    // Prepare the bulk load data
    bulk_data = new char[bulk_byte_size];
    bulk_keys = new char *[num_of_bulk];
    bulk_vals = new uint64_t[num_of_bulk];

    // Copy the data
    for (int i = 0; i < keys.size(); ++i) {
        bulk_keys[i] = bulk_data + bulk_ofs;
        bulk_vals[i] = i + 1;
        memcpy(bulk_data + bulk_ofs, keys[i].c_str(), keys[i].length() + 1);
        bulk_ofs += keys[i].length() + 1;
    }

    assert(bulk_ofs == bulk_byte_size);

    // 10M keys for possitive search
    std::random_shuffle(keys.begin(), keys.end());

    // Prepare the search key number
    num_of_search = default_search_cnt;

    for (int i = 0; i < num_of_search; i++) {
        search_byte_size += keys[i].length() + 1;
    }

    // Prepare the search data
    search_data = new char[search_byte_size];
    search_keys = new char *[num_of_search];

    // Copy the data
    for (int i = 0; i < num_of_search; ++i) {
        search_keys[i] = search_data + search_ofs;
        memcpy(search_data + search_ofs, keys[i].c_str(), keys[i].length() + 1);
        search_ofs += keys[i].length() + 1;
    }

    assert(search_ofs == search_byte_size);
}

void prepareInsertQuerys() {
    std::cout << "[Info]: Preparing insert queries ..." << std::endl;

    // Prepare 20M keys (50% bulk load, 50% insert)
    int key_cnt = default_key_cnt;
    uint64_t bulk_byte_size = 0, bulk_ofs = 0;
    uint64_t insert_byte_size = 0, insert_ofs = 0;

    // Prepare the bulk load key number
    num_of_bulk = key_cnt / 2;
    num_of_insert = key_cnt / 2;

    // Randomly shuffle the keys
    std::random_shuffle(keys.begin(), keys.end());

    // Sort the 50% keys for bulk load
    std::partial_sort(keys.begin(), keys.begin() + num_of_bulk, keys.end());

    for (int i = 0; i < num_of_bulk; i++) {
        bulk_byte_size += keys[i].length() + 1;
    }
    for (int i = num_of_bulk; i < num_of_bulk + num_of_insert; i++) {
        insert_byte_size += keys[i].length() + 1;
    }

    // Prepare the bulk load data
    bulk_data = new char[bulk_byte_size];
    bulk_keys = new char *[num_of_bulk];
    bulk_vals = new uint64_t[num_of_bulk];

    // Prepare the insert data
    insert_data = new char[insert_byte_size];
    insert_keys = new char *[num_of_insert];

    // Copy the data
    for (int i = 0; i < num_of_bulk; ++i) {
        // Copy the bulk load data
        bulk_keys[i] = bulk_data + bulk_ofs;
        bulk_vals[i] = i + 1;
        memcpy(bulk_data + bulk_ofs, keys[i].c_str(), keys[i].length() + 1);
        bulk_ofs += keys[i].length() + 1;
    }
    for (int i = 0; i < num_of_insert; ++i) {
        // Copy the insert data
        insert_keys[i] = insert_data + insert_ofs;
        memcpy(insert_data + insert_ofs, keys[i + num_of_bulk].c_str(),
               keys[i + num_of_bulk].length() + 1);
        insert_ofs += keys[i + num_of_bulk].length() + 1;
    }
}

void prepareScanQuerys() {
    std::cout << "[Info]: Preparing scan queries ..." << std::endl;

    // Prepare 20M keys (100% bulk load)
    int key_cnt = default_key_cnt;
    uint64_t bulk_byte_size = 0, bulk_ofs = 0;
    uint64_t search_byte_size = 0, search_ofs = 0;

    // Prepare the bulk load key number
    num_of_bulk = key_cnt;

    for (int i = 0; i < keys.size(); i++) {
        bulk_byte_size += keys[i].length() + 1;
    }

    // Prepare the bulk load data
    bulk_data = new char[bulk_byte_size];
    bulk_keys = new char *[num_of_bulk];
    bulk_vals = new uint64_t[num_of_bulk];

    // Copy the data
    for (int i = 0; i < keys.size(); ++i) {
        bulk_keys[i] = bulk_data + bulk_ofs;
        bulk_vals[i] = i + 1;
        memcpy(bulk_data + bulk_ofs, keys[i].c_str(), keys[i].length() + 1);
        bulk_ofs += keys[i].length() + 1;
    }

    assert(bulk_ofs == bulk_byte_size);

    // 1M keys for scan
    std::random_shuffle(keys.begin(), keys.end());

    // Prepare the search key number
    num_of_search = default_scan_cnt;

    for (int i = 0; i < num_of_search; i++) {
        search_byte_size += keys[i].length() + 1;
    }

    // Prepare the search data
    search_data = new char[search_byte_size];
    search_keys = new char *[num_of_search];

    // Copy the data
    for (int i = 0; i < num_of_search; ++i) {
        search_keys[i] = search_data + search_ofs;
        memcpy(search_data + search_ofs, keys[i].c_str(), keys[i].length() + 1);
        search_ofs += keys[i].length() + 1;
    }

    assert(search_ofs == search_byte_size);
}

void OutputResult(uint64_t checkSum, int numQuery, double second) {
    std::cout << "[Info]: Checksum:\t" << checkSum << std::endl;
    std::cout << "[Info]: Query Count:\t" << numQuery << std::endl;
    std::cout << "[Info]: Throughput:\t\033[32m" << numQuery / (1e6 * second)
              << " Mops\033[0m" << std::endl;
}

void LITS_Search_test() {
    lits::LITS index;
    uint64_t checkSum = 0;
    struct timeval tv1, tv2;
    double second;

    std::cout << "[Info]: Index bulk loading ... " << std::endl;

    // Bulk load the keys
    index.bulkload((const char **)(bulk_keys), (const uint64_t *)(bulk_vals),
                   num_of_bulk);

    std::cout << "[Info]: Index bulk loaded." << std::endl;

    gettimeofday(&tv1, NULL);

    for (int i = 0; i < num_of_search; ++i) {
        checkSum += index.lookup((const char *)(search_keys[i])) ? 1 : 0;
    }

    gettimeofday(&tv2, NULL);

    second = tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec) / 1000000.0;

    OutputResult(checkSum, num_of_search, second);

    index.destroy();
}

void LITS_Insert_test() {
    lits::LITS index;
    uint64_t checkSum = 0;
    struct timeval tv1, tv2;
    double second;

    std::cout << "[Info]: Index bulk loading ... " << std::endl;

    // Bulk load the keys
    index.bulkload((const char **)(bulk_keys), (const uint64_t *)(bulk_vals),
                   num_of_bulk);

    std::cout << "[Info]: Index bulk loaded." << std::endl;

    gettimeofday(&tv1, NULL);

    for (int i = 0; i < num_of_insert; ++i) {
        checkSum +=
            index.insert((const char *)(insert_keys[i]), dummy_value) ? 1 : 0;
    }

    gettimeofday(&tv2, NULL);

    second = tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec) / 1000000.0;

    OutputResult(checkSum, num_of_insert, second);

    index.destroy();
}

void LITS_Scan_test() {
    lits::LITS index;
    uint64_t checkSum = 0;
    struct timeval tv1, tv2;
    double second;

    std::cout << "[Info]: Index bulk loading ... " << std::endl;

    // Bulk load the keys
    index.bulkload((const char **)(bulk_keys), (const uint64_t *)(bulk_vals),
                   num_of_bulk);

    std::cout << "[Info]: Index bulk loaded." << std::endl;

    gettimeofday(&tv1, NULL);

    for (int i = 0; i < num_of_search; ++i) {
        int scan_range = rand() % default_scan_range + 1;
        auto iter = index.find((const char *)(search_keys[i]));
        for (int i = 0; i < scan_range && iter.not_finish(); ++i) {
            checkSum += iter.getKV()->v;
            iter.next();
        }
    }

    gettimeofday(&tv2, NULL);

    second = tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec) / 1000000.0;

    OutputResult(checkSum, num_of_search, second);

    index.destroy();
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (argc != 3) {
        std::cout << "Usage: " << std::endl;
        std::cout << argv[0] << " idcards/randstr 1/2/3" << std::endl;
        return 0;
    }

    int testMode = atoi(argv[2]);
    if (testMode < 1 || testMode > 3) {
        std::cout << "1: Search-Only Test" << std::endl;
        std::cout << "2: Insert-Only Test" << std::endl;
        std::cout << "3: Scan-Only Test" << std::endl;
        return 0;
    }

    if (strcmp(argv[1], "idcards") == 0)
        generateKeys(idcards);
    else if (strcmp(argv[1], "randstr") == 0)
        generateKeys(randstr);
    else {
        std::cout << "Invalid argument" << std::endl;
        return 0;
    }

    // Do Search Test
    if (testMode == 1) {
        std::cout << std::endl;
        std::cout << "\033[33m" << "[Search-Only Test] (100% bulk load, "
                  << default_search_cnt << " random search)"
                  << "\033[0m" << std::endl;
        prepareSearchQuerys();
        LITS_Search_test();
    }

    // Do Insert Test
    if (testMode == 2) {
        std::cout << std::endl;
        std::cout << "\033[33m"
                  << "[Insert-Only Test] (50% bulk load, 50% random insert)"
                  << "\033[0m" << std::endl;
        prepareInsertQuerys();
        LITS_Insert_test();
    }

    // Do Scan Test
    if (testMode == 3) {
        std::cout << std::endl;
        std::cout << "\033[33m" << "[Short Scan Test] (100% bulk load, "
                  << default_scan_cnt << " random scan)"
                  << "\033[0m" << std::endl;
        prepareScanQuerys();
        LITS_Scan_test();
    }

    // Free the data
    freeData();
}

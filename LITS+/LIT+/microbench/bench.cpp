#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <sys/time.h>
#include <vector>

#include <litsPlus.hpp>

#include <utils.hpp>

using namespace std;
using namespace litsPlus;

// ------------------------------------------------------------
// Debug helpers
// ------------------------------------------------------------
#define COUT_THIS(msg)                               \
    do {                                             \
        std::cout << "[COUT]: " << msg << std::endl; \
    } while (0)

#define COUT_VAR(var) std::cout << "[COUT]: " << #var << " = " << (var) << std::endl

// ------------------------------------------------------------
// Workload enum
// ------------------------------------------------------------

typedef enum {
    YCSB_A,
    YCSB_B,
    YCSB_C,
    YCSB_D,
    YCSB_E,
    YCSB_F,
    InsertOnly,
    ReadWrite
} workload_t;

// ------------------------------------------------------------
// Query queue holds string keys
// ------------------------------------------------------------

enum class QueryType : uint8_t {
    Lookup,
    Insert,
    Update,
    ReadModifyWrite,
    Scan50
};
using Query = std::pair<QueryType, std::string>; // (type, key)
using QueryQueue = std::vector<Query>;

// ------------------------------------------------------------
// Global containers
// ------------------------------------------------------------
static constexpr uint64_t DEFAULT_QUERY_CNT = 20'000'000ULL;
vector<Entry*> all_records;
vector<Entry*> bulk_load_records;
vector<Entry*> candidate_inserts;
vector<Entry*> candidate_updates;
QueryQueue query_queue;

// ------------------------------------------------------------
// readKeys & basicPartition (unchanged contents)
// ------------------------------------------------------------
void readKeys(const char* fname)
{
    std::ifstream file(fname);
    if (!file) {
        std::cerr << "open fail";
        exit(1);
    }
    std::string line;
    while (std::getline(file, line))
        all_records.push_back(Entry::create(line, 1));
}

void basicPartition(workload_t wl)
{
    /* shuffle the entries */
    std::shuffle(all_records.begin(), all_records.end(), std::mt19937 { std::random_device { }() });
    /*
     * for YCSB_C, bulk-load 100% keys
     * for insertOnly, bulk-load 10% keys
     * for other workload, bulk-load 80% keys
     */
    double bulk_load_ratio = (wl == YCSB_C ? 1.0 : (wl == InsertOnly ? 0.1 : (wl == ReadWrite ? 0.5 : 0.8)));
    std::cout << "Bulk-load Ratio: " << bulk_load_ratio << std::endl;
    size_t bulk = all_records.size() * bulk_load_ratio;
    bulk_load_records.assign(all_records.begin(), all_records.begin() + bulk);
    candidate_inserts.assign(all_records.begin() + bulk, all_records.end());
    candidate_updates.assign(all_records.begin(), all_records.end());

    /* sort the bulk-load entries */
    std::sort(bulk_load_records.begin(), bulk_load_records.end(),
        [](auto* a, auto* b) { return strcmp(a->key(), b->key()) < 0; });
}

// -----------------------------------------------------------------------------
//  ReadOnly: Zipf(α = 1) on bulk_load_records
// -----------------------------------------------------------------------------
void generateQueriesZipf_YCSB_C(void)
{
    query_queue.clear();
    query_queue.reserve(DEFAULT_QUERY_CNT);

    const size_t n = bulk_load_records.size();
    if (n == 0)
        return;

    std::vector<double> cdf(n);
    double Hn = 0.0;
    for (size_t i = 1; i <= n; ++i)
        Hn += 1.0 / static_cast<double>(i);

    double acc = 0.0;
    for (size_t i = 1; i <= n; ++i) {
        acc += (1.0 / static_cast<double>(i)) / Hn;
        cdf[i - 1] = acc;
    }
    cdf.back() = 1.0;

    std::mt19937_64 rng { 123456 };
    std::uniform_real_distribution<double> unif(0.0, 1.0);

    for (size_t q = 0; q < DEFAULT_QUERY_CNT; ++q) {
        const double u = unif(rng);
        size_t idx = static_cast<size_t>(std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin());

        query_queue.emplace_back(QueryType::Lookup, std::string(bulk_load_records[idx]->key()));
    }

    COUT_VAR(query_queue.size());
}

// ------------------------------------------------------------
// Query generation — push std::string keys
// ------------------------------------------------------------
void generateQueries(workload_t wl)
{
    query_queue.clear();
    if (wl == YCSB_F) {
        // For YCSB-F, there are 10M read and 10M read-modify-write by default
        // Here we treat RMW as a read and a update, so the query count is 30M
        query_queue.reserve(DEFAULT_QUERY_CNT * 3 / 2);
    } else
        query_queue.reserve(DEFAULT_QUERY_CNT);
    double /* Read Ratio */ rr, /* Write Ratio */ wr, /* Update Ratio */ ur;
    switch (wl) {
    case YCSB_A:
    case YCSB_F:
        rr = 0.5;
        ur = 0.5;
        wr = 0;
        break;
    case YCSB_B:
        rr = 0.95;
        ur = 0.05;
        wr = 0;
        break;
    case YCSB_C:
        rr = 1;
        ur = 0;
        wr = 0;
        break;
    case YCSB_D:
    case YCSB_E:
        rr = 0.95;
        ur = 0;
        wr = 0.05;
        break;
    default:
        rr = 0;
        wr = 1;
    }
    uint64_t wantW = DEFAULT_QUERY_CNT * wr;
    uint64_t wantU = DEFAULT_QUERY_CNT * ur;
    uint64_t wantR = DEFAULT_QUERY_CNT * rr;

    if (wl == InsertOnly) {
        wantW = candidate_inserts.size();
    }

    if (wl == ReadWrite) {
        wantW = candidate_inserts.size();
        wantR = wantW;
    }

    if (wantW > candidate_inserts.size()) {
        wantW = candidate_inserts.size();
    }

    if (wantU > candidate_updates.size()) {
        wantU = candidate_updates.size();
    }
    std::mt19937_64 rng { 123456 };

    if (wl == YCSB_D) {
        size_t win = 10000;
        vector<const char*> recent;
        recent.reserve(win);
        size_t next = 0;
        while (query_queue.size() < wantR + wantW) {
            if (next < wantW) {
                Entry* e = candidate_inserts[next++];
                query_queue.emplace_back(QueryType::Insert, e->key());
                recent.push_back(e->key());
                if (recent.size() > win)
                    recent.erase(recent.begin());
            }
            for (int i = 0; i < 19 && query_queue.size() < wantR + wantW && !recent.empty(); ++i) {
                size_t idx = zipf_idx(rng, recent.size());
                query_queue.emplace_back(QueryType::Lookup, std::string(recent[idx]));
            }
        }
    } else if (wl == YCSB_E || wl == InsertOnly) {
        vector<const char*> readKeys;
        readKeys.reserve(wantR);
        for (uint64_t i = 0; i < wantR; ++i)
            readKeys.push_back(bulk_load_records[i % bulk_load_records.size()]->key());
        std::shuffle(readKeys.begin(), readKeys.end(), rng);
        size_t ri = 0, wi = 0;
        while (ri < readKeys.size() || wi < wantW) {
            if (ri < readKeys.size())
                query_queue.emplace_back(QueryType::Scan50, std::string(readKeys[ri++]));
            if (wi < wantW)
                query_queue.emplace_back(QueryType::Insert, candidate_inserts[wi++]->key());
        }
    } else if (wl == ReadWrite) {
        for (int i = 0; i < candidate_inserts.size(); ++i) {
            query_queue.emplace_back(QueryType::Insert, candidate_inserts[i]->key());
        }

        // candicate has already been shuffled, so the lookup is random
        for (int i = 0; i < candidate_inserts.size(); ++i) {
            query_queue.emplace_back(QueryType::Lookup, candidate_updates[i]->key());
        }
    } else {
        vector<const char*> readKeys;
        readKeys.reserve(wantR);
        for (uint64_t i = 0; i < wantR; ++i)
            readKeys.push_back(bulk_load_records[i % bulk_load_records.size()]->key());
        std::shuffle(readKeys.begin(), readKeys.end(), rng);
        size_t ri = 0, ui = 0;
        while (ri < readKeys.size() || ui < wantU) {
            if (ri < readKeys.size())
                query_queue.emplace_back(QueryType::Lookup, std::string(readKeys[ri++]));
            if (ui < wantU) {
                if (wl == YCSB_F) {
                    query_queue.emplace_back(QueryType::Lookup, candidate_updates[ui]->key());
                }
                query_queue.emplace_back(QueryType::Update, candidate_updates[ui++]->key());
            }
        }
    }
    COUT_VAR(query_queue.size());
}

// ------------------------------------------------------------
// LITS+ benchmark — create Entry on‑the‑fly for insert
// ------------------------------------------------------------
void runBenchmarkLITSP(workload_t wl)
{
    Index<vector<Entry*>::iterator> idx;
#ifdef USE_PMSS
    double read_ratio = 1;
    idx.build(bulk_load_records.begin(), bulk_load_records.end(), "../PMSS", read_ratio);
#else
    idx.build(bulk_load_records.begin(), bulk_load_records.end());
#endif
    uint64_t sum = 0;
    timeval t1 { }, t2 { };
    gettimeofday(&t1, nullptr);
    for (auto& q : query_queue) {
        if (q.first == QueryType::Lookup) {
            auto r = idx.lookup(q.second.c_str(), q.second.length());
            if (r)
                sum += 1;
        } else if (q.first == QueryType::ReadModifyWrite) {
            auto r = idx.lookup(q.second.c_str(), q.second.length());
            if (r)
                sum += 1;

            sum += idx.upsert(q.second.c_str(), q.second.length(), 1);
        } else if (q.first == QueryType::Scan50) {
            auto r = idx.find(q.second.c_str(), q.second.length());
            for (int loop = 0; loop < 50; ++loop) {
                r.next();
                if (r.isFinish())
                    break;
                sum += r.getEntry()->value();
            }
        } else // use <upsert> for Update and Insert
        {
            sum += idx.upsert(q.second.c_str(), q.second.length(), 1);
        }
    }
    gettimeofday(&t2, nullptr);

    auto sz = idx.getSizeInBytes();
    std::cout << "\t[LITS+] Index Size: " << sz / (1024 * 1024) << " MB\n";

    double s = t2.tv_sec - t1.tv_sec + (t2.tv_usec - t1.tv_usec) / 1e6;
    std::cout << "\t[LITS+] Checksum: " << sum << "\n";
    uint64_t logical_ops = (wl == YCSB_F)
        ? DEFAULT_QUERY_CNT
        : query_queue.size();

    std::cout << "\t[LITS+] Throughput: "
              << (logical_ops / 1e6 / s)
              << " Mops\n";
}

// ------------------------------------------------------------
// main (arg parsing unchanged except removed distribution arg)
// ------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " YCSB_X data_file\n";
        return 1;
    }
    workload_t wl;
    {
        if (!strcmp(argv[1], "YCSB_A"))
            wl = YCSB_A;
        else if (!strcmp(argv[1], "YCSB_B"))
            wl = YCSB_B;
        else if (!strcmp(argv[1], "YCSB_C"))
            wl = YCSB_C;
        else if (!strcmp(argv[1], "YCSB_D"))
            wl = YCSB_D;
        else if (!strcmp(argv[1], "YCSB_E"))
            wl = YCSB_E;
        else if (!strcmp(argv[1], "YCSB_F"))
            wl = YCSB_F;
        else if (!strcmp(argv[1], "ReadWrite"))
            wl = ReadWrite;
        else if (!strcmp(argv[1], "InsertOnly"))
            wl = InsertOnly;
        else
            wl = YCSB_C;

        // ReadKeys will read keys from given file
        readKeys(argv[2]);

        // BasicPartition will prepare keys for bulk-load
        basicPartition(wl);
    }

    generateQueries(wl);
    // generateQueriesZipf_YCSB_C();
    runBenchmarkLITSP(wl);

    return 0;
}

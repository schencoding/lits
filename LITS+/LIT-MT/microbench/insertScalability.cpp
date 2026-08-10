#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <litsRCU.hpp> // Index / Entry

using namespace std;
using namespace litsRCU;

/* ───────────── config ───────────── */
constexpr size_t CHUNK = 1024;

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cerr << "Usage: " << argv[0] << " <input_file> <threads>\n";
        return 1;
    }
    const char *filename = argv[1];
    int n_threads = stoi(argv[2]);
    assert(n_threads > 0);

    /* ── load keys ─────────────────────────────────────────── */
    ifstream infile(filename);
    if (!infile.is_open())
    {
        cerr << "Error: Cannot open file " << filename << '\n';
        return 1;
    }
    vector<Entry *> entries;
    string line;
    while (getline(infile, line))
        if (!line.empty())
            entries.push_back(Entry::create(line, 1));
    infile.close();

    const size_t total = entries.size();
    if (total < 2)
    {
        cerr << "Not enough entries for split.\n";
        return 1;
    }
    const size_t bulkCount = total / 10;
    const size_t appendCount = total - bulkCount;

    cout << "Total entries: " << total << ", bulk-load " << bulkCount << ", append " << appendCount << '\n';

    /* ── shuffle & split ───────────────────────────────────── */
    mt19937 rng(42);
    shuffle(entries.begin(), entries.end(), rng);

    vector<Entry *> bulk_entries(entries.begin(), entries.begin() + bulkCount);
    vector<Entry *> append_entries(entries.begin() + bulkCount, entries.end());

    sort(bulk_entries.begin(), bulk_entries.end(), [](Entry *a, Entry *b) { return strcmp(a->key(), b->key()) < 0; });

    /* ── build index ───────────────────────────────────────── */
    Index<vector<Entry *>::iterator> index;

    auto t0 = std::chrono::steady_clock::now();
    index.build(bulk_entries.begin(), bulk_entries.end(), 2);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    cout << "Index bulk-loaded in " << ms << " ms.\n";

    /* ── multi-thread append phase ─────────────────────────── */
    atomic<size_t> cursor{0};
    atomic<size_t> global_ok{0};
    atomic<bool> start_flag{false};

    auto worker = [&]() {
        while (!start_flag.load(memory_order_acquire))
            ; // barrier

        size_t local_ok = 0;
        for (;;)
        {
            size_t begin = cursor.fetch_add(CHUNK, memory_order_relaxed);
            if (begin >= appendCount)
                break;
            size_t end = min(begin + CHUNK, appendCount);

            for (size_t i = begin; i < end; ++i)
                local_ok += index.insert(append_entries[i]);
        }
        global_ok.fetch_add(local_ok, memory_order_relaxed);
    };

    vector<thread> pool;
    for (int i = 0; i < n_threads; ++i)
        pool.emplace_back(worker);

    t0 = chrono::steady_clock::now();
    start_flag.store(true, memory_order_release); // let threads go

    for (auto &th : pool)
        th.join();
    t1 = chrono::steady_clock::now();

    /* ── stats ─────────────────────────────────────────────── */
    double seconds = chrono::duration<double>(t1 - t0).count();
    double throughputM = (appendCount / seconds) / 1'000'000.0;

    cout << "Append phase completed.\n"
         << "Appended: " << global_ok.load() << " / " << appendCount << '\n'
         << "Total time: " << seconds << " s\n"
         << "Throughput: " << throughputM << " Mops/s\n";

    // ── Verify all keys can be found ─────────────────────────
    cout << "Verifying all keys...\n";
    for (Entry *e : bulk_entries)
    {
        const Entry *r = index.lookup(e->key(), e->len());
        if (!r)
        {
            cerr << "Error: missing bulk key: " << e->key() << '\n';
            return 1;
        }
    }
    for (Entry *e : append_entries)
    {
        const Entry *r = index.lookup(e->key(), e->len());
        if (!r)
        {
            cerr << "Error: missing appended key: " << e->key() << '\n';
            return 1;
        }
    }
    cout << "All keys verified.\n";

    return 0;
}
#pragma once

#include "rss_utils.hpp"

static void printByteKey(const std::string& key, const int ofs) {
    for (int i = ofs; i < key.length(); ++i) {
        std::cout << (uint64_t)(key[i]) << " ";
    }
    std::cout << std::endl;
}

class RSSNode {
    // Only support 8 bytes span
    static_assert(LEN == 8);

public:
    typedef std::unordered_map<uint64_t, RSSNode*> rmap_t;
    typedef std::unordered_map<uint64_t, int> smap_t;
    typedef std::pair<uint64_t, int> record_t;

public:
    // The depth which the model operates
    int depth;

    // The bounds which the model operates
    int min;
    int max;

    // The redirect map which is used to store unpredictable prefixes
    rmap_t rmap;

    // The radix spline model used to predict positions
    rs::RadixSpline<uint64_t> model;

public:
    RSSNode() = default;

    RSSNode(const RSSNode& other) {
        depth = other.depth;
        min = other.min;
        max = other.max;
        rmap = other.rmap;
        model = other.model;
    }

    RSSNode(const std::vector<kv_pair>& kvs, int min, int max, int depth) {
        // Decide the responding boundary and depth
        this->depth = depth;
        this->min = min;
        this->max = max;

        // Decide the crucial bytes
        int byte_offset = depth * LEN;

        // Decide the training data
        std::vector<record_t> training_data;
        record_t lastRecord = {0, 0};
        for (int i = min; i <= max; ++i) {
            const std::string& key = kvs[i].first;
            record_t record = {Str2Int(key, byte_offset), i};
            if (record.first >= lastRecord.first) {
            } else {
                std::cout << "Depth: " << depth << std::endl;
                if (i > 0) {
                    const std::string& lastKey = kvs[i - 1].first;
                    std::cout << "Previous Key: " << std::endl;
                    printByteKey(lastKey, byte_offset);
                }
                printByteKey(key, byte_offset);
                std::cout << "Curr Int: " << Str2Int(key, byte_offset)
                          << std::endl;
                std::cout << "Last Int: " << lastRecord.first << std::endl;
                getchar();
            }
            training_data.push_back(record);
            lastRecord = record;
        }

        // std::cout << (int)log2(kvs.size()) << std::endl;
        // int radix_bits = std::max(6, std::min(18, (int)log2(kvs.size())));
        int radix_bits = depth == 0 ? 18 : 6;

        // Train the model
        rs::Builder<uint64_t> rsb(training_data.front().first,
                                  training_data.back().first + 1, radix_bits);
        // std::cout << "Min Key: " << training_data.front().first << std::endl;
        // std::cout << "Max Key: " << training_data.back().first + 1 <<
        // std::endl; getchar();
        for (const auto& data : training_data) {
            // std::cout << "Try to add " << data.first << std::endl;
            rsb.AddKey(data.first);
        }
        model = rsb.Finalize();

        // Iterate through all elements, pick out the ones
        // which do not follow the error bound
        uint64_t last_naughty_data = 0;
        for (int i = 0; i < training_data.size(); ++i) {
            // The training data and the error bound provided by the model
            const record_t& data = training_data[i];
            rs::SearchBound bound = model.GetSearchBound(data.first);

            // Recursively build a new RSSNode when the data is naughty
            if (data.second >= (bound.end + min) ||
                data.second < (bound.begin + min)) {
                if (data.first == last_naughty_data) {
                    continue;
                }
                last_naughty_data = data.first;

                // Decide the range
                int smin, smax;

                for (smin = i;
                     smin >= 0 && training_data[smin].first == data.first;
                     smin--)
                    ;
                smin += 1;

                for (smax = i; smax < training_data.size() &&
                               training_data[smax].first == data.first;
                     smax++)
                    ;
                smax -= 1;

                // Begin and end index in the raw string array
                int raw_str_l = training_data[smin].second;
                int raw_str_r = training_data[smax].second;

                assert(raw_str_l == (min + smin));
                assert(raw_str_r == (min + smax));

                if ((smax - smin) == 0) {
                    // Should not meet here: BUG fixed on 11.5
                    assert(false);
                }

                RSSNode* new_node =
                    new RSSNode(kvs, raw_str_l, raw_str_r, depth + 1);

                // Add the node into redirect map
                rmap.insert({data.first, new_node});
            }
        }
    }

    rs::SearchBound GetSearchBound(const std::string& key, size_t& dd) {
        dd += 1;

        // Byte offset decided by the depth
        int byte_offset = depth * LEN;

        // Extract the section
        uint64_t num = Str2Int(key, byte_offset);

        // Try find the child in the redirect map
        auto next_child = rmap.find(num);

        if (next_child != rmap.end()) {
            return next_child->second->GetSearchBound(key, dd);
        } else {
            auto bound = model.GetSearchBound(num);
            return {bound.begin + min, bound.end + min};
        }
    }

    uint64_t GetSize(uint64_t& modelSize) {
        uint64_t size = 0;
        for (auto& p : rmap) {
            size += p.second->GetSize(modelSize);
        }
        modelSize += model.GetSize();
        size += model.GetSize();
        size += rmap.size() * (sizeof(uint64_t) + sizeof(RSSNode*));
        size += sizeof(int) * 2;
        return size;
    }
};

class RSS {
public:
    RSSNode root;
    std::vector<kv_pair>* data;

public:
    RSS() = delete;
    RSS(std::vector<kv_pair>& kvs) {
        root = RSSNode(kvs, 0, kvs.size() - 1, 0);
        data = &kvs;
    }

    inline uint64_t lookup(const std::string& key, size_t& dd) {
        rs::SearchBound bound = root.GetSearchBound(key, dd);
        auto res = BinarySearch(*data, key, bound.begin, bound.end).second;
        return res;
    }

    inline uint64_t GetSize() {
        uint64_t modelSize = 0;
        uint64_t indexSize = root.GetSize(modelSize);
        std::cout << "Model Size: " << modelSize << std::endl;
        std::cout << "Index Size: " << indexSize << std::endl;
        return indexSize;
    }
};
#pragma once

#include <litsPlus_base.hpp>

#include <iostream>

namespace litsPlus {

    /**
     * Hash-enhanced Prefix Table
     */
    class HPT {
    public:
        // Attenuation factor, should be in (0, 1]
        static constexpr double AF = 0.5;

        // Position hash bits length
        static constexpr int PS_HASH_LEN = 5;

        // Front char hash bits length
        static constexpr int FC_HASH_LEN = 5;

        // Position hash mask and front char hash mask
        static constexpr uint32_t PS_MASK = (1 << PS_HASH_LEN) - 1;
        static constexpr uint32_t FC_MASK = (1 << FC_HASH_LEN) - 1;

        // Position array length and front char array length
        static constexpr uint32_t PS_SZ = PS_MASK + 1;
        static constexpr uint32_t FC_SZ = FC_MASK + 1;

        // Units in a table line
        class UNI {
        public:
            double CDF;  // Cumulative Distribution Function
            double PRO;

        public:
            UNI()
                : CDF(0), PRO(0) {}
            ~UNI() = default;
        };

        // Hash-enhanced Prefix Table
        UNI* m[PS_SZ][FC_SZ];

    public:
        HPT() {
            for (int i = 0; i < PS_SZ; ++i) {
                for (int j = 0; j < FC_SZ; ++j) {
                    m[i][j] = new UNI[MaxChar];
                }
            }
        }

        ~HPT() { destroy(); };

        void destroy() {
            for (int i = 0; i < PS_SZ; ++i) {
                for (int j = 0; j < FC_SZ; ++j) {
                    delete[] m[i][j];
                }
            }
        }

        size_t unit_size() { return sizeof(UNI); }

        size_t model_size() { return sizeof(UNI) * PS_SZ * FC_SZ * MaxChar; }

        template <typename EntryIter>
        bool train(EntryIter begin, EntryIter end) {
            // Variables
            double this_line_wgt;
            double weight[256];
            unsigned char src_ch, dst_ch;

            // Global common prefix length
            const uint8_t gcpl = ustrcpl((*begin)->key(), (*(end - 1))->key());

            // Init the weight
            weight[0] = 1;
            for (int i = 1; i < 256; ++i) {
                weight[i] = weight[i - 1] * AF;
            }

            // Recording the pairs
            for (auto it = begin; it != end; ++it) {
                // We only consider the distinguishing prefix
                int max_len = 0;

                if (it == begin)
                    max_len = ustrcpl((*begin)->key(), (*(begin + 1))->key()) + 1;
                else if (it == (end - 1))
                    max_len = ustrcpl((*(end - 2))->key(), (*(end - 1))->key()) + 1;
                else
                    max_len = std::max<int>(ustrcpl((*it)->key(), (*(it - 1))->key()),
                                            ustrcpl((*it)->key(), (*(it + 1))->key())) +
                              1;

                // Record the occurance frequency in table
                for (int b = gcpl; b < max_len; ++b) {
                    const char* key = (*it)->key();
                    dst_ch = key[b];
                    int _ps = b & PS_MASK;
                    int _fc = b == 0 ? 0 : (key[b - 1] & FC_MASK);
                    m[_ps][_fc][dst_ch].CDF += weight[b - gcpl];
                }
            }

            // Generate the cdf distribution from the frequency
            for (int x = 0; x < PS_SZ; ++x) {
                for (int y = 0; y < FC_SZ; ++y) {
                    this_line_wgt = 0;
                    for (int j = 0; j < MaxChar; ++j) {
                        this_line_wgt += m[x][y][j].CDF;
                    }
                    if (this_line_wgt <= 0)
                        continue;
                    for (int j = 0; j < MaxChar; ++j) {
                        m[x][y][j].CDF /= this_line_wgt;
                        m[x][y][j].PRO = m[x][y][j].CDF;
                    }
                    double sum = m[x][y][0].CDF;
                    m[x][y][0].CDF = 0;
                    for (int j = 1; j < MaxChar; ++j) {
                        double tmp = m[x][y][j].CDF;
                        m[x][y][j].CDF = sum;
                        sum += tmp;
                    }
                }
            }

            // Always success to train
            return true;
        }

        inline double getCdfUniform(const char* key, const int gcpl = 0) const {
            return static_cast<double>(key[gcpl]) / 128;
        }

        inline double getCdf(const char* key, const int gcpl = 0) const {
            static constexpr double limit = 1. / (1ULL << 32);

            double ps = 1;
            double c = 0;

            for (int i = gcpl; key[i] && ps >= limit; ++i) {
                const int fc = i == 0 ? 0 : key[i - 1] & FC_MASK;
                const auto& uni = m[i & PS_MASK][fc][key[i]];
                c += ps * uni.CDF;
                ps *= uni.PRO;
            }

            return c;
        }
    };

};  // namespace lits
#pragma once

#include "lits_base.hpp"

#include <iostream>
#include <xmmintrin.h>

namespace lits {

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
        double CDF; // Cumulative Distribution Function
        double PRO;

      public:
        UNI() : CDF(0), PRO(0) {}
        ~UNI() = default;
    };

    // Hash-enhanced Prefix Table
    UNI *m[PS_SZ][FC_SZ];

  public:
    HPT() {
        // The table cannot be too large!
        // ST_ASSERT((PS_HASH_LEN + FC_HASH_LEN) <= 12);

        for (int i = 0; i < PS_SZ; ++i) {
            for (int j = 0; j < FC_SZ; ++j) {
                m[i][j] = new UNI[MAX_CH];
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

    /**
     * Return the byte size of a UNI.
     */
    size_t unit_size() { return sizeof(UNI); }

    /**
     * Return the byte size of the model.
     */
    size_t model_size() { return sizeof(UNI) * PS_SZ * FC_SZ * MAX_CH; }

    /**
     * Train the HPT.
     * @param keys The input keys.
     * @param len The input data size.
     *
     * @return true when success, false otherwise.
     */
    bool train(const str *keys, const int len) {
        // Variables
        double this_line_wgt;
        double weight[256];
        unsigned char src_ch, dst_ch;

        // Global common prefix length
        const uint8_t gcpl = ucpl(keys[0], keys[len - 1]);

        // Init the weight
        weight[0] = 1;
        for (int i = 1; i < 256; ++i) {
            weight[i] = weight[i - 1] * AF;
        }

        // Recording the pairs
        for (int i = 0; i < len; ++i) {
            // We only consider the distinguishing prefix
            int max_len = 0;

            if (i == 0)
                max_len = ucpl(keys[0], keys[1]) + 1;
            else if (i == len - 1)
                max_len = ucpl(keys[len - 1], keys[len - 2]) + 1;
            else
                max_len = std::max<int>(ucpl(keys[i], keys[i - 1]),
                                        ucpl(keys[i], keys[i + 1])) +
                          1;

            // Record the occurance frequency in table
            for (int b = gcpl; b < std::min<int>(ustrlen(keys[i]), max_len);
                 ++b) {
                dst_ch = keys[i][b];
                int _ps = b & PS_MASK;
                int _fc = b == 0 ? 0 : (keys[i][b - 1] & FC_MASK);
                m[_ps][_fc][dst_ch].CDF += weight[b - gcpl];
            }
        }

        // Generate the cdf distribution from the frequency
        for (int x = 0; x < PS_SZ; ++x) {
            for (int y = 0; y < FC_SZ; ++y) {
                this_line_wgt = 0;
                for (int j = 0; j < MAX_CH; ++j) {
                    this_line_wgt += m[x][y][j].CDF;
                }
                if (this_line_wgt <= 0)
                    continue;
                for (int j = 0; j < MAX_CH; ++j) {
                    m[x][y][j].CDF /= this_line_wgt;
                    m[x][y][j].PRO = m[x][y][j].CDF;
                }
                double sum = m[x][y][0].CDF;
                m[x][y][0].CDF = 0;
                for (int j = 1; j < MAX_CH; ++j) {
                    double tmp = m[x][y][j].CDF;
                    m[x][y][j].CDF = sum;
                    sum += tmp;
                }
            }
        }

        // Always success to train
        return true;
    }

    /**
     * Get the position in the LITS node's item array.
     *
     * @param key The input key.
     * @param size The item array's length.
     * @param gcpl The group's partial key length. Calculation will skip the
     * common prefix.
     * @param ssl Second Skip Length. Provided by one byte index.
     * @param k The local linear model's slope.
     * @param b The local linear model's intercept.
     *
     * @return a integer which stands for key's position in the node array.
     */
    inline int getPos(const str key, const int size, int gcpl, double k = 1,
                      double b = 0) const {
        double ps = size * k;
        double c = size * b;

        for (int i = gcpl; key[i] && ps >= 1; ++i) {
            const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];
            c += ps * uni.CDF;
            ps *= uni.PRO;
        }

        return static_cast<int>(c);
    }

    inline int getPos_woGCPL(const str key, const int size, double k = 1,
                             double b = 0) const {
        double pro = size * k;
        double cdf = size * b;

        const auto &uni = m[0][0][key[0]];
        cdf += pro * uni.CDF;
        pro *= uni.PRO;

        for (int i = 1; key[i] && pro >= 1; ++i) {
            const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];
            cdf += pro * uni.CDF;
            pro *= uni.PRO;
        }

        return static_cast<int>(cdf);
    }

    /**
     * Return a CDF value of key which is NOT processed by the local model.
     */
    inline double getCdf(const str key, int gcpl) const {
        double pro = 1;
        double cdf = 0;
        static constexpr double min_double = 1. / (1UL << 52);
        for (int i = gcpl; key[i] && pro >= min_double; ++i) {
            const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];
            cdf += pro * uni.CDF;
            pro *= uni.PRO;
        }
        return cdf;
    }
};

}; // namespace lits
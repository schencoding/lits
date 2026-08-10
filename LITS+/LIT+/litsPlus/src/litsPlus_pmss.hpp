#pragma once

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <litsPlus_slot.hpp>

namespace litsPlus {

inline int ustrlen(const char* s)
{
    /* Iterate over the string until we reach the null character,
       and return the number of characters we encountered. */
    int i = 0;
    for (; s[i]; ++i) {
        // do nothing, just iterate
    }
    return i;
}

/**
 * Return the common prefix length of two strings.
 *
 * Find the length of the common prefix of two null-terminated strings s1 and
 * s2.
 *
 * @param s1 a null-terminated string
 * @param s2 a null-terminated string
 *
 * @return the length of the common prefix of s1 and s2
 */
inline int ucpl(const char* s1, const char* s2)
{
    int i = 0;
    /* Find the first character that differs between s1 and s2, or the
       null character if the strings are identical up to that point. */
    for (; s1[i] && s2[i] && s1[i] == s2[i]; ++i)
        ; /* do nothing, just iterate */
    return i;
}

/**
 * Return the distinguishing prefix length of string s1 and s2.
 *
 * This function returns the length of the common prefix of s1 and s2 plus 1.
 * If the strings have a common prefix of length n, then s1 and s2 differ in
 * their n+1-th character.
 */
inline int udpl(const char* s1, const char* s2)
{
    return ucpl(s1, s2) + 1;
}

/**
 * Return the longest distinguishing prefix length between s1, s2 and s3.
 *
 * This function returns the longest of the two distinguishing prefix lengths
 * between s1 and s2, and between s2 and s3. The strings must be given in
 * non-decreasing order, i.e. s1 <= s2 <= s3.
 *
 * @param s1 the first string
 * @param s2 the second string
 * @param s3 the third string
 *
 * @return the longest distinguishing prefix length between s1, s2 and s3
 */
inline int udpl(const char* s1, const char* s2, const char* s3)
{
    /* Find the longest of the two distinguishing prefix lengths between
       s1 and s2, and between s2 and s3. */
    return std::max<int>(udpl(s1, s2), udpl(s2, s3));
}

template <typename EntryIter>
double getGPKL(EntryIter begin, EntryIter end)
{
    const int len = std::distance(begin, end); // Length of the group
    double lcpl = ucpl((*begin)->key(), (*(end - 1))->key()); // Local Common Prefix Length
    double dkl_sum = 0; // Sum of the Distinguishing Prefix Lengths

    for (auto it = begin; it != end; ++it) {
        if (it == begin)
            dkl_sum += udpl((*begin)->key(),
                (*(begin + 1))->key());
        else if (it == end - 1)
            dkl_sum += udpl((*(end - 2))->key(),
                (*(end - 1))->key());
        else
            dkl_sum += udpl((*(it - 1))->key(),
                (*(it))->key(),
                (*(it + 1))->key());
    }

    double avg_dkl = dkl_sum / len; // Average Distinguishing Prefix Length
    return avg_dkl - lcpl; // Local Partial Key Length
}

/*---------------------------------------------------------------
 *  LatencyTable  – one CSV  → 2-D sparse map  + bilinear interp
 *-------------------------------------------------------------*/
class LatencyTable {
public:
    explicit LatencyTable(const std::string& csvPath) { loadCSV(csvPath); }

    /* bilinear interpolation (row = gpkl, col = n) */
    double estimate(double gpkl, double n) const
    {
        if (rows.empty() || cols.empty())
            throw std::runtime_error("Latency table is empty");

        gpkl = std::clamp(gpkl, rows.begin()->first, rows.rbegin()->first);
        n = std::clamp(n, cols.begin()->first, cols.rbegin()->first);

        // locate bounding grid in both axes
        auto hiR = rows.lower_bound(gpkl);
        auto loR = (hiR == rows.begin() ? hiR : std::prev(hiR));
        if (hiR == rows.end())
            hiR = loR;

        auto hiC = cols.lower_bound(n);
        auto loC = (hiC == cols.begin() ? hiC : std::prev(hiC));
        if (hiC == cols.end())
            hiC = loC;

        double r1 = loR->first, r2 = hiR->first;
        double c1 = loC->first, c2 = hiC->first;

        const auto& row1 = data.at(r1);
        const auto& row2 = data.at(r2);

        double f11 = row1.at(c1);
        double f12 = row1.at(c2);
        double f21 = row2.at(c1);
        double f22 = row2.at(c2);

        double t = (r1 == r2) ? 0.0 : (gpkl - r1) / (r2 - r1);
        double u = (c1 == c2) ? 0.0 : (n - c1) / (c2 - c1);

        // bilinear interpolation
        return (1 - t) * (1 - u) * f11 + (1 - t) * u * f12 + t * (1 - u) * f21 + t * u * f22;
    }

private:
    std::map<double, std::map<double, double>> data; // [row][col] = latency
    std::map<double, char> rows; // keys only (value dummy)
    std::map<double, char> cols;

    void loadCSV(const std::string& path)
    {
        std::ifstream ifs(path);
        if (!ifs)
            throw std::runtime_error("cannot open " + path);

        std::string line;

        // first line: column headers
        if (!std::getline(ifs, line))
            throw std::runtime_error("empty CSV " + path);

        std::vector<double> colKeys;
        {
            std::stringstream ss(line);
            std::string tok;
            std::getline(ss, tok, ','); // skip header label
            while (std::getline(ss, tok, ',')) {
                colKeys.push_back(std::stod(tok));
                cols[colKeys.back()] = 0;
            }
        }

        // remaining lines
        while (std::getline(ifs, line)) {
            std::stringstream ss(line);
            std::string tok;
            if (!std::getline(ss, tok, ','))
                continue;
            double rowKey = std::stod(tok);
            rows[rowKey] = 0;

            std::size_t idx = 0;
            while (idx < colKeys.size() && std::getline(ss, tok, ',')) {
                if (!tok.empty())
                    data[rowKey][colKeys[idx]] = std::stod(tok);
                else {
                    // For empty cells, we simply assign a default value of 0.0.
                    // This will not affect correctness since we make final decision based on
                    // linear regression
                    data[rowKey][colKeys[idx]] = 0.0;
                }
                ++idx;
            }
            // std::getline discards trailing empty fields when a line ends with delimiters.
            // Fill any remaining columns with 0.0 to keep the table rectangular.
            while (idx < colKeys.size()) {
                data[rowKey][colKeys[idx]] = 0.0;
                ++idx;
            }
        }
    }
};

/*---------------------------------------------------------------
 *  LatencyModel  – holds both tables  + convenience wrappers
 *-------------------------------------------------------------*/
template <typename EntryIter>
class LatencyModel {
public:
    LatencyModel(const std::string& litRCsv,
        const std::string& litWCsv,
        const std::string& hotRCsv,
        const std::string& hotWCsv,
        const double rr)
        : lit_r(litRCsv)
        , lit_w(litWCsv)
        , hot_r(hotRCsv)
        , hot_w(hotWCsv)
        , read_ratio(rr)
    {
    }

    double litLatency(double g, double n) const
    {
        const double rr = read_ratio;
        const double wr = 1 - rr;
        return rr * lit_r.estimate(g, n) + wr * lit_w.estimate(g, n);
    }
    double hotLatency(double g, double n) const
    {
        const double rr = read_ratio;
        const double wr = 1 - rr;
        return rr * hot_r.estimate(g, n) + wr * hot_w.estimate(g, n);
    }

    double speedupLIT(double g, double n) const // >1  means LIT faster
    {
        return hotLatency(g, n) / (litLatency(g, n) + 1);
    }

    Slot::Type PMSS(EntryIter begin, EntryIter end) const
    {
        double dataSize = end - begin;
        double gpkl = getGPKL(begin, end);
        return speedupLIT(gpkl, dataSize) > 1 ? Slot::Type::MNodeSlot : Slot::Type::SubTreeSlot;
    }

private:
    double read_ratio = 1;
    LatencyTable lit_r, hot_r;
    LatencyTable lit_w, hot_w;
};
};

/* ------------------ Example usage -------------------
#include "LatencyModel.hpp"

int main() {
    LatencyModel lm("latency_LIT.csv", "latency_HOT.csv");

    double gpkl = 7.5;
    double n    = 500;     // dataSize
    std::cout << "LIT  : " << lm.litLatency (gpkl,n) << " ns\n";
    std::cout << "HOT  : " << lm.hotLatency (gpkl,n) << " ns\n";
    std::cout << "speed-up HOT vs LIT: " << lm.speedupHOT(gpkl,n) << "×\n";
}
------------------------------------------------------- */

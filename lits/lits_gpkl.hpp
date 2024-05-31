#pragma once

#include "lits_base.hpp"

namespace lits {

/**
 * Returns the length of a null-terminated string.
 *
 * @param s a null-terminated string
 *
 * @return the length of the string, excluding the terminating null character
 */
inline int ustrlen(const str s) {
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
inline int ucpl(const str s1, const str s2) {
    int i = 0;
    /* Find the first character that differs between s1 and s2, or the
       null character if the strings are identical up to that point. */
    for (; s1[i] && s2[i] && s1[i] == s2[i]; ++i)
        ; /* do nothing, just iterate */
    return i;
}

/**
 * Return the common prefix length of two std::strings.
 *
 * Find the length of the common prefix of two null-terminated strings s1 and
 * s2. The null-terminated C-style strings are obtained by calling c_str() on
 * the std::string objects.
 *
 * @param s1 a std::string
 * @param s2 a std::string
 *
 * @return the length of the common prefix of s1 and s2
 */
inline int ucpl(const std::string &s1, const std::string &s2) {
    // Get the null-terminated C-style string representations of s1 and s2
    const char *cs1 = s1.c_str();
    const char *cs2 = s2.c_str();
    return ucpl(cs1, cs2);
}

/**
 * Return the distinguishing prefix length of string s1 and s2.
 *
 * This function returns the length of the common prefix of s1 and s2 plus 1.
 * If the strings have a common prefix of length n, then s1 and s2 differ in
 * their n+1-th character.
 */
inline int udpl(const str s1, const str s2) { return ucpl(s1, s2) + 1; }

/**
 * Return the distinguishing prefix length of two std::strings.
 *
 * @param s1, s2 input strings
 *
 * @return the length of the common prefix plus 1
 */
inline int udpl(const std::string &s1, const std::string &s2) {
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
inline int udpl(const str s1, const str s2, const str s3) {
    /* Find the longest of the two distinguishing prefix lengths between
       s1 and s2, and between s2 and s3. */
    return std::max<int>(udpl(s1, s2), udpl(s2, s3));
}

/**
 * Return the longest distinguishing prefix length between s1, s2, and s3.
 *
 * This function returns the longest of the two distinguishing prefix lengths
 * between s1 and s2, and between s2 and s3. The strings must be given in
 * non-decreasing order, i.e. s1 <= s2 <= s3.
 *
 * @param s1 the first string
 * @param s2 the second string
 * @param s3 the third string
 *
 * @return the longest distinguishing prefix length between s1, s2, and s3
 */
inline int udpl(const std::string &s1, const std::string &s2,
                const std::string &s3) {
    // Find the longest of the two distinguishing prefix lengths between
    // s1 and s2, and between s2 and s3.
    return std::max<int>(udpl(s1, s2), udpl(s2, s3));
}

/**
 * Calculate the local group partial key length (LPKL) of a group.
 *
 * The LPKL is a measure of how much information each element in the group
 * provides to the group's key. A higher LPKL means that each element in the
 * group provides more information to the group's key.
 *
 * The LPKL is calculated as the average length of the distinguishing prefixes
 * between each element in the group and the previous element, plus the
 * following element, minus the group common prefix length. The group common
 * prefix length is the length of the common prefix of all elements in the
 * group.
 *
 * @param keys the group keys, in non-decreasing order
 * @param len the number of keys in the group
 *
 * @return the local group partial key length of the group
 */
inline double lpkl(const str *keys, const int len) {
    // Group Common Prefix Length
    double gcpl = ucpl(keys[0], keys[len - 1]);
    double dkl_sum = 0;

    // Calculate the average length of the distinguishing prefixes between each
    // element in the group and its neighbors.
    for (int i = 0; i < len; ++i) {
        if (i == 0)
            dkl_sum += udpl(keys[0], keys[1]);
        else if (i == len - 1)
            dkl_sum += udpl(keys[len - 2], keys[len - 1]);
        else
            dkl_sum += udpl(keys[i - 1], keys[i], keys[i + 1]);
    }

    double avg_dkl = dkl_sum / len;
    return avg_dkl - gcpl;
}

/**
 * Compute the local partial key length of a group of strings.
 *
 * @param keys a vector of std::strings representing the group
 *
 * @return the local partial key length of the group
 */
inline double lpkl(const std::vector<std::string> &keys) {
    // Local Common Prefix Length
    const int len = keys.size();                // Length of the group
    double lcpl = ucpl(keys[0], keys[len - 1]); // Local Common Prefix Length
    double dkl_sum = 0; // Sum of the Distinguishing Prefix Lengths

    // Calculate the average length of the distinguishing prefixes between each
    // element in the group and its neighbors.
    for (int i = 0; i < len; ++i) {
        if (i == 0)
            dkl_sum += udpl(
                keys[0], keys[1]); // Prefix length of first and second elements
        else if (i == len - 1)
            dkl_sum += udpl(
                keys[len - 2],
                keys[len - 1]); // Prefix length of last and last-1 elements
        else
            dkl_sum += udpl(
                keys[i - 1], keys[i],
                keys[i + 1]); // Prefix length of ith element and its neighbors
    }

    double avg_dkl = dkl_sum / len; // Average Distinguishing Prefix Length
    return avg_dkl - lcpl;          // Local Partial Key Length
}

template <class records>
double getGPKL(const records &kvs, const int l, const int r) {
    const int len = r - l;                      // Length of the group
    double lcpl = ucpl(kvs[l].k, kvs[r - 1].k); // Local Common Prefix Length
    double dkl_sum = 0; // Sum of the Distinguishing Prefix Lengths

    // Calculate the average length of the distinguishing prefixes between each
    // element in the group and its neighbors.
    for (int i = l; i < r; ++i) {
        if (i == l)
            dkl_sum += udpl(
                kvs[l].k,
                kvs[l + 1].k); // Prefix length of first and second elements
        else if (i == r - 1)
            dkl_sum +=
                udpl(kvs[r - 2].k,
                     kvs[r - 1].k); // Prefix length of last and last-1 elements
        else
            dkl_sum += udpl(
                kvs[i - 1].k, kvs[i].k,
                kvs[i + 1].k); // Prefix length of ith element and its neighbors
    }

    double avg_dkl = dkl_sum / len; // Average Distinguishing Prefix Length
    return avg_dkl - lcpl;          // Local Partial Key Length
}

/**
 * Compares two null-terminated strings and returns their ordering.
 *
 * @param s1 a null-terminated string
 * @param s2 a null-terminated string
 *
 * @return 1 if s1 > s2, -1 if s1 < s2, 0 if s1 == s2
 */
inline int ustrcmp(const str s1, const str s2) {
    /* Iterate over both strings until we reach a character that differs
     * between the two strings, or until we reach the null character in both
     * strings. */
    int i = 0;
    for (; s1[i] && s2[i] && s1[i] == s2[i]; ++i)
        ;

    /* If the characters at the current position in both strings are equal,
     * then the strings are equal. */
    if (s1[i] == s2[i])
        return 0; // s1 == s2

    /* Otherwise, return the ordering of the characters at the current position
     * in both strings. */
    return s1[i] > s2[i] ? 1 : -1;
}

/**
 * Compare the given number of characters of two null-terminated strings and
 * return their ordering.
 *
 * @param s1 a null-terminated string
 * @param s2 a null-terminated string
 * @param len the number of characters to compare
 *
 * @return 1 if s1 > s2, -1 if s1 < s2, 0 if s1 == s2
 */
inline int ustrcmp(const str s1, const str s2, const int len) {
    /* Iterate over the given number of characters of both strings and compare
     * each character. If we encounter a character that differs between the two
     * strings, return the ordering of those characters. If all characters are
     * equal, then the strings are equal. */
    for (int i = 0; i < len; ++i) {
        if (s1[i] != s2[i]) {
            return s1[i] > s2[i] ? 1 : -1;
        }
    }
    return 0; // s1 == s2
}

}; // namespace lits

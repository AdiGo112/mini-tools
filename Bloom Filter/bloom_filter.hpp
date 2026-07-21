#pragma once
// Header-only Bloom filter. Probabilistic set: contains() may false-positive,
// never false-negative. C++17, no dependencies beyond stdlib.

#include <vector>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <functional>

template <typename T>
class BloomFilter {
public:
    // n = expected number of items, p = target false-positive rate (0<p<1).
    // These are the calibration knobs — m (bits) and k (hashes) derive from them.
    BloomFilter(std::size_t n, double p) {
        if (n == 0) n = 1;
        if (p <= 0.0 || p >= 1.0) p = 0.01;
        const double ln2 = std::log(2.0);
        std::size_t m = static_cast<std::size_t>(
            std::ceil(-(static_cast<double>(n) * std::log(p)) / (ln2 * ln2)));
        if (m < 1) m = 1;
        std::size_t k = static_cast<std::size_t>(
            std::round((static_cast<double>(m) / n) * ln2));
        if (k < 1) k = 1;
        m_bits = m;
        m_k = k;
        // ponytail: 1 byte/bit — pack into uint64_t words only if memory matters.
        m_data.assign(m, 0);
    }

    void add(const T& item) {
        uint64_t h1, h2;
        hashes(item, h1, h2);
        for (std::size_t i = 0; i < m_k; ++i)
            m_data[index(h1, h2, i)] = 1;
    }

    bool contains(const T& item) const {
        uint64_t h1, h2;
        hashes(item, h1, h2);
        for (std::size_t i = 0; i < m_k; ++i)
            if (!m_data[index(h1, h2, i)]) return false;
        return true;
    }

    std::size_t bits() const { return m_bits; }
    std::size_t num_hashes() const { return m_k; }

private:
    std::size_t m_bits;
    std::size_t m_k;
    std::vector<uint8_t> m_data;

    // Kirsch–Mitzenmacher double hashing: two base hashes give k indices.
    void hashes(const T& item, uint64_t& h1, uint64_t& h2) const {
        h1 = static_cast<uint64_t>(std::hash<T>{}(item));
        // Second independent-ish hash: bit-mix h1 (splitmix64 finalizer).
        uint64_t x = h1 + 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        h2 = x ^ (x >> 31);
        if (h2 == 0) h2 = 1; // ensure the step is nonzero
    }

    std::size_t index(uint64_t h1, uint64_t h2, std::size_t i) const {
        return static_cast<std::size_t>((h1 + i * h2) % m_bits);
    }
};
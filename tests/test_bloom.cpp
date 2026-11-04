// Assert-based tests for the Bloom filter. Build from repo root:
//   g++ -std=c++17 -Wall -Werror tests/test_bloom.cpp -o test_bloom && ./test_bloom
#include "../Bloom Filter/bloom_filter.hpp"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    const std::size_t n = 100000;
    const double p = 0.01;
    BloomFilter<std::string> bf(n, p);
    assert(bf.bits() > n && bf.num_hashes() >= 1);
    for (std::size_t i = 0; i < n; ++i) bf.add("in-" + std::to_string(i));

    // No false negatives, ever.
    for (std::size_t i = 0; i < n; ++i) assert(bf.contains("in-" + std::to_string(i)));

    // False-positive rate near the configured target.
    std::size_t fp = 0, probes = 100000;
    for (std::size_t i = 0; i < probes; ++i) fp += bf.contains("out-" + std::to_string(i));
    const double rate = double(fp) / probes;
    std::printf("bloom: m=%zu k=%zu fp=%.4f (target %.2f)\n", bf.bits(), bf.num_hashes(), rate, p);
    assert(rate < p * 2.0);

    // Empty filter contains nothing.
    BloomFilter<int> empty(10, 0.01);
    for (int i = 0; i < 1000; ++i) assert(!empty.contains(i));
    std::puts("bloom: ok");
}

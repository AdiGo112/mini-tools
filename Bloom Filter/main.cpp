// Demo + assert-based self-check for BloomFilter.
// Build: g++ -std=c++17 -O2 -Wall main.cpp -o bloom && ./bloom
//    or: cl /std:c++17 /EHsc main.cpp

#include "bloom_filter.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
    // ---- Demo ----
    BloomFilter<std::string> bf(100000, 0.01);
    std::cout << "Bloom filter: " << bf.bits() << " bits, "
              << bf.num_hashes() << " hash functions\n\n";

    const std::vector<std::string> fruit = {"apple", "banana", "cherry", "date"};
    for (const auto& f : fruit) bf.add(f);

    std::cout << "Added: apple, banana, cherry, date\n";
    for (const std::string q : {"apple", "cherry", "grape", "mango"})
        std::cout << "  contains(\"" << q << "\") = "
                  << (bf.contains(q) ? "true" : "false") << "\n";
    std::cout << "\n";

    // ---- Self-check (the real test) ----

    // 1. No false negatives: every inserted item must report present. Invariant.
    BloomFilter<int> t(100000, 0.01);
    for (int i = 0; i < 100000; ++i) t.add(i);
    for (int i = 0; i < 100000; ++i) assert(t.contains(i) && "false negative!");

    // 2. False-positive rate sane: query absent items, rate should be near p.
    const int trials = 1000000;
    int fp = 0;
    for (int i = 100000; i < 100000 + trials; ++i)  // all absent
        if (t.contains(i)) ++fp;
    double rate = static_cast<double>(fp) / trials;
    std::cout << "Measured false-positive rate: " << rate
              << " (target 0.01)\n";
    assert(rate < 0.02 && "false-positive rate too high — hashing/sizing broken");

    std::cout << "\nAll checks passed.\n";
    return 0;
}

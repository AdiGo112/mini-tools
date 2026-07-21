#pragma once
// RANDOM — pick a healthy server uniformly at random.
//
// Sounds lazy to the point of negligence, and it nearly is: it keeps no state
// at all, which is exactly why it scales. Round robin needs a shared counter,
// so N load balancer instances either coordinate (slow) or all start at index 0
// and synchronise their bursts onto the same server (worse). Random needs
// nothing shared and still converges to an even split by the law of large
// numbers.
//
// The catch is variance. Over 100 requests to 3 servers you will not see
// 33/33/34 — you will see something like 28/41/31. Fine at high volume, lumpy
// at low volume, and it can still hand a request to a server that is drowning,
// because it never looks at load. Power-of-two-choices (see power_of_two.hpp)
// fixes exactly that for one extra random draw.
//
// Cost: O(n) here because of the healthy scan; O(1) if you keep a live list.

#include "server.hpp"
#include <random>

class RandomChoice {
public:
    // Fixed default seed so the demo/self-test is reproducible. In production
    // seed from std::random_device — otherwise every balancer instance in the
    // fleet makes the identical sequence of "random" choices.
    explicit RandomChoice(uint64_t seed = 0xC0FFEEULL) : m_rng(seed) {}

    std::size_t pick(const Pool& pool) {
        gather_healthy(pool);
        if (m_up.empty()) return NONE;
        std::uniform_int_distribution<std::size_t> d(0, m_up.size() - 1);
        return m_up[d(m_rng)];
    }

protected:
    void gather_healthy(const Pool& pool) {
        m_up.clear();  // reused buffer: capacity survives, so no per-pick allocation
        for (std::size_t i = 0; i < pool.size(); ++i)
            if (pool[i].healthy) m_up.push_back(i);
    }

    std::mt19937_64          m_rng;
    std::vector<std::size_t> m_up;
};

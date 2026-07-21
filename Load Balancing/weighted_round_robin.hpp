#pragma once
// WEIGHTED ROUND ROBIN — round robin where a bigger server gets more turns.
//
// The naive version expands the pool ("A A A A A B C") and cycles it. That hits
// the right ratio but in the worst possible order: A gets five requests
// back-to-back while B and C idle, then A idles while B and C get theirs.
//
// This is the *smooth* variant (nginx's algorithm), which produces the same
// 5:1:1 ratio spread evenly: A B A C A A A. Each pick:
//
//   1. every healthy server does  current += weight
//   2. the server with the highest `current` wins
//   3. the winner pays  current -= total_weight
//
// Step 3 is the whole trick: winning puts you deep in debt, so you can't win
// again until the smaller servers have accumulated their way past you.
//
// Cost: O(n) per pick. Tune by setting Server::weight to relative capacity.

#include "server.hpp"

class WeightedRoundRobin {
public:
    std::size_t pick(const Pool& pool) {
        if (m_current.size() != pool.size()) m_current.assign(pool.size(), 0);

        int total = 0;
        std::size_t best = NONE;
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (!pool[i].healthy || pool[i].weight <= 0) continue;
            m_current[i] += pool[i].weight;
            total        += pool[i].weight;
            if (best == NONE || m_current[i] > m_current[best]) best = i;
        }
        if (best == NONE) return NONE;

        m_current[best] -= total;  // the winner's debt
        return best;
    }

private:
    // Credit counters live here, not on Server — the pool stays a plain data
    // description, and two balancers over the same pool can't corrupt each other.
    std::vector<int> m_current;
};

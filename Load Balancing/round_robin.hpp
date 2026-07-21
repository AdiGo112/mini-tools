#pragma once
// ROUND ROBIN — hand out servers in a fixed cycle: A, B, C, A, B, C, ...
//
// The baseline. Zero knowledge of load, latency, or capacity: it just counts.
// Perfectly fair *by request count*, which is only perfectly fair overall when
// every server is identical AND every request costs the same. Break either
// assumption and it happily overloads a weak or slow box.
//
// Cost: O(1) amortised, one counter. Nothing to tune.

#include "server.hpp"

class RoundRobin {
public:
    std::size_t pick(const Pool& pool) {
        const std::size_t n = pool.size();
        if (n == 0) return NONE;
        if (m_next >= n) m_next = 0;  // pool shrank under us

        // Walk forward at most n slots so a down server costs a skip, not a
        // wasted turn. Bounded loop => always terminates, even if all are down.
        for (std::size_t tries = 0; tries < n; ++tries) {
            const std::size_t i = m_next;
            m_next = (m_next + 1) % n;
            if (pool[i].healthy) return i;
        }
        return NONE;
    }

private:
    std::size_t m_next = 0;
};

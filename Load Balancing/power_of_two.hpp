#pragma once
// POWER OF TWO CHOICES — pick two servers at random, send to the less loaded.
//
// The best value-for-money algorithm on this list. Pure random can hand work to
// a server that is already drowning. Least-connections avoids that but must
// inspect every server on every request, and in a distributed balancer that
// means every instance needs a fresh global view of load — expensive, and the
// view is stale anyway. Stale global load is actively dangerous: every balancer
// reads "server C is idle" at the same moment and stampedes C together.
//
// Two random samples plus one comparison gets you almost all of the benefit.
// The classic result (Mitzenmacher): random assignment leaves a maximum load of
// about log n / log log n above average, while two choices drops that to
// log log n — an exponential improvement, for one extra draw. A third choice
// buys almost nothing on top; two is the sweet spot.
//
// Cost: O(1) decision (O(n) only for the healthy scan). No shared state.

#include "random_choice.hpp"

// Inherits the RNG and the reusable healthy-index buffer from RandomChoice —
// the only difference between the two algorithms is one extra draw.
class PowerOfTwo : public RandomChoice {
public:
    using RandomChoice::RandomChoice;

    std::size_t pick(const Pool& pool) {
        gather_healthy(pool);
        if (m_up.empty()) return NONE;
        if (m_up.size() == 1) return m_up[0];

        std::uniform_int_distribution<std::size_t> da(0, m_up.size() - 1);
        const std::size_t a = da(m_rng);

        // Draw b from the remaining size-1 slots and shift it past a. This
        // guarantees two *distinct* servers in one draw; a reject-and-retry
        // loop would be unbounded in the worst case for no benefit.
        std::uniform_int_distribution<std::size_t> db(0, m_up.size() - 2);
        std::size_t b = db(m_rng);
        if (b >= a) ++b;

        const std::size_t ia = m_up[a], ib = m_up[b];
        return pool[ia].active <= pool[ib].active ? ia : ib;
    }
};

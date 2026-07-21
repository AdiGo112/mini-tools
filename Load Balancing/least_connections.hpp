#pragma once
// LEAST CONNECTIONS — send the request to whoever is currently busiest-free,
// i.e. the server with the fewest in-flight requests.
//
// This is the first algorithm that reacts to *reality* instead of a schedule.
// Round robin sends request #7 to server B regardless of whether B is chewing
// on a 30-second report; least-connections notices B's count is high and routes
// around it. Self-correcting: a slow server drains slowly, so its count stays
// high, so it stops receiving work until it catches up.
//
// Requires the balancer to track completion (active++ on dispatch, active-- on
// finish). If you never decrement, every server looks permanently loaded.
//
// Cost: O(n) per pick.

#include "server.hpp"

class LeastConnections {
public:
    std::size_t pick(const Pool& pool) {
        const std::size_t n = pool.size();
        if (n == 0) return NONE;

        std::size_t best = NONE;
        for (std::size_t t = 0; t < n; ++t) {
            // Rotating scan start: on a cold pool every server has active == 0,
            // and a fixed scan order would dump the whole first burst onto
            // index 0 before any count moves. Rotating spreads the tie.
            const std::size_t i = (m_start + t) % n;
            if (!pool[i].healthy) continue;
            if (best == NONE || pool[i].active < pool[best].active) best = i;
        }
        m_start = (m_start + 1) % n;
        return best;
    }

private:
    std::size_t m_start = 0;
};

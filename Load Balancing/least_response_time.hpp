#pragma once
// LEAST RESPONSE TIME — least connections, but weighted by how fast each server
// actually answers.
//
// Least-connections treats "5 in flight" the same everywhere. But 5 requests on
// a box averaging 10ms will clear in 50ms; 5 on a box averaging 200ms means a
// one-second queue. This scores servers by expected wait instead of raw count:
//
//     score = avg_latency_ms * (active + 1)      -> lowest score wins
//
// The +1 counts the request we are about to send, so an idle server is scored
// on its own latency rather than collapsing to 0 and always winning.
//
// avg_latency_ms is the calibration knob and must be fed by real measurements —
// typically an exponential moving average of recent completions:
//
//     s.avg_latency_ms = 0.8 * s.avg_latency_ms + 0.2 * observed_ms;
//
// A plain lifetime mean goes stale: a server that was fast for an hour looks
// fast for a long time after it starts failing. The EMA forgets.
//
// Cost: O(n) per pick.

#include "server.hpp"

class LeastResponseTime {
public:
    std::size_t pick(const Pool& pool) {
        const std::size_t n = pool.size();
        if (n == 0) return NONE;

        std::size_t best  = NONE;
        double      bestS = 0.0;
        for (std::size_t t = 0; t < n; ++t) {
            const std::size_t i = (m_start + t) % n;  // rotate ties, as in least-connections
            if (!pool[i].healthy) continue;
            const double s = score(pool[i]);
            if (best == NONE || s < bestS) { best = i; bestS = s; }
        }
        m_start = (m_start + 1) % n;
        return best;
    }

    static double score(const Server& s) {
        return s.avg_latency_ms * static_cast<double>(s.active + 1);
    }

private:
    std::size_t m_start = 0;
};

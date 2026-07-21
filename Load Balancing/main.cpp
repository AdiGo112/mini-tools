// Demo + assert-based self-check for every load-balancing algorithm here.
// Build: g++ -std=c++17 -O2 -Wall main.cpp -o lb && ./lb
//    or: cl /std:c++17 /EHsc main.cpp

#include "consistent_hash.hpp"
#include "ip_hash.hpp"
#include "least_connections.hpp"
#include "least_response_time.hpp"
#include "power_of_two.hpp"
#include "random_choice.hpp"
#include "round_robin.hpp"
#include "server.hpp"
#include "weighted_round_robin.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

namespace {

Pool make_pool() {
    return {{"A"}, {"B"}, {"C"}};
}

// Print the sequence of server ids a balancer produces.
template <typename Balancer>
std::string sequence(Balancer& lb, const Pool& pool, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        const std::size_t k = lb.pick(pool);
        out += (k == NONE ? "-" : pool[k].id);
        out += ' ';
    }
    return out;
}

void heading(const char* title) {
    std::cout << "\n=== " << title << " ===\n";
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);

    // ---------------------------------------------------------------- 1. RR
    heading("Round Robin");
    {
        Pool pool = make_pool();
        RoundRobin rr;
        std::cout << "  cycle:        " << sequence(rr, pool, 7) << "\n";

        RoundRobin t;
        for (int i = 0; i < 6; ++i)
            assert(pool[t.pick(pool)].id == std::string(1, char('A' + i % 3)));

        // A down server is skipped, not wasted as a turn.
        pool[1].healthy = false;
        RoundRobin d;
        std::cout << "  B down:       " << sequence(d, pool, 6) << "\n";
        RoundRobin d2;
        for (int i = 0; i < 20; ++i) assert(pool[d2.pick(pool)].id != "B");

        // Everything down => NONE, no infinite loop.
        for (auto& s : pool) s.healthy = false;
        RoundRobin dead;
        assert(dead.pick(pool) == NONE);
    }

    // --------------------------------------------------------------- 2. WRR
    heading("Weighted Round Robin (smooth)");
    {
        Pool pool = make_pool();
        pool[0].weight = 5;  // A is a big box
        pool[1].weight = 1;
        pool[2].weight = 1;

        WeightedRoundRobin wrr;
        std::cout << "  weights 5:1:1 " << sequence(wrr, pool, 14) << "\n";

        // Ratio must match the weights exactly over whole cycles.
        WeightedRoundRobin t;
        std::map<std::string, int> hits;
        for (int i = 0; i < 700; ++i) hits[pool[t.pick(pool)].id]++;
        std::cout << "  700 requests: A=" << hits["A"] << " B=" << hits["B"]
                  << " C=" << hits["C"] << "  (expect 500/100/100)\n";
        assert(hits["A"] == 500 && hits["B"] == 100 && hits["C"] == 100);

        // Smoothness: A must not take all 5 of its turns back to back.
        WeightedRoundRobin s;
        int run = 0, longest = 0;
        for (int i = 0; i < 700; ++i) {
            run = (pool[s.pick(pool)].id == "A") ? run + 1 : 0;
            if (run > longest) longest = run;
        }
        std::cout << "  longest A run: " << longest << " (naive WRR would be 5)\n";
        assert(longest < 5 && "not smooth — this is plain expanded WRR");
    }

    // ---------------------------------------------------------------- 3. LC
    heading("Least Connections");
    {
        Pool pool = make_pool();
        pool[0].active = 7;
        pool[1].active = 2;
        pool[2].active = 5;

        LeastConnections lc;
        std::cout << "  load 7/2/5 -> " << pool[lc.pick(pool)].id << "\n";
        assert(pool[lc.pick(pool)].id == "B");

        // Simulate real dispatch: the winner's count goes up, so the choice moves.
        LeastConnections sim;
        Pool p = make_pool();
        for (int i = 0; i < 9; ++i) p[sim.pick(p)].active++;
        std::cout << "  9 dispatches: A=" << p[0].active << " B=" << p[1].active
                  << " C=" << p[2].active << "\n";
        assert(p[0].active == 3 && p[1].active == 3 && p[2].active == 3);

        // The busiest server is never chosen while a lighter one is up.
        pool[1].healthy = false;  // fewest connections, but down
        assert(pool[lc.pick(pool)].id == "C");
    }

    // --------------------------------------------------------------- 4. LRT
    heading("Least Response Time");
    {
        Pool pool = make_pool();
        pool[0].active = 0; pool[0].avg_latency_ms = 200.0;  // idle but slow
        pool[1].active = 3; pool[1].avg_latency_ms = 10.0;   // busy but fast
        pool[2].active = 1; pool[2].avg_latency_ms = 90.0;

        for (const auto& s : pool)
            std::cout << "  " << s.id << ": active=" << s.active << " latency="
                      << s.avg_latency_ms << "ms -> score "
                      << LeastResponseTime::score(s) << "\n";

        LeastResponseTime lrt;
        std::cout << "  picks: " << pool[lrt.pick(pool)].id
                  << "  (least-connections would pick A)\n";
        // B: 10*4 = 40 beats A: 200*1 = 200 and C: 90*2 = 180.
        assert(pool[lrt.pick(pool)].id == "B");
    }

    // ------------------------------------------------------------- 5. Random
    heading("Random");
    {
        Pool pool = make_pool();
        RandomChoice rnd;
        std::cout << "  sample:       " << sequence(rnd, pool, 12) << "\n";

        std::map<std::string, int> hits;
        for (int i = 0; i < 30000; ++i) hits[pool[rnd.pick(pool)].id]++;
        std::cout << "  30000 reqs:   A=" << hits["A"] << " B=" << hits["B"]
                  << " C=" << hits["C"] << "  (expect ~10000 each)\n";
        for (const auto& s : pool)
            assert(hits[s.id] > 9000 && hits[s.id] < 11000 && "distribution skewed");
    }

    // -------------------------------------------------------- 6. P2C vs rand
    heading("Power of Two Choices");
    {
        // Balls into bins — the exact setting the theorem is about. Throw 10000
        // requests at 10 servers, never complete any, and measure how far the
        // WORST server ends up above the 1000-request average. That overshoot is
        // the queue a real user waits behind.
        auto imbalance = [](auto& lb, int requests, std::size_t servers) {
            Pool pool;
            for (std::size_t i = 0; i < servers; ++i)
                pool.push_back({std::string(1, char('A' + i))});
            for (int i = 0; i < requests; ++i) pool[lb.pick(pool)].active++;

            std::size_t max = 0;
            for (const auto& s : pool) max = std::max(max, s.active);
            return max - static_cast<std::size_t>(requests) / servers;
        };

        RandomChoice rnd;
        PowerOfTwo   p2c;
        const std::size_t over_rnd = imbalance(rnd, 10000, 10);
        const std::size_t over_p2c = imbalance(p2c, 10000, 10);

        std::cout << "  10000 requests over 10 servers (average load 1000):\n";
        std::cout << "    random:        busiest server is +" << over_rnd
                  << " over average\n";
        std::cout << "    power of two:  busiest server is +" << over_p2c
                  << " over average\n";
        assert(over_p2c < over_rnd && "p2c should flatten the peak");

        // Given a lopsided pool, p2c must never choose the heaviest server:
        // it only ever sees two candidates, so the max can only win if drawn
        // against itself — impossible, the two draws are distinct.
        Pool skew = make_pool();
        skew[0].active = 100; skew[1].active = 1; skew[2].active = 2;
        PowerOfTwo p;
        int chose_worst = 0;
        for (int i = 0; i < 5000; ++i)
            if (p.pick(skew) == 0) ++chose_worst;
        std::cout << "  heaviest server chosen " << chose_worst << "/5000 times\n";
        assert(chose_worst == 0);
    }

    // ------------------------------------------------------------ 7. IP hash
    heading("IP Hash (modulo)");
    {
        Pool pool = {{"A"}, {"B"}, {"C"}, {"D"}};
        const std::vector<std::string> clients = {"10.0.0.1", "10.0.0.2",
                                                  "10.0.0.3", "10.0.0.4"};
        for (const auto& c : clients)
            std::cout << "  " << c << " -> " << pool[ip_hash_pick(pool, c)].id << "\n";

        // Stickiness: same key, same server, forever.
        for (int i = 0; i < 100; ++i)
            assert(ip_hash_pick(pool, "10.0.0.1") == ip_hash_pick(pool, "10.0.0.1"));

        // The remap storm. Record 10000 key->server mappings, add one server,
        // and count how many keys changed owner.
        const int KEYS = 10000;
        std::vector<std::string> before(KEYS);
        for (int i = 0; i < KEYS; ++i) {
            const std::string k = "key" + std::to_string(i);
            before[i] = pool[ip_hash_pick(pool, k)].id;
        }
        pool.push_back({"E"});  // scale 4 -> 5
        int moved = 0;
        for (int i = 0; i < KEYS; ++i)
            if (pool[ip_hash_pick(pool, "key" + std::to_string(i))].id != before[i])
                ++moved;

        const double frac = static_cast<double>(moved) / KEYS;
        std::cout << "  add 1 server (4->5): " << 100.0 * frac
                  << "% of keys remapped\n";
        assert(frac > 0.5 && "modulo hashing is supposed to be this bad");
    }

    // ---------------------------------------------------- 8. Consistent hash
    heading("Consistent Hash (ring + virtual nodes)");
    {
        Pool pool = {{"A"}, {"B"}, {"C"}, {"D"}};
        ConsistentHash ch(pool, 150);
        std::cout << "  ring: " << ch.ring_size() << " points ("
                  << pool.size() << " servers x 150 vnodes)\n";

        const int KEYS = 10000;

        // Distribution: virtual nodes should keep every share near 1/N.
        std::map<std::string, int> hits;
        std::vector<std::string> before(KEYS);
        for (int i = 0; i < KEYS; ++i) {
            const std::string k = "key" + std::to_string(i);
            before[i] = pool[ch.pick(pool, k)].id;
            hits[before[i]]++;
        }
        std::cout << "  spread:";
        for (const auto& s : pool) std::cout << " " << s.id << "=" << hits[s.id];
        std::cout << "  (expect ~2500 each)\n";
        for (const auto& s : pool)
            assert(hits[s.id] > 1500 && hits[s.id] < 3500 && "vnodes not smoothing");

        // The payoff: same 4->5 scale-out as the modulo test above.
        pool.push_back({"E"});
        ch.rebuild(pool, 150);
        int moved = 0;
        for (int i = 0; i < KEYS; ++i)
            if (pool[ch.pick(pool, "key" + std::to_string(i))].id != before[i])
                ++moved;

        const double frac = static_cast<double>(moved) / KEYS;
        std::cout << "  add 1 server (4->5): " << 100.0 * frac
                  << "% of keys remapped  (theory: ~1/5 = 20%)\n";
        assert(frac < 0.35 && "consistent hashing failed its whole reason to exist");

        // Stickiness holds, and a down server's keys spill to a neighbour
        // WITHOUT disturbing anyone else's mapping.
        std::vector<std::string> healthy_map(KEYS);
        for (int i = 0; i < KEYS; ++i)
            healthy_map[i] = pool[ch.pick(pool, "key" + std::to_string(i))].id;

        pool[0].healthy = false;  // A goes down; no rebuild
        int disturbed = 0;
        for (int i = 0; i < KEYS; ++i) {
            const std::string now = pool[ch.pick(pool, "key" + std::to_string(i))].id;
            assert(now != "A");
            if (now != healthy_map[i]) ++disturbed;
        }
        std::cout << "  A marked down:       " << 100.0 * disturbed / KEYS
                  << "% of keys moved  (only A's share)\n";
        assert(disturbed > 0 && disturbed < KEYS / 2);

        // ...and when A comes back, every one of its keys returns to it.
        pool[0].healthy = true;
        for (int i = 0; i < KEYS; ++i)
            assert(pool[ch.pick(pool, "key" + std::to_string(i))].id == healthy_map[i]);
        std::cout << "  A recovers:          all keys back on their original server\n";
    }

    std::cout << "\nAll checks passed.\n";
    return 0;
}

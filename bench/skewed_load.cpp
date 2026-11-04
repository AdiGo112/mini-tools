// Simulated skewed-load benchmark for all 8 strategies (no networking).
//   g++ -std=c++17 -O2 -Wall bench/skewed_load.cpp -o skewed_load && ./skewed_load
//
// Model: 4 heterogeneous backends, each a FIFO queue with `slots` parallel
// workers and exponential service time (mean = base_ms). Poisson arrivals at
// ~70% of total capacity; client keys are Zipf(s=1.1) over 10k clients and
// feed the two hash strategies. Latency = queue wait + service. Fixed seed.
#include "../Load Balancing/consistent_hash.hpp"
#include "../Load Balancing/ip_hash.hpp"
#include "../Load Balancing/least_connections.hpp"
#include "../Load Balancing/least_response_time.hpp"
#include "../Load Balancing/power_of_two.hpp"
#include "../Load Balancing/random_choice.hpp"
#include "../Load Balancing/round_robin.hpp"
#include "../Load Balancing/weighted_round_robin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <queue>
#include <random>

struct Backend { int slots; double base_ms; };
static const std::vector<Backend> BACKENDS = {{8, 10}, {4, 20}, {2, 20}, {1, 40}};
static const int REQUESTS = 200000, CLIENTS = 10000;
static const uint64_t SEED = 12345;

struct Result { std::vector<int> count; double p50, p99; };

// pick(pool, key) -> backend index
static Result run(const std::function<std::size_t(const Pool&, const std::string&)>& pick) {
    std::mt19937_64 rng(SEED);
    Pool pool;
    double cap = 0;
    for (std::size_t i = 0; i < BACKENDS.size(); ++i) {
        Server s; s.id = "srv" + std::to_string(i); s.weight = int(BACKENDS[i].slots * 40 / BACKENDS[i].base_ms);  // capacity ratio 32:8:4:1
        s.avg_latency_ms = BACKENDS[i].base_ms;
        pool.push_back(s);
        cap += BACKENDS[i].slots / BACKENDS[i].base_ms;
    }
    // Zipf CDF over client ids.
    std::vector<double> cdf(CLIENTS);
    double z = 0;
    for (int i = 0; i < CLIENTS; ++i) cdf[i] = (z += 1.0 / std::pow(i + 1, 1.1));
    std::uniform_real_distribution<double> u(0, z);
    std::exponential_distribution<double> arrive(0.7 * cap);

    // Per backend: min-heap of worker free times. Completion events release `active`.
    std::vector<std::priority_queue<double, std::vector<double>, std::greater<>>> free(BACKENDS.size());
    for (std::size_t i = 0; i < BACKENDS.size(); ++i) for (int k = 0; k < BACKENDS[i].slots; ++k) free[i].push(0);
    using Ev = std::pair<double, std::size_t>;
    std::priority_queue<Ev, std::vector<Ev>, std::greater<>> done;

    Result r{std::vector<int>(BACKENDS.size()), 0, 0};
    std::vector<double> lat; lat.reserve(REQUESTS);
    double t = 0;
    for (int n = 0; n < REQUESTS; ++n) {
        t += arrive(rng);
        while (!done.empty() && done.top().first <= t) { --pool[done.top().second].active; done.pop(); }
        const int client = int(std::lower_bound(cdf.begin(), cdf.end(), u(rng)) - cdf.begin());
        const std::size_t b = pick(pool, "client-" + std::to_string(client));
        std::exponential_distribution<double> svc(1.0 / BACKENDS[b].base_ms);
        const double start = std::max(t, free[b].top()); free[b].pop();
        const double end = start + svc(rng);
        free[b].push(end);
        ++pool[b].active; done.push({end, b});
        pool[b].avg_latency_ms = 0.8 * pool[b].avg_latency_ms + 0.2 * (end - t);  // EMA, as the header suggests
        lat.push_back(end - t);
        ++r.count[b];
    }
    std::sort(lat.begin(), lat.end());
    r.p50 = lat[lat.size() / 2]; r.p99 = lat[lat.size() * 99 / 100];
    return r;
}

int main() {
    RoundRobin rr; WeightedRoundRobin wrr; LeastConnections lc; LeastResponseTime lrt;
    RandomChoice rnd(SEED); PowerOfTwo p2(SEED);
    Pool ring_pool; for (std::size_t i = 0; i < BACKENDS.size(); ++i) { Server s; s.id = "srv" + std::to_string(i); ring_pool.push_back(s); }
    ConsistentHash ch(ring_pool);

    const std::vector<std::pair<const char*, std::function<std::size_t(const Pool&, const std::string&)>>> strategies = {
        {"round_robin",          [&](const Pool& p, const std::string&) { return rr.pick(p); }},
        {"weighted_round_robin", [&](const Pool& p, const std::string&) { return wrr.pick(p); }},
        {"least_connections",    [&](const Pool& p, const std::string&) { return lc.pick(p); }},
        {"least_response_time",  [&](const Pool& p, const std::string&) { return lrt.pick(p); }},
        {"random",               [&](const Pool& p, const std::string&) { return rnd.pick(p); }},
        {"power_of_two",         [&](const Pool& p, const std::string&) { return p2.pick(p); }},
        {"ip_hash",              [&](const Pool& p, const std::string& k) { return ip_hash_pick(p, k); }},
        {"consistent_hash",      [&](const Pool& p, const std::string& k) { return ch.pick(p, k); }},
    };

    std::printf("backends (slots/mean ms): 8/10 4/20 2/20 1/40 | %d requests, Zipf s=1.1 over %d clients, 70%% load, seed %llu\n\n",
                REQUESTS, CLIENTS, (unsigned long long)SEED);
    std::printf("| strategy | share per backend (%%) | max/min share | util spread (max/min) | p50 ms | p99 ms |\n");
    std::printf("|---|---|---|---|---|---|\n");
    for (auto& [name, pick] : strategies) {
        Result r = run(pick);
        std::string shares;
        double mx = 0, mn = 1e18, umx = 0, umn = 1e18;
        for (std::size_t i = 0; i < r.count.size(); ++i) {
            const double s = 100.0 * r.count[i] / REQUESTS;
            char buf[16]; std::snprintf(buf, sizeof buf, "%s%.1f", i ? " / " : "", s); shares += buf;
            mx = std::max(mx, s); mn = std::min(mn, s);
            const double util = r.count[i] * BACKENDS[i].base_ms / BACKENDS[i].slots;  // offered work / capacity
            umx = std::max(umx, util); umn = std::min(umn, util);
        }
        std::printf("| %s | %s | %.1f | %.2f | %.1f | %.1f |\n", name, shares.c_str(), mx / std::max(mn, 1e-9), umx / std::max(umn, 1e-9), r.p50, r.p99);
    }
}

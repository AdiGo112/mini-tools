// Assert-based tests for all 8 load-balancing strategies. Build from repo root:
//   g++ -std=c++17 -Wall -Werror tests/test_lb.cpp -o test_lb && ./test_lb
#include "../Load Balancing/consistent_hash.hpp"
#include "../Load Balancing/ip_hash.hpp"
#include "../Load Balancing/least_connections.hpp"
#include "../Load Balancing/least_response_time.hpp"
#include "../Load Balancing/power_of_two.hpp"
#include "../Load Balancing/random_choice.hpp"
#include "../Load Balancing/round_robin.hpp"
#include "../Load Balancing/weighted_round_robin.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static Pool abc() { return {{"A"}, {"B"}, {"C"}}; }

static void round_robin() {
    Pool p = abc();
    RoundRobin rr;
    for (int i = 0; i < 9; ++i) assert(rr.pick(p) == std::size_t(i % 3));
    p[1].healthy = false;
    std::vector<std::size_t> got;
    for (int i = 0; i < 4; ++i) got.push_back(rr.pick(p));
    for (auto g : got) assert(g != 1);
    for (auto& s : p) s.healthy = false;
    assert(rr.pick(p) == NONE);
    assert(RoundRobin{}.pick(Pool{}) == NONE);
}

static void weighted() {
    Pool p = abc();
    p[0].weight = 5;
    WeightedRoundRobin w;
    std::string seq;
    for (int i = 0; i < 7; ++i) seq += p[w.pick(p)].id;
    assert(seq == "AABACAA");  // smooth: 5:1:1, A never 5x in a row
    int c[3] = {};
    for (int i = 0; i < 7000; ++i) ++c[w.pick(p)];
    assert(c[0] == 5000 && c[1] == 1000 && c[2] == 1000);
    p[0].healthy = false;
    for (int i = 0; i < 10; ++i) assert(w.pick(p) != 0);
}

static void least_connections() {
    Pool p = abc();
    p[0].active = 5; p[1].active = 2; p[2].active = 9;
    LeastConnections lc;
    for (int i = 0; i < 5; ++i) assert(lc.pick(p) == 1);
    p[1].healthy = false;
    assert(lc.pick(p) == 0);
    // Cold pool: ties rotate rather than piling on index 0.
    Pool cold = abc();
    LeastConnections lc2;
    assert(lc2.pick(cold) == 0 && lc2.pick(cold) == 1 && lc2.pick(cold) == 2);
}

static void least_response_time() {
    Pool p = abc();
    p[0].avg_latency_ms = 10;  p[0].active = 5;  // score 60
    p[1].avg_latency_ms = 200; p[1].active = 0;  // score 200
    p[2].avg_latency_ms = 20;  p[2].active = 1;  // score 40
    LeastResponseTime lrt;
    assert(lrt.pick(p) == 2);
    p[2].healthy = false;
    assert(lrt.pick(p) == 0);
}

static void random_choice() {
    Pool p = abc();
    RandomChoice a(42), b(42);
    int c[3] = {};
    for (int i = 0; i < 30000; ++i) {
        std::size_t x = a.pick(p);
        assert(x == b.pick(p));  // same seed, same sequence
        ++c[x];
    }
    for (int v : c) assert(v > 9000 && v < 11000);
    p[0].healthy = p[1].healthy = false;
    for (int i = 0; i < 10; ++i) assert(a.pick(p) == 2);
}

static void power_of_two() {
    Pool p = abc();
    p[0].active = 100; p[1].active = 0; p[2].active = 0;
    PowerOfTwo pt(7);
    // Any pair includes B or C, both lighter than A: A is never chosen.
    for (int i = 0; i < 1000; ++i) assert(pt.pick(p) != 0);
    Pool one = abc(); one[0].healthy = one[2].healthy = false;
    assert(pt.pick(one) == 1);
}

static void ip_hash() {
    Pool p = abc();
    for (int i = 0; i < 1000; ++i) {
        std::string k = "10.0.0." + std::to_string(i);
        assert(ip_hash_pick(p, k) == ip_hash_pick(p, k));
    }
    // Down server: its keys move, everything else stays.
    std::size_t k0 = NONE; std::string key;
    for (int i = 0; k0 != 0; ++i) { key = "k" + std::to_string(i); k0 = ip_hash_pick(p, key); }
    p[0].healthy = false;
    assert(ip_hash_pick(p, key) == 1);
    // Growing the pool remaps most keys (the documented trap).
    Pool p4 = {{"A"}, {"B"}, {"C"}, {"D"}}, p5 = p4; p5.push_back({"E"});
    int moved = 0;
    for (int i = 0; i < 10000; ++i) { std::string k = "u" + std::to_string(i); moved += ip_hash_pick(p4, k) != ip_hash_pick(p5, k); }
    assert(moved > 7000);
}

static void consistent_hash() {
    Pool p = {{"A"}, {"B"}, {"C"}, {"D"}};
    ConsistentHash ch(p);
    assert(ch.ring_size() == 4 * 150);
    const int N = 20000;
    std::vector<std::size_t> before(N);
    int c[5] = {};
    for (int i = 0; i < N; ++i) { before[i] = ch.pick(p, "u" + std::to_string(i)); ++c[before[i]]; assert(before[i] == ch.pick(p, "u" + std::to_string(i))); }
    for (int i = 0; i < 4; ++i) assert(c[i] > N / 4 * 0.75 && c[i] < N / 4 * 1.25);  // vnodes keep it flat

    p.push_back({"E"});
    ch.rebuild(p);
    int moved = 0;
    for (int i = 0; i < N; ++i) {
        std::size_t now = ch.pick(p, "u" + std::to_string(i));
        if (now != before[i]) { ++moved; assert(now == 4); }  // keys only move TO the newcomer
    }
    const double frac = double(moved) / N;  // expect ~1/5
    std::printf("consistent hash: %.3f of keys moved on 4->5\n", frac);
    assert(frac > 0.12 && frac < 0.28);

    // Down server: keys walk to a healthy neighbour, others unaffected.
    p[0].healthy = false;
    for (int i = 0; i < 1000; ++i) assert(ch.pick(p, "u" + std::to_string(i)) != 0);
}

int main() {
    round_robin(); weighted(); least_connections(); least_response_time();
    random_choice(); power_of_two(); ip_hash(); consistent_hash();
    std::puts("load balancing: ok");
}

#pragma once
// Shared vocabulary for every load-balancing algorithm in this folder.
// A Pool is just a vector of backends; every algorithm's pick() returns an
// index into that Pool (or NONE when nothing is up). C++17, stdlib only.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One backend server. Each algorithm reads a different field:
//   weight          -> weighted round robin
//   active          -> least connections, power of two choices
//   avg_latency_ms  -> least response time
//   id              -> ip hash, consistent hash
//   healthy         -> all of them (flipped by the health checker)
struct Server {
    std::string id;
    int         weight         = 1;    // relative capacity; bigger box, bigger number
    std::size_t active         = 0;    // in-flight requests right now
    double      avg_latency_ms = 1.0;  // observed average response time
    bool        healthy        = true; // false => never routed to
};

using Pool = std::vector<Server>;

// "No server available" — every pool member is down (or the pool is empty).
inline constexpr std::size_t NONE = static_cast<std::size_t>(-1);

// splitmix64 finalizer. std::hash quality varies wildly between standard
// libraries (MSVC ships FNV-1a, which clusters); running the result through a
// bit mixer gives the uniform spread that modulo and ring placement need.
inline uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

inline uint64_t hash_key(const std::string& key) {
    return mix64(static_cast<uint64_t>(std::hash<std::string>{}(key)));
}

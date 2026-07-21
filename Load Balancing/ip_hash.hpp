#pragma once
// IP HASH (modulo hashing) — route by a key instead of by turn:
//
//     index = hash(key) % server_count
//
// The key is usually the client IP, but it can be a session id, a user id, or a
// URL. The point is *stickiness*: the same key deterministically lands on the
// same server every time, with no shared session table and no cookie. That
// makes in-process caches and in-memory sessions actually work.
//
// THE TRAP — and it is the reason consistent_hash.hpp exists:
//
//   The server count is baked into the answer. Go from 4 servers to 5 and the
//   modulus changes, so essentially *every* key remaps at once:
//
//     hash=13:  13 % 4 = 1   ->   13 % 5 = 3    moved
//     hash=27:  27 % 4 = 3   ->   27 % 5 = 2    moved
//     hash=40:  40 % 4 = 0   ->   40 % 5 = 0    stayed (lucky)
//
//   Roughly 1 - 1/N of all keys move. Every warm cache is instantly wrong,
//   every sticky session is on the wrong box, and the origin database eats the
//   entire miss storm at once. A single autoscale event or one crashed node
//   triggers it. The self-test in main.cpp measures this.
//
// No state at all, so this is a free function rather than a class.

#include "server.hpp"

inline std::size_t ip_hash_pick(const Pool& pool, const std::string& key) {
    const std::size_t n = pool.size();
    if (n == 0) return NONE;

    const std::size_t start = static_cast<std::size_t>(hash_key(key) % n);

    // Linear probe forward past down servers. Note this only *masks* an
    // outage — it does not fix the remap problem above, because the modulus
    // itself is unchanged. Removing the server from the pool would change n and
    // reshuffle everything.
    for (std::size_t t = 0; t < n; ++t) {
        const std::size_t i = (start + t) % n;
        if (pool[i].healthy) return i;
    }
    return NONE;
}

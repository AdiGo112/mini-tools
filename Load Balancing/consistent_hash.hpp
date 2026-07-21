#pragma once
// CONSISTENT HASHING — sticky routing that survives adding and removing servers.
//
// The fix for modulo hashing's remap storm (see ip_hash.hpp). Instead of
// hashing keys into "one of N slots", hash BOTH servers and keys onto the same
// circular 64-bit space, and let each key belong to the first server it meets
// walking clockwise:
//
//        0 ────────────────────────────────────────── 2^64
//        │      A            B         C        A       │
//        │      ^            ^         ^        ^       │
//        │      │            │         │        │       │
//        │   key k1 ─────────┘      key k2 ─────┘       │
//        │   (walks clockwise to B)  (walks to A)       │
//
// Now remove B. Only the keys that sat between A and B move — they slide on to
// C. Every other key on the ring is untouched, because nobody's *neighbour*
// changed. Adding a server is the mirror image: the newcomer steals one arc
// from one successor and nothing else moves.
//
//     modulo hashing:     ~(1 - 1/N) of keys remap   (nearly all of them)
//     consistent hashing: ~1/N of keys remap         (only the departed share)
//
// VIRTUAL NODES — the part people skip and then wonder why one box is on fire.
//
// With one ring position per server, three random points on a circle carve it
// into wildly uneven arcs; a 50/30/20 split is completely normal, and removing
// a server dumps its whole arc onto exactly one successor. The fix is to place
// each server at many positions (hash "A#0", "A#1", ... "A#149"). Averaging 150
// random arcs smooths the load to within a few percent, and when a server
// leaves, its 150 arcs are inherited by 150 different neighbours instead of one.
//
// vnodes is the calibration knob: more vnodes = flatter distribution, larger
// ring, slower lookup. 100-200 is the standard range.
//
// Cost: O(log(n * vnodes)) lookup — std::map is already a sorted ring, and
// lower_bound IS the clockwise walk. Building the ring is O(n * vnodes * log).

#include "server.hpp"
#include <map>

class ConsistentHash {
public:
    explicit ConsistentHash(const Pool& pool, std::size_t vnodes = 150) {
        rebuild(pool, vnodes);
    }

    // Call this whenever pool *membership* changes (server added or removed).
    // Health flips do NOT need a rebuild — pick() walks past down servers, which
    // keeps their keys' normal owner intact for when they come back.
    void rebuild(const Pool& pool, std::size_t vnodes = 150) {
        if (vnodes == 0) vnodes = 1;
        m_ring.clear();
        for (std::size_t i = 0; i < pool.size(); ++i)
            for (std::size_t v = 0; v < vnodes; ++v)
                m_ring[vnode_hash(pool[i].id, v)] = i;
    }

    std::size_t pick(const Pool& pool, const std::string& key) const {
        if (m_ring.empty()) return NONE;

        // lower_bound = first ring position >= hash(key), i.e. the clockwise
        // successor. end() means we ran off the top of the circle, so wrap to
        // the first entry — that wrap is the only thing making this a ring.
        auto it = m_ring.lower_bound(hash_key(key));
        if (it == m_ring.end()) it = m_ring.begin();

        for (std::size_t t = 0; t < m_ring.size(); ++t) {
            if (pool[it->second].healthy) return it->second;
            if (++it == m_ring.end()) it = m_ring.begin();  // keep walking clockwise
        }
        return NONE;
    }

    std::size_t ring_size() const { return m_ring.size(); }

private:
    // Sorted map == the ring. ponytail: a sorted vector + std::lower_bound would
    // be faster to scan and cheaper on cache; swap it in if lookups ever profile
    // hot. std::map wins here because rebuild() stays three lines.
    std::map<uint64_t, std::size_t> m_ring;  // ring position -> index into Pool

    static uint64_t vnode_hash(const std::string& id, std::size_t v) {
        return hash_key(id + "#" + std::to_string(v));
    }
};

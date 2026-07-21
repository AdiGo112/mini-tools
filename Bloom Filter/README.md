# Bloom Filter (C++)

A tiny, header-only **Bloom filter** in C++17 — a space-efficient probabilistic
data structure that answers one question fast: *"Have I seen this item before?"*

It can be **wrong in only one direction**:

- Says **"No"** → the item is *definitely* not in the set. Always correct.
- Says **"Yes"** → the item is *probably* in the set. Might be a false alarm.

You trade a little accuracy for enormous savings in memory and speed.

---

## The Idea in One Picture

A Bloom filter is just a big array of bits (all `0` to start) plus a handful of
hash functions. It **never stores the actual items** — only fingerprints of them.

```
Add "apple":   hash it to k positions, set those bits to 1

  index:   0   1   2   3   4   5   6   7   8   9  ...
  bits:  [ 0 | 1 | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 0 ]
               ^           ^           ^
              apple's 3 bit positions now set

Check "apple":  hash to the same positions → all 1? → "probably yes"
Check "mango":  hash to its positions      → any 0? → "definitely no"
```

Because bits are only ever turned **on**, an added item's bits are always still
set later — so a real member is never missed. A false positive happens only when
some *other* items happen to have already set all of a stranger's bits.

---

## How It Works

1. **Bit array of size `m`** — starts all zeros.
2. **`k` hash functions** — each item maps to `k` positions in the array.
3. **`add(x)`** — set all `k` of `x`'s bits to `1`.
4. **`contains(x)`** — return `true` only if *all* `k` of `x`'s bits are `1`.

### Sizing it correctly

Given the number of items you expect (`n`) and the false-positive rate you can
tolerate (`p`), the optimal array size and hash count come from standard formulas:

```
m = ceil( -(n · ln p) / (ln 2)² )     bits
k = round( (m / n) · ln 2 )           hash functions
```

These are baked into the constructor — you just say *how many items* and *how
accurate*, and it figures out the rest.

### Getting `k` hashes from one

Rather than implement `k` separate hash algorithms, this uses the
**Kirsch–Mitzenmacher double-hashing** trick: compute two base hashes and combine
them to produce all `k` positions:

```
position_i = (h1 + i · h2) mod m,   for i = 0 … k-1
```

`h1` comes from `std::hash<T>`; `h2` is `h1` run through a bit-mixer so the two
are effectively independent. Cheap and statistically solid.

---

## Usage

```cpp
#include "bloom_filter.hpp"

BloomFilter<std::string> bf(1000, 0.01);   // ~1000 items, 1% false-positive target

bf.add("apple");
bf.add("banana");

bf.contains("apple");   // true
bf.contains("grape");   // almost always false (small chance of false positive)

bf.bits();              // number of bits in the array
bf.num_hashes();        // number of hash functions (k)
```

Works with any type `T` that `std::hash` supports (`int`, `std::string`, etc.).

### Build & run the demo/tests

```bash
g++ -std=c++17 -O2 -Wall main.cpp -o bloom
# MSVC:  cl /std:c++17 /EHsc main.cpp
./bloom
# main.exe
```

`main.cpp` is both a demo and a self-test. It:

1. Inserts 100,000 items and asserts **every one** is reported present
   (proves *no false negatives*).
2. Queries 1,000,000 never-added items and checks the measured false-positive
   rate stays near the 0.01 target (proves the math and hashing are correct).

Passing output ends with `All checks passed.` and exit code `0`, so it doubles
as a CI gate.

### Example output

```text
Bloom filter: 958506 bits, 7 hash functions

Added: apple, banana, cherry, date
  contains("apple") = true
  contains("cherry") = true
  contains("grape") = false
  contains("mango") = false

Measured false-positive rate: 0.010894 (target 0.01)

All checks passed.
```

Notice the filter needs only **958,506 bits (~117 KB)** to track 100,000 items,
`apple`/`cherry` (added) report present while `grape`/`mango` (not added) report
absent, and the measured false-positive rate (`0.0109`) lands right on the
`0.01` target — the proof that the sizing math and hashing are correct.

---

## Advantages

- **Tiny memory footprint.** Stores bits, not items. A filter for millions of
  entries fits in a few megabytes — often 8–10× smaller than a hash set.
- **Constant-time operations.** `add` and `contains` are O(k), independent of how
  many items are stored.
- **No false negatives.** A "not present" answer is always trustworthy.
- **Privacy-friendly.** It keeps fingerprints, not the original data, so the raw
  items can't be read back out.
- **Tunable.** You dial the false-positive rate up or down by trading memory.

## Disadvantages

- **False positives are possible.** "Present" means *probably*, not *certainly*.
- **No deletion.** You can't remove an item — clearing its bits could break other
  items that share those bits. (A *counting Bloom filter* fixes this at extra cost.)
- **Can't list contents.** There's no way to enumerate what's inside; you can only
  test membership of a specific item.
- **Fills up.** Add far more than the planned `n` and the false-positive rate
  climbs toward 100%. Size for your real workload.
- **No value storage.** It's a set, not a map — membership only, no associated data.

---

## When to Use It

The classic pattern: put a Bloom filter **in front of an expensive lookup** to
skip work that would come back empty.

```
                    ┌─────────────────┐   "no"
   query ──────────▶│  Bloom filter   │──────────▶ done, skip the slow check
                    └────────┬────────┘
                             │ "maybe"
                             ▼
                    ┌─────────────────┐
                    │ real lookup     │  (DB / disk / network — the slow path)
                    │ (authoritative) │
                    └─────────────────┘
```

Since "no" is always correct, every "no" is a slow lookup you safely avoid. Only
the rare false-positive "maybe" costs an unnecessary real lookup.

---

## Real-World Applications

- **Databases (Cassandra, HBase, RocksDB, LevelDB).** Before hitting disk to find
  a key, a Bloom filter says whether it *could* be there — avoiding countless
  pointless disk reads.
- **Web caches / CDNs.** "Cache one-hit-wonders": only cache a URL the *second*
  time it's requested; the filter cheaply remembers first sightings.
- **Browsers (Google Chrome, historically).** Checking a URL against a huge
  malicious-site list — the filter screens locally; only a "maybe" triggers a
  full server check.
- **Spell checkers & dictionaries.** Fast "is this a known word?" without holding
  the whole word list in memory.
- **Networking & security.** Detecting duplicate packets, seen IPs, or already-
  processed events in a stream.
- **Recommendation systems.** "Has this user already seen this post/ad?" to avoid
  showing repeats.
- **Distributed systems.** Deduplicating messages and reducing chatter between
  nodes.

---

## Worked Example

Say you run a signup form and want to reject usernames already taken, but the
user table lives in a slow database.

```cpp
BloomFilter<std::string> taken(1'000'000, 0.001);   // 1M users, 0.1% FP
for (const auto& name : all_existing_usernames)      // load once at startup
    taken.add(name);

// On each signup attempt:
if (!taken.contains(requested)) {
    // Definitely free — accept instantly, no DB query needed.
} else {
    // Probably taken — do the ONE real DB check to be sure.
}
```

Almost every *available* name is confirmed without touching the database. Only
names that are actually taken (plus ~0.1% false alarms) pay for a real query.

---

## Files

| File               | Purpose                                         |
|--------------------|-------------------------------------------------|
| `bloom_filter.hpp` | Header-only `BloomFilter<T>` implementation.    |
| `main.cpp`         | Demo + assert-based self-test (no framework).   |

## Possible Extensions

- **Bit-packing** — store 8 bits per byte instead of 1, for 8× less memory.
- **Counting Bloom filter** — use small counters instead of bits to support
  deletion.
- **Scalable / Cuckoo filter** — grow automatically, or allow deletion with
  better space efficiency.
- **Serialization** — save/load the filter to disk for reuse across runs.

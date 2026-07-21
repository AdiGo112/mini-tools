# Load Balancing Algorithms (C++)

Eight load-balancing algorithms, each in its own header-only C++17 file, with a
single `main.cpp` that demos and self-tests all of them. No dependencies beyond
the standard library.

A load balancer sits between clients and a pool of servers and answers one
question per request: **which server handles this?** That single decision is the
whole subject. Everything below is a different answer to it.

```
                          ┌──────────────┐
   client ───────────────▶│     load     │───▶ server A   (healthy)
   client ───────────────▶│   balancer   │───▶ server B   (healthy)
   client ───────────────▶│              │──✗  server C   (down, health check failed)
                          └──────────────┘
                            picks one ▲
                            per request
```

---

## The Algorithms at a Glance

| # | Algorithm | File | Decides by | Cost | Best for |
|---|-----------|------|------------|------|----------|
| 1 | Round Robin | `round_robin.hpp` | turn order | O(1) | identical servers, uniform requests |
| 2 | Weighted Round Robin | `weighted_round_robin.hpp` | turn order × capacity | O(n) | mixed hardware |
| 3 | Least Connections | `least_connections.hpp` | in-flight count | O(n) | requests of varying duration |
| 4 | Least Response Time | `least_response_time.hpp` | latency × load | O(n) | latency-sensitive traffic |
| 5 | Random | `random_choice.hpp` | coin flip | O(1)\* | huge fleets, many balancers |
| 6 | Power of Two Choices | `power_of_two.hpp` | best of 2 random | O(1)\* | huge fleets, load-aware |
| 7 | IP Hash | `ip_hash.hpp` | `hash(key) % N` | O(1) | stickiness, fixed server count |
| 8 | Consistent Hash | `consistent_hash.hpp` | position on a ring | O(log n) | stickiness + servers come and go |

\* O(n) as written here only because of the healthy-server scan; the decision itself is O(1).

Everything shares `server.hpp`: a `Server` struct, a `Pool` (just a
`std::vector<Server>`), and the sentinel `NONE` returned when nothing is up.
Every algorithm returns an **index into the pool**.

---

## 1. Round Robin

Hand out servers in a fixed cycle.

```
request 1 → A     request 4 → A
request 2 → B     request 5 → B
request 3 → C     request 6 → C
```

One counter, incremented per request, taken modulo the pool size. That's it.

It is perfectly fair **by request count** — which is only perfectly fair overall
when every server is identical *and* every request costs the same. Break either
assumption and round robin cheerfully overloads the weak or unlucky box: it will
hand a 30-second report query to a server already grinding on two others,
because it has no idea that's happening. It doesn't look at load. It counts.

Down servers are skipped rather than wasted as a turn, and the scan is bounded
by the pool size so an all-down pool returns `NONE` instead of spinning.

```cpp
RoundRobin rr;
std::size_t i = rr.pick(pool);   // A, B, C, A, B, C, ...
```

---

## 2. Weighted Round Robin (smooth)

Round robin where a bigger server gets more turns. Set `Server::weight` to
relative capacity — a box with double the cores gets weight 2.

The naive implementation expands the pool into a list (`A A A A A B C`) and
cycles it. That produces the right *ratio* in the worst possible *order*:

```
naive:   A A A A A B C   A A A A A B C
         └─ 5 in a row ─┘   ← A is hammered while B and C sit idle
```

This file implements the **smooth** variant (nginx's algorithm), which yields
the same 5:1:1 ratio spread evenly:

```
smooth:  A A B A C A A   A A B A C A A
```

Each pick does three things:

1. every healthy server does `current += weight`
2. the server with the highest `current` wins
3. the winner pays `current -= total_weight`

Step 3 is the entire trick. Winning puts you deep in debt, so you cannot win
again until the smaller servers have accumulated their way past you. The credit
counters live inside the balancer rather than on `Server`, so the pool stays a
plain data description and two balancers over the same pool can't corrupt each
other's bookkeeping.

The self-test asserts both properties: exactly 500/100/100 over 700 requests,
**and** that A never takes 5 turns back-to-back (which is what would fail if
someone swapped in the naive version).

---

## 3. Least Connections

Send the request to whoever has the fewest requests in flight.

This is the first algorithm that reacts to *reality* instead of a schedule.
Round robin sends request #7 to server B regardless of whether B is chewing on a
slow query. Least connections notices B's count is high and routes around it.

It's self-correcting: a slow server drains slowly, so its count stays high, so
it stops receiving work until it catches up. No configuration, no measurement of
server speed — the queue length *is* the measurement.

**It requires you to track completion.** `active++` on dispatch, `active--` when
the response finishes. Forget the decrement and every server looks permanently
loaded, at which point the algorithm silently degenerates into "always pick
whichever server the tie-break happens to favour".

Speaking of which — the scan start rotates each call. On a cold pool every
server has `active == 0`, and a fixed scan order would dump an entire opening
burst onto index 0 before a single count moved.

> **Weighted least connections** is a one-line variant: compare
> `a.active * b.weight < b.active * a.weight` (integer math, no division) to
> normalise load by capacity. Worth adding only when your servers actually
> differ in size.

---

## 4. Least Response Time

Least connections, weighted by how fast each server actually answers.

Least connections treats "5 in flight" the same everywhere. But 5 requests on a
box averaging 10 ms clears in 50 ms; 5 on a box averaging 200 ms is a
one-second queue. This scores by expected wait instead of raw count:

```
score = avg_latency_ms × (active + 1)      → lowest score wins
```

The `+ 1` counts the request you're about to send, so an idle server is scored
on its own latency rather than collapsing to `0` and always winning.

The demo makes the difference concrete:

| Server | active | latency | score | |
|--------|--------|---------|-------|---|
| A | 0 | 200 ms | 200 | idle but slow — least-connections picks this |
| B | 3 | 10 ms  | **40** | busy but fast — **least-response-time picks this** |
| C | 1 | 90 ms  | 180 | |

`avg_latency_ms` is the calibration knob and must be fed from real measurements,
normally an exponential moving average of recent completions:

```cpp
s.avg_latency_ms = 0.8 * s.avg_latency_ms + 0.2 * observed_ms;
```

Use an EMA, not a lifetime mean. A server that was fast for an hour keeps
*looking* fast for a long time after it starts failing — a lifetime average has
no way to forget, and stale latency data is worse than none.

---

## 5. Random

Pick a healthy server uniformly at random.

This sounds lazy to the point of negligence, and it nearly is — which is exactly
why it scales. Round robin needs a shared counter. Run ten load balancer
instances in front of your fleet and they either coordinate that counter (a
synchronisation point on every single request) or they all independently start
at index 0 and synchronise their bursts onto the same poor server. Random needs
nothing shared, and still converges to an even split by the law of large numbers.

The costs are real though:

- **Variance.** Over 100 requests to 3 servers you will not see 33/33/34. You'll
  see something like 28/41/31. Fine at high volume, lumpy at low volume.
- **No load awareness.** It can hand a request to a server that is drowning,
  because it never looks.

The self-test throws 30,000 requests at 3 servers and asserts each lands within
10% of 10,000 — evenness that only shows up at volume.

> Seed from `std::random_device` in production. The fixed default seed here
> exists so the self-test is reproducible; ship that and every balancer in your
> fleet makes the identical sequence of "random" choices.

---

## 6. Power of Two Choices

Pick **two** servers at random, send to whichever has fewer connections.

The best value-for-money algorithm on this list, and the one most worth
knowing.

Pure random can dump work on a server that's already drowning.
Least-connections avoids that, but must inspect every server on every request —
and in a *distributed* balancer that means every instance needs a fresh global
view of load. Expensive, and the view is stale anyway. Stale global load is
actively dangerous: every balancer reads "server C is idle" at the same instant
and stampedes C together, which is how you turn a load balancer into a
synchronised denial-of-service tool.

Two random samples and one comparison gets you almost all the benefit with zero
shared state. The classic result (Mitzenmacher) is that random assignment leaves
a maximum load of roughly `log n / log log n` above average, while two choices
drops that to `log log n` — an *exponential* improvement, for one extra draw.
A third choice buys almost nothing on top. Two is the sweet spot.

The self-test measures exactly this, in the balls-into-bins setting the theorem
is about — 10,000 requests over 10 servers, average load 1000:

```
random:        busiest server is +20 over average
power of two:  busiest server is +1  over average
```

Same information, same cost class, twentyfold flatter peak. It also asserts the
stronger structural property: given a lopsided pool, power-of-two **never**
selects the single heaviest server, because the max can only win when drawn
against itself — and the two draws are constructed to be distinct.

> The two draws are made distinct by drawing the second from `size - 1` slots
> and shifting it past the first. A reject-and-retry loop would be unbounded in
> the worst case for no benefit.

---

## 7. IP Hash (modulo hashing)

Route by a **key** instead of by turn:

```
index = hash(key) % server_count
```

The key is usually the client IP, but it can be a session ID, a user ID, or a
URL. The point is **stickiness**: the same key deterministically lands on the
same server every time, with no shared session store and no cookie. That's what
makes in-process caches and in-memory sessions work at all.

### The trap

The server count is baked into the answer. Go from 4 servers to 5 and the
modulus changes, so essentially *every* key remaps at once:

```
hash=13:   13 % 4 = 1   →   13 % 5 = 3    moved
hash=27:   27 % 4 = 3   →   27 % 5 = 2    moved
hash=40:   40 % 4 = 0   →   40 % 5 = 0    stayed (lucky)
```

Roughly `1 − 1/N` of all keys move. Every warm cache is instantly wrong, every
sticky session is on the wrong box, and the origin database eats the entire miss
storm simultaneously. One autoscale event or one crashed node triggers it.

The self-test measures the damage on 10,000 keys:

```
add 1 server (4→5): 79.58% of keys remapped
```

That is not a bug in the implementation — it's the algorithm working as
designed, and it's why the next file exists.

> Down servers are handled by probing forward to the next healthy one. Note this
> only *masks* an outage; it doesn't fix the remap problem, because the modulus
> is unchanged. Actually removing the server from the pool changes `N` and
> reshuffles everything.

> This one is a free function, not a class — it has no state whatsoever.

---

## 8. Consistent Hashing

The fix for modulo hashing's remap storm, and the most interesting algorithm
here.

Instead of hashing keys into "one of N slots", hash **both servers and keys**
onto the same circular 64-bit space. Each key belongs to the first server it
meets walking clockwise:

```
   0 ──────────────────────────────────────────────── 2^64
   │       A            B          C          A         │
   │       ▲            ▲          ▲          ▲         │
   │       │            │          │          │         │
   │    key k1 ─────────┘       key k2 ───────┘         │
   │    (walks clockwise to B)  (walks clockwise to A)  │
```

Now remove B. Only the keys sitting between A and B move — they slide on to C.
Every other key on the ring is untouched, because nobody else's *neighbour*
changed. Adding a server is the mirror image: the newcomer steals one arc from
one successor, and nothing else moves.

```
modulo hashing:      ~(1 − 1/N) of keys remap    ← nearly all of them
consistent hashing:  ~(1/N)     of keys remap    ← only the departed share
```

### Virtual nodes — the part people skip

With one ring position per server, three random points on a circle carve it into
wildly uneven arcs. A 50/30/20 split is completely normal, and when a server
leaves, its entire arc lands on exactly one successor — which then falls over,
and *its* arc lands on the next one. That's a cascade.

The fix is to place each server at **many** positions, hashing `"A#0"`, `"A#1"`,
… `"A#149"`. Averaging 150 random arcs smooths the distribution to within a few
percent, and when a server leaves, its 150 arcs are inherited by up to 150
different neighbours instead of one.

`vnodes` is the calibration knob: more vnodes means flatter distribution, a
larger ring, and slightly slower lookup. 100–200 is the standard range.

### Implementation

A `std::map<uint64_t, size_t>` **is** the ring — it's already sorted, and
`lower_bound` **is** the clockwise walk. `end()` means you ran off the top of
the circle, so you wrap to `begin()`; that wrap is the only thing that makes
this a ring rather than a line.

```cpp
ConsistentHash ch(pool, 150);          // build the ring
std::size_t i = ch.pick(pool, "user42");
ch.rebuild(pool, 150);                 // after adding/removing a server
```

Membership changes need a `rebuild()`. **Health flips do not** — `pick()` walks
clockwise past down servers, which deliberately leaves their keys' normal owner
intact for when they come back. The self-test verifies exactly that: when a
server is marked down its keys spill to neighbours without disturbing anyone
else's mapping, and when it recovers, *every* one of its keys returns to it.

### Measured results

```
ring: 600 points (4 servers x 150 vnodes)
spread: A=2636 B=2695 C=2362 D=2307    (expect ~2500 each)
add 1 server (4→5): 19.91% of keys remapped   (theory: ~1/5 = 20%)
A marked down:      20.31% of keys moved      (only A's share)
A recovers:         all keys back on their original server
```

19.91% against a theoretical 20%, versus modulo's 79.58% for the identical
change. That's the whole point of the algorithm in one number.

---

## Health Checks

None of the above matters if you balance traffic evenly onto a dead server.

Every algorithm here honours `Server::healthy` and returns `NONE` when the
entire pool is down. Setting that flag is the health checker's job: poll each
backend (`GET /health` every few seconds), mark it down after N consecutive
failures, mark it up again after N consecutive successes.

The hysteresis matters. Flipping on a single failure makes a server oscillate
in and out of the pool on any transient blip, and every flip is a fresh
rebalancing event — which, for the hash-based algorithms, means a fresh cache
disruption.

Real health checks are network I/O and belong in the load balancer's event loop,
not in a pick() function; that's why this folder models the *result* of a health
check (one bool) and not the check itself.

---

## The Hard Part: State

Round robin is trivial. What actually breaks in production is **session
affinity** — a user logs in on server 3, their next request lands on server 1,
which has never heard of them.

Three fixes, in order of increasing regret:

1. **Make servers stateless.** Put sessions in Redis or the database. Any
   algorithm works, any server can die, scaling is free. Do this one.
2. **Sticky sessions** via a cookie the balancer reads. Works, but now the
   balancer is stateful and a dead server logs its users out.
3. **Consistent hashing on user ID.** Stickiness with no shared store, and the
   remap cost stays at `1/N` when the fleet changes.

Note the ordering: consistent hashing is the best answer to a problem you're
usually better off not having.

---

## Layer 4 vs Layer 7

Orthogonal to all of the above, but worth knowing which one you're configuring:

- **L4** routes on IP and port only and never reads the payload. Fast,
  protocol-agnostic, essentially TCP forwarding.
- **L7** parses HTTP. It can route by path (`/api` → service A, `/img` →
  service B), terminate TLS, compress, and rewrite headers. Slower, far more
  capable. nginx, HAProxy, Envoy, AWS ALB.

The algorithms in this folder apply at both layers — they only care about
picking an index.

---

## Build & Run

```bash
g++ -std=c++17 -O2 -Wall main.cpp -o lb
# MSVC:  cl /std:c++17 /EHsc main.cpp
./lb
```

`main.cpp` is both a demo and a self-test — it prints what each algorithm does
and asserts the property that makes it worth using. It exits `0` with
`All checks passed.`, so it doubles as a CI gate.

The assertions are the interesting part, because each one fails loudly if
someone "simplifies" an algorithm into a subtly different one:

| Algorithm | What is asserted |
|-----------|------------------|
| Round Robin | strict cycle; down servers skipped; all-down returns `NONE` |
| Weighted RR | exact 500/100/100 ratio **and** no run of 5 (catches naive WRR) |
| Least Connections | picks the lightest; 9 dispatches split 3/3/3; skips down |
| Least Response Time | picks the fast-but-busy server over the idle-but-slow one |
| Random | 30,000 requests land within 10% of even |
| Power of Two | flatter peak than random; never picks the heaviest server |
| IP Hash | same key → same server; >50% remap on scale-out (the flaw, proven) |
| Consistent Hash | even spread; <35% remap on scale-out; keys return after recovery |

### Full output

```text
=== Round Robin ===
  cycle:        A B C A B C A
  B down:       A C A C A C

=== Weighted Round Robin (smooth) ===
  weights 5:1:1 A A B A C A A A A B A C A A
  700 requests: A=500 B=100 C=100  (expect 500/100/100)
  longest A run: 4 (naive WRR would be 5)

=== Least Connections ===
  load 7/2/5 -> B
  9 dispatches: A=3 B=3 C=3

=== Least Response Time ===
  A: active=0 latency=200.000ms -> score 200.000
  B: active=3 latency=10.000ms -> score 40.000
  C: active=1 latency=90.000ms -> score 180.000
  picks: B  (least-connections would pick A)

=== Random ===
  sample:       B C A A A A C A C A B A
  30000 reqs:   A=9992 B=10004 C=10004  (expect ~10000 each)

=== Power of Two Choices ===
  10000 requests over 10 servers (average load 1000):
    random:        busiest server is +20 over average
    power of two:  busiest server is +1 over average
  heaviest server chosen 0/5000 times

=== IP Hash (modulo) ===
  10.0.0.1 -> C
  10.0.0.2 -> A
  10.0.0.3 -> D
  10.0.0.4 -> B
  add 1 server (4->5): 79.580% of keys remapped

=== Consistent Hash (ring + virtual nodes) ===
  ring: 600 points (4 servers x 150 vnodes)
  spread: A=2636 B=2695 C=2362 D=2307  (expect ~2500 each)
  add 1 server (4->5): 19.910% of keys remapped  (theory: ~1/5 = 20%)
  A marked down:       20.310% of keys moved  (only A's share)
  A recovers:          all keys back on their original server

All checks passed.
```

---

## Usage

```cpp
#include "server.hpp"
#include "least_connections.hpp"

Pool pool = {{"web-1"}, {"web-2"}, {"web-3"}};

LeastConnections lb;
std::size_t i = lb.pick(pool);
if (i == NONE) return 503;          // whole pool is down

pool[i].active++;                   // dispatch
send_request_to(pool[i].id);
pool[i].active--;                   // completion
```

Key-based algorithms take the key as a second argument:

```cpp
std::size_t i = ip_hash_pick(pool, client_ip);

ConsistentHash ch(pool, 150);
std::size_t j = ch.pick(pool, session_id);
```

---

## Choosing One

```
Do requests need to land on a specific server (cache/session)?
├── no ──▶ Do your servers differ in capacity?
│         ├── no  ──▶ Do requests vary a lot in duration?
│         │           ├── no  ──▶ Round Robin
│         │           └── yes ──▶ Least Connections
│         │                       (huge fleet? Power of Two Choices)
│         └── yes ──▶ Weighted Round Robin
│                     (latency matters more than capacity? Least Response Time)
└── yes ─▶ Will the server count ever change?
          ├── no  ──▶ IP Hash
          └── yes ──▶ Consistent Hashing
```

Honest default for most systems: **least connections**, or **power of two
choices** once you're running enough balancer instances that a shared view of
load stops being free.

---

## Files

| File | Purpose |
|------|---------|
| `server.hpp` | `Server`, `Pool`, `NONE`, and the shared hash mixer |
| `round_robin.hpp` | Round robin |
| `weighted_round_robin.hpp` | Smooth weighted round robin (nginx algorithm) |
| `least_connections.hpp` | Least connections |
| `least_response_time.hpp` | Least response time (latency × load) |
| `random_choice.hpp` | Uniform random |
| `power_of_two.hpp` | Power of two choices |
| `ip_hash.hpp` | Modulo hashing (free function — no state) |
| `consistent_hash.hpp` | Hash ring with virtual nodes |
| `main.cpp` | Demo + assert-based self-test for all eight |

## Possible Extensions

- **Weighted least connections** — normalise `active` by `weight` (one line).
- **Slow start** — ramp traffic to a recovered server gradually instead of
  immediately, so it isn't knocked straight back over by a cold cache.
- **Outlier ejection** — temporarily remove servers whose error rate spikes,
  even when their health check still passes (Envoy does this).
- **Bounded-load consistent hashing** — cap any server at `c ×` average and
  overflow to the next on the ring, combining stickiness with load awareness.
- **Rendezvous (HRW) hashing** — an alternative to the ring: score every server
  with `hash(key + server_id)` and take the max. Simpler, no vnodes needed,
  but O(n) per lookup instead of O(log n).
- **Real health checking** — an async poller that flips `Server::healthy` with
  proper failure/success hysteresis.

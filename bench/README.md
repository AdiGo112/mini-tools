# Skewed-load benchmark

A simulation, not a network test: four backends of unequal capacity (8 workers at 10 ms, 4 at 20 ms, 2 at 20 ms, 1 at 40 ms; each a FIFO queue with exponential service time), Poisson arrivals at 70% of total capacity, and client keys drawn Zipf(s=1.1) over 10k clients (used only by the two hash strategies). Weighted round robin gets the true capacity ratio 32:8:4:1. Seed 12345, so every run prints the same table.

```
g++ -std=c++17 -O2 -Wall bench/skewed_load.cpp -o skewed_load && ./skewed_load
```

Run from the repo root on Windows 11 with MinGW-w64 g++ 13.2.0 (about 1 s). The numbers are simulated milliseconds, so the CPU does not change them; the hash rows depend on libstdc++'s `std::hash`, so another standard library gives different hash rows.

- **share per backend**: percentage of requests each backend received.
- **max/min share**: request-count spread. Equal is not the goal when the boxes are unequal.
- **util spread (max/min)**: spread of offered work divided by capacity. 1.00 means every backend is equally busy, which is the number to look at.
- **p50 / p99**: request latency, queue wait plus service. The queues are unbounded, so an overloaded backend's queue keeps growing for the whole run, which is why some p99s are huge.

backends (slots/mean ms): 8/10 4/20 2/20 1/40 | 200000 requests, Zipf s=1.1 over 10000 clients, 70% load, seed 12345

| strategy | share per backend (%) | max/min share | util spread (max/min) | p50 ms | p99 ms |
|---|---|---|---|---|---|
| round_robin | 25.0 / 25.0 / 25.0 / 25.0 | 1.0 | 32.00 | 556.5 | 1681433.7 |
| weighted_round_robin | 71.1 / 17.8 / 8.9 / 2.2 | 32.0 | 1.00 | 9.8 | 96.3 |
| least_connections | 59.8 / 24.4 / 12.7 / 3.2 | 18.7 | 1.71 | 12.7 | 318.0 |
| least_response_time | 68.8 / 17.3 / 11.2 / 2.7 | 25.1 | 1.31 | 9.6 | 88.9 |
| random | 24.9 / 25.1 / 25.0 / 25.0 | 1.0 | 32.16 | 690.2 | 1672511.6 |
| power_of_two | 49.9 / 28.3 / 15.6 / 6.1 | 8.2 | 3.92 | 114.9 | 199608.2 |
| ip_hash | 17.3 / 19.9 / 15.4 / 47.4 | 3.1 | 87.45 | 41428.4 | 3463911.7 |
| consistent_hash | 19.0 / 33.1 / 24.3 / 23.6 | 1.7 | 39.64 | 55975.4 | 1557237.4 |

**Takeaway:** when backends differ, an even request count is the wrong target. Round robin and random split requests exactly evenly, overload the 1-worker box, and get p99s measured in minutes. Weighted round robin with correct weights (p99 96 ms) is about as good as least-response-time (89 ms), the only adaptive strategy that does this well here; least-connections is close behind. Power-of-two compares only in-flight counts, not speed, so it under-serves the big box. The hash strategies keep each client on one backend, so with Zipf keys a hot client pins its backend, and both hash rows are the worst in the table. Use them for cache affinity, not for balancing.

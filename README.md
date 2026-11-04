# mini-tools

Small systems pieces built from scratch in C++17 to see how they work. Header-only, standard library only, no build system.

| Tool | What it is |
| --- | --- |
| [Bloom Filter](Bloom%20Filter/) | A header-only Bloom filter, sized from the expected item count and target false-positive rate |
| [Load Balancing](Load%20Balancing/) | Eight load-balancing strategies side by side over a shared server pool, with a demo that compares them |

## Load-balancing strategies

1. Round robin
2. Weighted round robin (smooth)
3. Least connections
4. Least response time
5. Random
6. Power of two choices
7. IP hash (modulo)
8. Consistent hashing (ring with virtual nodes)

## Build and run

Any C++17 compiler; tested with MinGW-w64 g++. Each `main.cpp` runs a demo and self-checks, ending with `All checks passed.`

```sh
cd "Bloom Filter"   && g++ -std=c++17 -O2 -Wall main.cpp -o bloom && ./bloom
cd "Load Balancing" && g++ -std=c++17 -O2 -Wall main.cpp -o lb    && ./lb
```

Binaries are not committed (see `.gitignore`); build them with the commands above.

## Files

Each folder has its own README explaining the idea, the trade-offs and the API in detail.

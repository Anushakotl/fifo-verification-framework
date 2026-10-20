# FIFO Verification Framework (C++)

A constrained-random verification environment written in C++ for a synchronous FIFO. It shows how a hardware testbench is organized: random stimulus is generated, applied to the design, observed, checked against a reference model, and measured with functional coverage. The FIFO also contains a switchable bug, so the project demonstrates both catching a bug and a test that misses it.

## What it does

1. A **generator** creates random read and write transactions from a fixed seed, so every run is reproducible.
2. A **driver** applies each transaction to the FIFO (the design under test, or DUT).
3. A **monitor** records what happened and broadcasts it to anyone subscribed.
4. A **scoreboard** keeps its own reference queue and checks every result against it.
5. A **coverage** collector counts which interesting situations the test actually reached, and flags the ones it never reached.
6. An **environment** creates and connects all of the above, runs the test, and prints a final report with PASS or FAIL.

## Architecture

```
Generator --> list of Transactions
                    |
     for each one:  v
                 Driver --push/pop--> syncFIFO (DUT)
                    |
                    v   Observed (what happened)
                 Monitor --notify--> Scoreboard  (checks it)
                                 \-> Coverage    (counts it)
```

| Component | Role |
|---|---|
| `syncFIFO` | Circular-buffer FIFO with head, tail and count. Optional injected bug. |
| `Transactions`, `WriteTxn`, `ReadTxn` | Abstract base class with two derived request types and an auto-assigned id. |
| `Generator` | Seeded random stream of transactions with a configurable write percentage. |
| `Driver` | Applies one transaction to the FIFO and returns an `Observed` result. |
| `Monitor` | Publishes each result to subscribers through callbacks. |
| `Scoreboard<T>` | Templated checker with a reference queue and a custom exception. |
| `Coverage` | Eight coverage bins with a report that marks holes. |
| `Component`, `Environment` | Base class with build, run and report phases, and the class that owns and connects everything. |

## Scoreboard checks

The scoreboard compares the FIFO with a simple model and reports a mismatch when:

- a write is accepted although the model is full
- a write is rejected although the model is not full
- a read returns data different from the oldest expected value
- a read is rejected although the model holds data

A read that succeeds while the model is empty throws a `ScoreboardError`, which the environment catches and counts so the simulation keeps going.

## Coverage bins

`write_ok`, `read_ok`, `write_when_full`, `read_when_empty`, `hit_full`, `hit_empty`, `back_to_back_writes`, `back_to_back_reads`.

Every bin starts at zero, so a situation the test never reached appears in the report as a `HOLE`.

## The injected bug

When the bug is enabled and the FIFO is full, a write overwrites the oldest entry and still reports success, which silently loses data. It only shows up when a write arrives at a full FIFO.

## Results

The program runs four scenarios (FIFO depth 4, 1000 transactions, seed 1):

| Scenario | Mismatches | Result |
|---|---|---|
| Clean FIFO, 70% writes | 0 | PASS |
| Buggy FIFO, 70% writes | 632 | FAIL (bug caught) |
| Buggy FIFO, 10% writes | 0 | PASS (bug missed) |
| Buggy FIFO, 85% writes | 846 | FAIL (bug caught) |

The third scenario is the main finding. The FIFO is broken, but the test passes. The coverage report shows why:

```
===== 3. buggy DUT, write 10% =====
--- Scoreboard ---
  checks:     1000
  mismatches: 0
  errors:     0
--- Coverage ---
  back_to_back_reads: 776
  back_to_back_writes: 15
  hit_empty: 100
  hit_full: 0   <-- HOLE
  read_ok: 119
  read_when_empty: 762
  write_ok: 119
  write_when_full: 0   <-- HOLE
RESULT: PASS
```

With mostly reads, the FIFO never fills, so the buggy case is never exercised. A passing test says nothing about situations it never tried, and coverage is what reveals that gap.

## Build and run

Requires a C++17 compiler such as g++.

```
g++ -std=c++17 src/main.cpp -o sim
./sim
```

On Windows PowerShell:

```
g++ -std=c++17 src/main.cpp -o sim; if ($?) { .\sim.exe }
```

## C++ concepts demonstrated

- Classes, constructors, destructors, initializer lists, static members
- Inheritance, abstract base classes, virtual functions, virtual destructors, `dynamic_cast`
- Dynamic memory with `new` and `delete`, references, copy operations deleted for a class that owns memory
- Class and function templates
- Custom exceptions derived from `std::runtime_error`
- Lambdas and `std::function` used as callbacks
- STL containers: `vector`, `queue`, `map`
- Seeded random generation with `<random>`

## Relation to UVM

The structure follows the same layout used by UVM testbenches: transaction (sequence item), generator (sequence), driver, monitor, scoreboard, coverage and environment, with phases called build, run and report. This project is a plain C++ version of those ideas.

## Possible extensions

- Add a second DUT, such as a small ALU, and reuse the same components
- Add more bug types and find which stimulus exposes each one
- Add coverage bins for occupancy ranges
- Split the code into separate header and source files
- Replace raw `new` and `delete` with `std::unique_ptr`

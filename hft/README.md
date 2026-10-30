# High-Frequency Trading (HFT) & Quantitative Interview Question Bank

A comprehensive, difficulty-ordered reference of **100 real-world HFT and Quantitative interview questions** covering **Low-Latency Modern C++20**, **Lock-Free Concurrency**, **CPU Architecture & OS Internals**, **Kernel-Bypass Networking**, and **Quantitative Probability, Expected Value, Markov Chains & Brainteasers** (curated from `@Quant_Prof`, Coding Jesus, QuantVault, Geodesic, Xinfeng Zhou's *Green Book*, *Brainstellar*, and technical loops at Citadel Securities, Hudson River Trading, Jump Trading, Tower Research, Optiver, IMC, SIG, Two Sigma, Graviton, Quadeye, AlphaGrep, Quantbox, and NK Securities).

---

## Repository Structure (`src/` & `test/`)

The `src/` directory provides 20 self-contained, single-header C++20 reference implementations ordered by difficulty (`q01` easiest to `q20` hardest), paired with deterministic and multi-threaded stress tests in `test/`:

| # | Header (`src/`) | Test (`test/`) | Topic |
|---|---|---|---|
| 01 | `q01_crtp_concepts_dispatch.hpp` | `test_q01_crtp_concepts_dispatch.cpp` | Static Polymorphism (`CRTP` & C++20 `concept` dispatch) |
| 02 | `q02_binary_wire_codec.hpp` | `test_q02_binary_wire_codec.cpp` | Unaligned & Endian-Safe Binary Exchange Wire Codec |
| 03 | `q03_fixed_point_price.hpp` | `test_q03_fixed_point_price.cpp` | Checked Fixed-Point Decimal Price Arithmetic (`FixedPrice<Scale>`) |
| 04 | `q04_intrusive_list.hpp` | `test_q04_intrusive_list.cpp` | $O(1)$ Intrusive Doubly-Linked List |
| 05 | `q05_ttas_spinlock.hpp` | `test_q05_ttas_spinlock.cpp` | Test-and-Test-and-Set (`TTAS`) SpinLock with Backoff |
| 06 | `q06_sharded_counter_map.hpp` | `test_q06_sharded_counter_map.cpp` | Cache-Line-Aligned (`alignas(64)`) Sharded Concurrent Counter Map |
| 07 | `q07_future_then_continuation.hpp` | `test_q07_future_then_continuation.cpp` | Exception-Propagating `then(std::future<T>, F)` Continuation |
| 08 | `q08_ttl_read_cache.hpp` | `test_q08_ttl_read_cache.cpp` | Concurrent Read-Mostly TTL Cache (`std::shared_mutex` + `std::jthread`) |
| 09 | `q09_thread_pool.hpp` | `test_q09_thread_pool.cpp` | Fixed-Worker Thread Pool (`DrainQueued` & `DiscardQueued` policies) |
| 10 | `q10_backpressure_pipeline.hpp` | `test_q10_backpressure_pipeline.cpp` | Bounded Backpressure Channel (`Block`/`DropNewest`/`DropOldest`) |
| 11 | `q11_fixed_object_pool.hpp` | `test_q11_fixed_object_pool.cpp` | $O(1)$ Fixed-Block Slab Object Pool (`std::construct_at` / `std::launder`) |
| 12 | `q12_shared_ptr_atomic.hpp` | `test_q12_shared_ptr_atomic.cpp` | Reference-Counted `SharedPtr<T>` (`relaxed` copy vs. `acq_rel` release) |
| 13 | `q13_inplace_function_sbo.hpp` | `test_q13_inplace_function_sbo.cpp` | Small-Buffer-Optimized Zero-Heap `InplaceFunction<Ret(Args...), Capacity>` |
| 14 | `q14_atomic_rcu_publish.hpp` | `test_q14_atomic_rcu_publish.cpp` | RCU Immutable Snapshot Publisher (`std::atomic<std::shared_ptr<const T>>`) |
| 15 | `q15_seqlock_snapshot.hpp` | `test_q15_seqlock_snapshot.cpp` | Lock-Free `SeqLock<T>` Optimistic Reader/Writer |
| 16 | `q16_spsc_ring_buffer.hpp` | `test_q16_spsc_ring_buffer.cpp` | Lock-Free SPSC Ring Buffer with Cached Shadow Indices |
| 17 | `q17_observable_history.hpp` | `test_q17_observable_history.cpp` | Observable Persistent History DAG (Wait-Free `snapshot()`) |
| 18 | `q18_ab_feed_arbitrator.hpp` | `test_q18_ab_feed_arbitrator.cpp` | HFT A/B Redundant Multicast Feed Arbitrator & Gap Reorder Ring |
| 19 | `q19_limit_order_book.hpp` | `test_q19_limit_order_book.cpp` | $O(1)$ L3 Limit Order Book (`Add`, `Cancel`, `ReduceQty`, `Execute`) |
| 20 | `q20_mpmc_bounded_queue.hpp` | `test_q20_mpmc_bounded_queue.cpp` | Atomic MPMC Bounded Queue (Vyukov Per-Slot Sequence Tickets) |

### Building and Running Tests

```bash
# Linux / macOS (GCC 11+ or Clang 14+)
for f in test/test_q*.cpp; do
    g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread "$f" -o /tmp/hft_test && /tmp/hft_test
done
```

---

## Part I: Low-Latency C++ & Systems Engineering Questions (1–45)

### Section 1: Live Coding & Data Structure Implementation (Questions 1–20)

1. **Lock-Free SPSC Ring Buffer** (`src/q16_spsc_ring_buffer.hpp`)
   - *Prompt*: Implement a fixed-capacity single-producer/single-consumer ring buffer without locks.
   - *Key Points*: Power-of-two capacity (`index & (Capacity - 1)`), `std::memory_order_release` on index publication, `std::memory_order_acquire` on remote index observation, `alignas(64)` separation of producer/consumer state to prevent false sharing, cached shadow indices to avoid cross-core cache-line reads on every operation, and raw uninitialized byte storage (`std::construct_at` / `std::destroy_at`) so non-default-constructible and move-only types are supported.

2. **Atomic MPMC Bounded Queue** (`src/q20_mpmc_bounded_queue.hpp`)
   - *Prompt*: Implement a bounded multi-producer/multi-consumer concurrent queue using atomics and explain why SPSC techniques fail under multiple producers.
   - *Key Points*: Multiple producers racing on `write_idx` require a Compare-And-Swap (`compare_exchange_weak`) loop, and separating index reservation from payload publication requires a per-slot `std::atomic<size_t> sequence` ticket (Vyukov algorithm). Slot $i$ is initialized with sequence $i$; a producer claims `pos` when `seq == pos` and publishes `seq = pos + 1` (`release`); a consumer claims `pos` when `seq == pos + 1` and releases the slot by setting `seq = pos + Capacity` (`release`). Note on progress guarantees: while mutex-free, Vyukov's ring is technically *obstruction-blocking* if an OS thread is preempted between claiming `write_idx` via CAS and storing `seq = pos + 1` (which is why HFT deployments pin each worker to an isolated core).

3. **$O(1)$ L3 Market-by-Order Limit Order Book** (`src/q19_limit_order_book.hpp`)
   - *Prompt*: Design and implement a Limit Order Book supporting `addOrder`, `cancelOrder`, `reduceOrder`, and `execute` in $O(1)$ time per order event.
   - *Key Points*: Pre-allocated slab pool of `OrderNode` objects indexed directly by dense `OrderId` (worst-case $O(1)$) or open-addressed flat hash table (expected $O(1)$), intrusive doubly-linked list per price level preserving FIFO time priority, a direct `PriceLevel*` back-pointer stored inside each `OrderNode` so `cancelOrder(id)` unlinks the node in worst-case $O(1)$ without searching the book, dense tick-indexed price arrays for $O(1)$ level lookup near the touch (with $O(k)$ matching work when an aggressive order sweeps $k$ resting orders), and integer tick prices (never `double`).

4. **Fixed-Block Slab / Arena Object Pool** (`src/q11_fixed_object_pool.hpp`)
   - *Prompt*: Implement a fixed-capacity object pool with $O(1)$ allocation and deallocation and zero heap allocations on the hot path.
   - *Key Points*: Contiguous `alignas(T)` byte buffer per slot, intrusive free-list index/pointer for unused slots, `std::construct_at` with exception rollback on `acquire()`, `std::launder` when accessing constructed objects in raw storage, and destructor clean-up of remaining live slots on pool destruction.

5. **Reference-Counted `SharedPtr<T>` from Scratch** (`src/q12_shared_ptr_atomic.hpp`)
   - *Prompt*: Implement a thread-safe reference-counted smart pointer and justify the exact memory ordering for increments and decrements.
   - *Key Points*: Control block holding `std::atomic<size_t> ref_count{1}`. Copy construction uses `fetch_add(1, std::memory_order_relaxed)` because creating a new reference from an already-owned live reference requires no new happens-before edge. Destruction uses `fetch_sub(1, std::memory_order_acq_rel)` (or `release` followed by an `acquire` fence before `delete`) so all modifications to `T` completed before other threads dropped their references synchronize-with the final thread executing `~T()`. Note that control-block thread safety does not synchronize concurrent reads/writes to `T` itself or concurrent mutations to the same `SharedPtr<T>` instance.

6. **Lock-Free Optimistic `SeqLock<T>`** (`src/q15_seqlock_snapshot.hpp`)
   - *Prompt*: Implement a Single-Writer / Multi-Reader sequence lock for disseminating market-data BBO snapshots with zero reader starvation of the writer.
   - *Key Points*: Atomic sequence counter starts at $0$ (even) and `T` is trivially copyable with lock-free word atomics. Writer stores `seq + 1` (`relaxed`), issues `std::atomic_thread_fence(std::memory_order_release)` (because a `release` store alone only orders prior accesses, whereas a `release` fence prevents subsequent payload stores from floating before `seq + 1`), writes payload words via `relaxed` atomic stores (avoiding C++ data-race Undefined Behavior when concurrent readers overlap), and stores `seq + 2` (`release`). Reader loads `seq1` (`acquire`), retries if odd, loads payload words (`relaxed`), issues `std::atomic_thread_fence(std::memory_order_acquire)`, and verifies `seq1 == seq2.load(relaxed)`.

7. **Small-Buffer-Optimized Zero-Heap `InplaceFunction`** (`src/q13_inplace_function_sbo.hpp`)
   - *Prompt*: Why is `std::function` avoided on HFT hot paths, and how is a fixed-capacity SBO replacement implemented?
   - *Key Points*: `std::function` heap-allocates when a lambda capture exceeds its internal small buffer. `InplaceFunction<Ret(Args...), Capacity>` enforces `static_assert(sizeof(F) <= Capacity && alignof(F) <= alignof(std::max_align_t))` at compile time and stores static dispatch pointers (`invoke`, `move_construct`, `destroy`) alongside `alignas(std::max_align_t) std::byte m_storage[Capacity]`.

8. **A/B Redundant Multicast Feed Arbitrator** (`src/q18_ab_feed_arbitrator.hpp`)
   - *Prompt*: Exchanges broadcast identical UDP multicast packets over independent network paths (Line A and Line B). Implement an arbitrator that processes whichever packet arrives first, deduplicates the slower leg, and reorders bounded out-of-order gaps.
   - *Key Points*: Track `uint64_t next_expected_seq`; immediately emit packets matching `next_expected_seq`; drop duplicates where `static_cast<int64_t>(seq - next_expected_seq) < 0` (modular half-range comparison valid for sequence deltas $< 2^{63}$); buffer early packets in a power-of-two reorder ring and drain consecutive slots once the missing sequence arrives; trigger out-of-band gap recovery / book snapshot rebuild if a gap exceeds ring capacity.

9. **Compile-Time Polymorphism (`CRTP` & C++20 `concept`)** (`src/q01_crtp_concepts_dispatch.hpp`)
   - *Prompt*: Replace a `virtual void onTick(const Tick&)` callback hierarchy with static polymorphism and explain the hardware benefit.
   - *Key Points*: Eliminates vtable pointer dereference, prevents Branch Target Buffer (BTB) misses on polymorphic call sites, and—most importantly—allows the compiler to inline the handler body directly into the network poll loop.

10. **Unaligned & Endian-Safe Binary Exchange Wire Codec** (`src/q02_binary_wire_codec.hpp`)
    - *Prompt*: Parse and serialize packed big-endian exchange wire frames (such as NASDAQ ITCH / OUCH) safely in ISO C++20.
    - *Key Points*: Avoid `reinterpret_cast<const WireHeader*>(raw_bytes)` on arbitrary byte buffers (risks unaligned access UB and endian mismatch). Copy bytes via `std::memcpy` (compiled to a single register load/store on modern compilers) and convert byte order using `std::endian` (C++20) + a `constexpr` shift/mask byte-swap helper or `__builtin_bswap64` (or `std::byteswap` in C++23).

11. **Checked Fixed-Point Decimal Price (`FixedPrice<Scale>`)** (`src/q03_fixed_point_price.hpp`)
    - *Prompt*: Why are `float` and `double` unacceptable for order book price keys, and how is fixed-point multiplication implemented without 64-bit integer overflow?
    - *Key Points*: Binary IEEE-754 floats cannot represent decimal ticks like $0.01$ or $0.10$ exactly, so economically identical prices computed through different paths (e.g., $0.10 + 0.20$ vs. $0.30$) produce unequal binary significands and fail exact hash/map lookups. Represent prices as `int64_t` scaled by $10^k$, and decompose wide multiplication via quotient/remainder (`a / Scale` and `a % Scale`) with explicit rounding and overflow checks.

12. **$O(1)$ Intrusive Doubly-Linked List** (`src/q04_intrusive_list.hpp`)
    - *Prompt*: Why does an HFT order book use an intrusive doubly-linked list rather than `std::list<Order>`?
    - *Key Points*: `std::list` performs a separate heap allocation per node and adds pointer indirection. Embedding `prev` and `next` pointers directly inside the pre-allocated `Order` struct yields zero allocations, keeps node links in the same cache line as order fields, and allows $O(1)$ removal given only `Order*`.

13. **Test-and-Test-and-Set (`TTAS`) SpinLock** (`src/q05_ttas_spinlock.hpp`)
    - *Prompt*: Why does a naive `while (flag.exchange(true))` spinlock destroy multi-core performance, and how does TTAS fix it?
    - *Key Points*: `exchange` is a Read-Modify-Write (RMW) operation that forces the CPU core to broadcast MESI invalidations (`RFO`) on every iteration, saturating the interconnect. TTAS spins on a read-only `flag.load(std::memory_order_relaxed)` in the `Shared (S)` cache state and only executes `exchange(true, std::memory_order_acquire)` when the lock appears free.

14. **Cache-Line-Aligned Sharded Counter Map** (`src/q06_sharded_counter_map.hpp`)
    - *Prompt*: Design a high-throughput concurrent position/volume counter map updated by many threads.
    - *Key Points*: Partition keys across $2^k$ independent shards, each wrapped in an `alignas(64)` struct so adjacent shard locks and maps never share a 64-byte cache line.

15. **Future Continuation Adapter (`then`)** (`src/q07_future_then_continuation.hpp`)
    - *Prompt*: Implement an asynchronous continuation `then(std::future<T>, F)` that returns immediately to the caller and propagates exceptions through `std::promise` without losing exception types.
    - *Key Points*: Because standard C++20 `std::future` lacks a native readiness callback registration API, hand off the blocking `fut.get()` wait to an async worker/executor thread, capture `std::current_exception()` inside `catch (...)`, and forward it via `promise.set_exception(std::current_exception())`.

16. **Concurrent Read-Mostly TTL Cache** (`src/q08_ttl_read_cache.hpp`)
    - *Prompt*: Implement a thread-safe TTL cache optimized for 99% reads and 1% writes with deterministic background expiry.
    - *Key Points*: `std::shared_lock<std::shared_mutex>` for concurrent readers, `std::unique_lock` for writers/purges, monotonic `std::chrono::steady_clock` timestamps, and a cooperative `std::jthread` + `std::condition_variable_any` sweeper.

17. **Fixed-Worker Thread Pool** (`src/q09_thread_pool.hpp`)
    - *Prompt*: Implement a fixed-size thread pool supporting `submit(F, Args...) -> std::future<R>` and graceful vs. immediate shutdown policies.
    - *Key Points*: Package tasks via `std::packaged_task<R()>`, guard queue state with a single mutex/condition variable, and prevent worker deadlocks on destruction when tasks throw or submit nested work.

18. **Bounded Backpressure Pipeline** (`src/q10_backpressure_pipeline.hpp`)
    - *Prompt*: Implement a bounded inter-stage communication channel supporting `Block`, `DropNewest`, and `DropOldest` overflow policies.
    - *Key Points*: Ring or deque guarded by condition variables for `Block` mode, immediate rejection for `DropNewest`, and head eviction for `DropOldest`, paired with an RAII stage guard that closes the channel on scope exit.

19. **Atomic Immutable Snapshot Publisher & Observable History DAG** (`src/q14_atomic_rcu_publish.hpp`, `src/q17_observable_history.hpp`)
    - *Prompt*: How can a background risk/configuration builder publish multi-kilobyte tables to reader threads using an RCU-like immutable snapshot pattern?
    - *Key Points*: Construct a new immutable `std::shared_ptr<const T>` off-thread and atomically swap `std::atomic<std::shared_ptr<const T>>` (`release` on store, `acquire` on load). Caveats to state in interviews: (1) `std::atomic<std::shared_ptr<T>>` is not guaranteed by the C++20 standard to be lock-free on all standard libraries (verify `is_lock_free()`), and (2) if a writer replaces the snapshot while a reader holds the last reference to the old snapshot, `~T()` will run on the reader thread unless old snapshots are retained in a background garbage-collection ring. For persistent history DAGs (`q17`), use an iterative destructor loop so dropping deep ancestor chains never overflows the call stack.

20. **Additional Live-Coding Classics (`20a–20e`, Frequently Asked in 45-Minute CoderPad Rounds)**
    - **20a. Custom `std::vector<T>`**: Implement `reserve()`, `push_back()`, and `emplace_back()` using raw uninitialized memory and explain how `std::move_if_noexcept` preserves the strong exception guarantee during reallocation.
    - **20b. `SafeVector` (Stable-Pointer Segmented Array)**: Implement a random-access container where `push_back()` never invalidates pointers or references to existing elements (fixed-size page table of power-of-two blocks).
    - **20c. Hierarchical Timing Wheel**: Implement an $O(1)$ timer scheduler for 100,000+ order-timeout callbacks using a slotted ring of intrusive timer lists instead of an $O(\log N)$ `std::priority_queue`.
    - **20d. Streaming Median & Rolling VWAP Tracker**: Maintain a rolling time-window VWAP in $O(1)$ amortized time over a ring buffer, and maintain a streaming median in $O(\log N)$ insertion / $O(1)$ query time via two heaps (or $O(1)$ update time using a fixed-tick-range bucketed histogram with a tracked median cursor).
    - **20e. Compile-Time FIX Tag / String Hash (`constexpr`)**: Compute an FNV-1a 64-bit hash of string literals at compile time so protocol tags can be dispatched inside a `switch` statement.

---

### Section 2: Modern C++ Language Internals & Memory Model (Questions 21–30)

21. **What does `std::move` actually do at runtime?**
    - *Answer*: Nothing at runtime. `std::move(x)` is purely a compile-time cast (`static_cast<std::remove_reference_t<T>&&>(x)`) that turns an lvalue expression into an xvalue, making move constructors and move assignment operators eligible during overload resolution.

22. **Why does `std::vector<T>` copy elements instead of moving them during reallocation if `T(T&&)` lacks `noexcept`?**
    - *Answer*: `std::vector::push_back` provides the **strong exception guarantee** (if an exception is thrown during reallocation, the original vector remains intact). If a move constructor throws mid-way through moving elements to the new buffer, the already-moved elements in the old buffer have been mutated and cannot be safely restored without risking a second exception. Therefore, `std::move_if_noexcept` selects move construction if `std::is_nothrow_move_constructible_v<T>` is true **or** if `T` is move-only (`!std::is_copy_constructible_v<T>`); otherwise it falls back to copying (and for move-only types with a throwing move constructor, the strong guarantee is waived).

23. **Compare `memory_order_relaxed`, `acquire`, `release`, `acq_rel`, and `seq_cst`—and what do they compile to on x86-64 vs. ARM64?**
    - *Answer*:
      - `relaxed`: Guarantees atomicity and modification-order consistency of that single atomic variable only; establishes no synchronization or ordering constraints on other memory accesses.
      - `release` (store) / `acquire` (load): When an `acquire` load observes the value written by a `release` store (or a subsequent value in its release sequence), the store *synchronizes-with* the load—all memory writes performed before the `release` store become visible after the `acquire` load.
      - `acq_rel`: Used on Read-Modify-Write (`fetch_add`, `exchange`, `compare_exchange`) operations to perform both an `acquire` load and a `release` store atomically.
      - `seq_cst`: Enforces Acquire-Release semantics plus a single total global order across all `seq_cst` operations observed consistently by all threads.
      - *Hardware compilation*: Because x86-64 uses Total Store Order (TSO), plain loads and stores already preserve acquire/release ordering in hardware; thus `load(acquire)` and `store(release)` compile to plain `MOV` instructions (preventing only compiler reordering), whereas `store(seq_cst)` emits `XCHG` or an `MFENCE` to drain the core's Store Buffer before subsequent loads execute. On weakly ordered ARM64, `acquire`/`release` emit ordered load/store instructions (`LDAR`/`STLR` or `LDAPR`), distinct from full `DMB` memory barriers.

24. **When is `compare_exchange_weak` preferred over `compare_exchange_strong`, and when is `strong` better?**
    - *Answer*: On LL/SC (Load-Linked / Store-Conditional) architectures like ARM64 and PowerPC, the hardware reservation on a cache line can be cleared by an unrelated cache eviction or context switch, causing SC to fail even when the value equals `expected` (a **spurious failure**). `compare_exchange_strong` wraps the LL/SC in an internal retry loop to hide spurious failures; `compare_exchange_weak` exposes them. Use `weak` when the CAS is already inside a cheap retry loop; use `strong` when only a single attempt is needed or when recomputing the loop body is expensive.

25. **What is the ABA problem in lock-free algorithms, and what are the three primary solutions?**
    - *Answer*: Thread 1 reads head pointer $A$ (with next pointer $B$) and gets preempted. Thread 2 pops $A$, pops $B$, frees/reuses $A$, and pushes $A$ back onto the stack above $C$. Thread 1 wakes up, sees head is still $A$, and succeeds with `CAS(head, A, B)`, corrupting the stack by pointing head to freed node $B$.
    - *Solutions*: (1) **Tagged / Versioned Handles** (pack a monotonically increasing generation counter into a 64-bit integer index/handle or use double-width 128-bit `CMPXCHG16B`), (2) **Hazard Pointers** (readers publish the target pointer in a thread-local hazard slot and **revalidate** that the source atomic still holds that pointer before dereferencing, preventing premature reclamation), or (3) **Epoch-Based Reclamation (EBR) / RCU** (defer node reuse until all threads active in the current epoch have passed a quiescent state).

26. **Why is `reinterpret_cast<const OrderMsg*>(byte_buffer)` dangerous on network buffers in C++, and what is the portable zero-overhead alternative?**
    - *Answer*: Even though C++20 (`P0593R6`) introduced implicit-lifetime creation for trivially copyable structs in `unsigned char`/`std::byte` storage, directly casting an arbitrary offset in a network packet buffer to `const OrderMsg*` still risks: (1) **Unaligned pointer dereference** (`[expr.unary.op]`), which is Undefined Behavior in ISO C++ regardless of whether the target CPU traps, (2) **Strict-aliasing violations** if the buffer is typed as `char[]` or `uint8_t[]` rather than `unsigned char`/`std::byte` or if lifetimes were not implicitly started, and (3) **Endianness and struct padding mismatches** on packed wire protocols. Copying fields via `std::memcpy` into properly aligned variables and applying explicit byte-order conversion is 100% well-defined and compiles to the exact same load/bswap instructions under `-O2`.

27. **What is the purpose of `std::launder` when using placement `new`?**
    - *Answer*: `std::launder(p)` is a compile-time pointer-provenance intrinsic (not a CPU fence or general memory barrier). When a new object is created via placement `new` in storage previously occupied by another object, using the pointer returned directly by placement `new` is always valid; however, accessing the new object through an *old pointer variable* or raw storage address `p` requires `std::launder(p)` whenever transparent replacement (`[basic.life]`) does not apply (for example, when reinterpreting a `std::byte*` slab slot or when subobject/vtable provenance differs).

28. **What is the difference between `volatile` and `std::atomic`?**
    - *Answer*: `volatile` instructs the compiler that reads/writes to an address have observable side effects beyond the abstract machine (such as platform-specific memory-mapped PCIe NIC registers paired with hardware fences, or `volatile sig_atomic_t` inside a POSIX signal handler) and must not be optimized away or reordered relative to other `volatile` accesses in the same thread. It provides **zero** multi-threaded atomicity, **zero** CPU cache-coherency ordering, and **zero** protection against data races. `std::atomic` provides well-defined inter-thread atomicity and memory ordering.

29. **What are the hidden lifetime traps of `std::string_view` and `std::span`?**
    - *Answer*: Both are non-owning `{pointer, length}` views. Returning a `std::string_view` bound to a function-local `std::string`, or storing `std::string_view sv = getTemporaryString();`, creates an immediate dangling pointer at the end of the full-expression. Rule of thumb: use views as function parameters that do not outlive the call; use owning types (`std::string`, fixed char arrays) for return values and struct fields unless the backing buffer's lifetime is statically guaranteed.

30. **Why is a `virtual` function call slow on an HFT hot path if the CPU branch predictor can predict indirect branches?**
    - *Answer*: When a hot loop calls the same derived class 99.9% of the time, the CPU's Indirect Branch Predictor / BTB predicts the target in ~2–4 cycles. The true cost is **missed inlining**: because the target is opaque at compile time (unless devirtualized by LTO), the compiler cannot inline the callee, forcing register spills across the ABI boundary and preventing cross-function constant propagation, loop invariant code motion, and dead-code elimination.

---

### Section 3: CPU Architecture, Cache Hierarchy & OS Internals (Questions 31–39)

#### Representative Hardware & OS Latency Numbers (4 GHz x86-64 CPU, $1\text{ cycle} = 0.25\text{ ns}$)

| Hardware / OS Operation | Approximate Latency | Cycles (@ 4 GHz) |
|---|---|---|
| Register access / ALU instruction | $0.25\text{ ns}$ | $1\text{ cycle}$ |
| L1 Data Cache hit ($32\text{–}48\text{ KB}$) | $\sim 1\text{ ns}$ | $4\text{ cycles}$ |
| L2 Cache hit ($512\text{ KB–}2\text{ MB}$) | $\sim 3\text{–}4\text{ ns}$ | $12\text{–}16\text{ cycles}$ |
| Branch misprediction penalty | $\sim 4\text{–}5\text{ ns}$ | $15\text{–}20\text{ cycles}$ |
| L3 / LLC Cache hit (local socket) | $\sim 12\text{–}25\text{ ns}$ | $50\text{–}100\text{ cycles}$ |
| Cross-core cache line transfer (MESI RFO) | $\sim 35\text{–}60\text{ ns}$ | $140\text{–}240\text{ cycles}$ |
| Main memory (Local DRAM) miss | $\sim 80\text{–}100\text{ ns}$ | $320\text{–}400\text{ cycles}$ |
| Cross-socket NUMA DRAM access | $\sim 140\text{–}200\text{ ns}$ | $560\text{–}800\text{ cycles}$ |
| vDSO `clock_gettime` (user space) | $\sim 15\text{–}25\text{ ns}$ | $60\text{–}100\text{ cycles}$ |
| Fast Linux syscall entry/exit | $\sim 150\text{–}400\text{ ns}$ | $600\text{–}1{,}600\text{ cycles}$ |
| Full OS thread context switch (with cache footprint) | $\sim 1.5\text{–}5\ \mu\text{s}$ | $6{,}000\text{–}20{,}000\text{ cycles}$ |
| Minor page fault (first-touch allocation) | $\sim 2\text{–}5\ \mu\text{s}$ | $8{,}000\text{–}20{,}000\text{ cycles}$ |
| Kernel-bypass NIC (one-way wire-to-userspace) | $\sim 0.5\text{–}0.8\ \mu\text{s}$ | — |
| Kernel-bypass NIC (round-trip RX + TX wire-to-wire) | $\sim 1.0\text{–}1.5\ \mu\text{s}$ | — |

31. **Worked Tick-to-Trade Latency Budget Question**
    - *Prompt*: *"A trading strategy must react to a market-data packet and emit an order within $1.5\ \mu\text{s}$ wire-to-wire. Kernel-bypass networking consumes $1.0\ \mu\text{s}$ combined RX+TX round-trip. At $4\text{ GHz}$, how many sequential main-memory (DRAM) cache misses can the software hot path afford?"*
    - *Answer*:
      - Remaining software budget $= 1.5\ \mu\text{s} - 1.0\ \mu\text{s} = 500\text{ ns} = 2{,}000\text{ cycles}$ at $4\text{ GHz}$.
      - Assuming wire decode + book update + signal evaluation + order encode takes $\sim 800\text{ cycles}$ of L1-resident compute, $1{,}200\text{ cycles}$ remain for memory stalls.
      - At $\sim 400\text{ cycles}$ ($100\text{ ns}$) per dependent (serialized) DRAM miss, the hot path can afford at most $\lfloor 1{,}200 / 400 \rfloor = \mathbf{3}$ sequential DRAM misses. This first-order budget explains why pointer-chasing trees (`std::map`) and cold heap allocations are fatal on the critical path.

32. **Explain the MESI / MOESI Cache Coherency Protocol on a Core-to-Core Write.**
    - *Answer*: Every 64-byte cache line in L1/L2 is tagged as **Modified (M)**, **Exclusive (E)**, **Shared (S)**, or **Invalid (I)**. When Core 0 wants to write to a line currently held in state `S` by Core 0 and Core 1, Core 0 issues a **Read-For-Ownership (RFO)** / Invalidate broadcast over the interconnect. Core 0 places the write into its hardware **Store Buffer** to avoid stalling immediately, waits for Core 1 to invalidate its copy (`S -> I`) and send an invalidation acknowledgement, and then transitions the line to `M`.

33. **True Sharing vs. False Sharing: Why do 8 threads incrementing adjacent `std::atomic<uint64_t>` counters run slower than 1 thread?**
    - *Answer*: Eight `uint64_t` counters occupy $8 \times 8 = 64\text{ bytes}$—a single cache line. Even though each thread updates a distinct variable with `memory_order_relaxed` (**false sharing**), the cache coherency hardware operates at 64-byte cache-line granularity. Every write by any core invalidates the cache line on the other 7 cores, serializing all 8 cores behind interconnect RFO transfers. Fix: pad each per-thread counter with `alignas(64)`.

34. **Why are the first several thousand messages after application startup 20x–50x slower, and how is that mitigated?**
    - *Answer*: Linux uses **lazy demand paging**: fresh anonymous mappings from `mmap`/`brk` are initially unmapped or backed by the shared zero page without physical RAM frames. The first write to each 4 KB page triggers a CPU page-table trap (**minor page fault**), entering the kernel to allocate and zero a physical frame and populate the TLB.
    - *Fix*: Configure sufficient `RLIMIT_MEMLOCK`, call `mlockall(MCL_CURRENT | MCL_FUTURE)` at startup, allocate large arenas with `MAP_POPULATE` and `MAP_HUGETLB` (2 MB or 1 GB HugePages to drastically reduce dTLB misses), and explicitly write-prefault every page of all pools and rings during initialization before market open.

35. **Cold-Path Warming: How do HFT systems keep the order-entry path fast when orders are sent on less than $0.1\%$ of market-data ticks?**
    - *Answer*: The market-data decode path runs millions of times per second and stays hot in L1i, L1d, and the BTB, whereas the order-submission code path goes cold between trades—suffering L1 instruction misses, L1 data misses, and branch mispredictions at the exact moment a trading signal fires. Production engines combat this using **transport-isolated dry-run execution and vendor-supported NIC warm-send mechanisms** (such as Solarflare `Onload` / `ef_vi` TX warm-send or SmartNIC dry-run descriptor rings) that execute the full strategy, risk check, and packet-staging code path on frequent ticks while discarding the frame in the NIC/driver before physical PHY transmission—without mutating live risk counters or advancing live TCP sequence numbers.

36. **Array of Structs (AoS) vs. Struct of Arrays (SoA): When is each faster?**
    - *Answer*: Choose **AoS** (`struct Order { id, price, qty, side }; Order orders[N];`) when an operation accesses multiple fields of a single element selected by index/ID (only 1 cache line is fetched). Choose **SoA** (`struct BookLevels { int64_t prices[N]; int64_t qtys[N]; };`) when scanning or SIMD-vectorizing (`AVX2`/`AVX-512`) a single field across many elements, so unused fields do not pollute the 64-byte cache lines.

37. **How is a Linux server tuned to minimize OS scheduler and interrupt jitter on critical trading threads?**
    - *Answer*:
      - Isolate critical cores from general scheduling via kernel boot parameters: `isolcpus=...`, `nohz_full=...` (suppresses the periodic scheduler tick when only 1 runnable task is on the core), and `rcu_nocbs=...` (offloads RCU kernel callbacks to housekeeping cores).
      - Pin each critical thread (`feed_handler`, `strategy`) to a dedicated physical core on the same **NUMA node** as the PCIe NIC using `pthread_setaffinity_np`.
      - Route NIC/storage hardware IRQs (`smp_affinity`) and kernel workqueues to designated housekeeping cores, disable Hyper-Threading (SMT), lock CPU frequency governors to maximum performance (`cpupower frequency-set -g performance`), disable deep C-states (`processor.max_cstate=0 intel_idle.max_cstate=0`), disable Transparent HugePage background compaction (`defrag=never`), and audit BIOS settings to eliminate System Management Interrupts (SMIs).

38. **How is nanosecond latency measured accurately in C++ on x86-64 (`rdtsc` vs. `rdtscp` vs. `clock_gettime`)?**
    - *Answer*: Plain `rdtsc` is non-serializing—the CPU's out-of-order execution engine can execute `rdtsc` before prior instructions retire or allow subsequent instructions to execute ahead of it. For cycle-accurate microbenchmarking on a pinned core:
      - Start timestamp: `LFENCE; RDTSC; LFENCE` (plus a compiler memory barrier `asm volatile("" ::: "memory")`) so prior instructions retire before reading TSC and measured code cannot start early.
      - End timestamp: `RDTSCP; LFENCE` (plus compiler barrier; `RDTSCP` waits for prior instructions to retire, and trailing `LFENCE` blocks subsequent code from hoisting above it), subtracting calibrated timer overhead.
      - Convert TSC ticks to nanoseconds using the CPU's invariant TSC reference frequency (`constant_tsc` / `nonstop_tsc`), not the dynamic turbo clock. For cross-core wall-clock timestamps where a ~20 ns overhead is acceptable, user-space vDSO `clock_gettime(CLOCK_MONOTONIC_RAW)` avoids a kernel syscall.

39. **What is Coordinated Omission in latency benchmarking?**
    - *Answer*: Coordinated Omission occurs in **closed-loop** or rate-coupled benchmarks where the load generator waits for message $k$ to complete (or backs off during a stall) before sending message $k+1$. Consequently, a single $10\text{ ms}$ pause affects only **one** recorded sample instead of the thousands of messages that would have queued behind that stall at a realistic arrival rate—making p99 look orders of magnitude better than reality. Fixing it requires an **open-loop** workload generator (replaying packets at independent scheduled timestamps $T_{\text{scheduled}}$) and measuring latency from $T_{\text{scheduled}}$ (or wire ingress under unthrottled open-loop replay) to wire egress.

---

### Section 4: Low-Latency Networking & Exchange Connectivity (Questions 40–45)

40. **Why do exchanges use UDP Multicast for Market Data and TCP for Order Entry?**
    - *Answer*:
      - **Market Data (UDP Multicast)**: One-to-many distribution where L2/L3 switches replicate packets in hardware simultaneously to all subscribers, and UDP avoids TCP head-of-line blocking (a dropped packet does not stall newer price updates). Reliability is layered at the application protocol level via sequence numbers, redundant A/B multicast feeds (`src/q18_ab_feed_arbitrator.hpp`), and out-of-band retransmission/snapshot channels.
      - **Order Entry (TCP)**: Point-to-point session providing an ordered, reliable byte stream with flow control while the connection is active (supplemented by application-layer sequence numbers in FIX/OUCH for disconnect recovery).

41. **What is the Nagle's Algorithm + TCP Delayed ACK interaction, and how is it mitigated?**
    - *Answer*: Nagle's algorithm buffers small outgoing writes until either a full MSS is accumulated or all prior sent bytes are ACKed; meanwhile, the receiver's TCP Delayed ACK timer waits (typically up to $40\text{ ms}$ on Linux) before sending a standalone ACK hoping to piggyback on a reply. When an application sends sub-MSS order messages or performs write-write-read patterns, the two features deadlock each other until the delayed-ACK timer expires. Fix: enable `TCP_NODELAY` (`setsockopt`) on the sender to disable Nagle, and where local ACK latency matters on Linux, re-arm `TCP_QUICKACK` after `recv()` calls (as `TCP_QUICKACK` is transient and resets across TCP state changes).

42. **How does Kernel-Bypass Networking (`Solarflare Onload` / `ef_vi`, `Mellanox VMA`, `DPDK`) reduce latency from $\sim 15\ \mu\text{s}$ to $< 1\ \mu\text{s}$?**
    - *Answer*: Standard Linux networking triggers a hardware interrupt (`IRQ`), schedules `NET_RX_SOFTIRQ`, allocates an `sk_buff`, traverses the kernel network stack, copies payload bytes into the socket buffer, and wakes the blocked user thread via a context switch. Kernel bypass eliminates the kernel from the steady-state data path in two ways: (1) **User-space socket stacks** (`Solarflare Onload`, `Mellanox VMA`) intercept POSIX socket calls via `LD_PRELOAD` and run the TCP/UDP stack in user space over mapped NIC rings, and (2) **Direct Layer-2 ring APIs** (`ef_vi`, `DPDK`) let a pinned thread busy-poll the NIC's hardware RX/TX DMA descriptor rings directly in user space with zero steady-state syscalls, zero interrupts, and zero kernel copies.

43. **Compare `select`, `poll`, `epoll`, and `io_uring`.**
    - *Answer*:
      - `select` / `poll`: $O(N)$ linear scan across all $N$ watched file descriptors on every call, copying the descriptor set between user space and kernel space each time (`select` is also limited to `FD_SETSIZE = 1024`).
      - `epoll`: $O(\log N)$ for `epoll_ctl` descriptor registration in an in-kernel red-black tree, and $O(K)$ for `epoll_wait` where $K$ is the number of *ready* descriptors appended to the doubly-linked ready list by NIC interrupt callbacks.
      - `io_uring`: Shared user/kernel Submission Queue (SQ) and Completion Queue (CQ) ring buffers supporting true asynchronous I/O; with `IORING_SETUP_SQPOLL`, a kernel thread polls the submission ring so steady-state submissions require zero syscalls while the polling thread remains active.

44. **What is the difference between Level-Triggered (`LT`) and Edge-Triggered (`EPOLLET`) modes in `epoll`?**
    - *Answer*: In **Level-Triggered** mode (default), `epoll_wait` keeps returning an FD as long as unread bytes remain in its kernel socket buffer. In **Edge-Triggered** mode (`EPOLLET`), `epoll_wait` notifies only once when the FD transitions from not-ready to ready; the application **must** use non-blocking sockets (`O_NONBLOCK`) and read in a loop until `read()` returns `EAGAIN` / `EWOULDBLOCK`, otherwise remaining data will stall until the next network packet arrives.

45. **What are NIC Programmed I/O (PIO) Templates and FPGA Tick-to-Trade Triggering?**
    - *Answer*: Rather than building an entire Ethernet + IP + TCP + exchange order frame from scratch after a market-data tick arrives, a user-space engine pre-stages a template order packet in NIC memory (such as Solarflare Programmed I/O / PIO buffers or delegated sends) with only the price, quantity, TCP sequence, and checksum fields left to patch on trigger. In ultra-low-latency FPGA architectures, a SmartNIC/FPGA connected to the SFP+ transceiver parses the incoming market-data frame in hardware, evaluates pre-loaded risk/trigger thresholds, and transmits a pre-staged order frame in $\sim 80\text{–}150\text{ ns}$ (measured first-bit RX to first-bit TX at the physical layer).

---

## Part II: Quant Probability, Expected Value & Brainteaser Questions (46–100)

*(Curated from `@Quant_Prof` YouTube playlists, Xinfeng Zhou's *Green Book*, *Brainstellar*, *QuantVault*, and Quant Trader / Researcher / Developer interviews at Citadel, Jane Street, Optiver, SIG, HRT, Two Sigma, Graviton, and Quadeye.)*

### Section 5: Featured `@Quant_Prof` YouTube Channel Problems (Questions 46–55)

46. **Two Dice Strictly Higher**
    - *Question*: Two fair 6-sided dice are rolled. What is the probability that the second die is strictly higher than the first?
    - *Solution*: Out of $36$ outcomes, $6$ are ties ($D_1 = D_2$). The remaining $30$ outcomes split symmetrically between $D_2 > D_1$ and $D_1 > D_2$. Thus $P(D_2 > D_1) = \frac{36 - 6}{2 \times 36} = \frac{15}{36} = \mathbf{\frac{5}{12}}$.

47. **Plus or Minus Signs Between $1$ and $10$**
    - *Question*: Either a $+$ or $-$ sign is placed independently before each integer in $1, 2, 3, \dots, 10$. What is the probability that the resulting expression evaluates to $0$?
    - *Solution*: $\mathbf{0}$. The sum $1 + 2 + \dots + 10 = 55$ is odd. Changing any $+k$ to $-k$ changes the total by $-2k$ (an even number), so the parity of the sum is an **invariant** and is always odd—it can never equal $0$.

48. **Probability That $P^P$ Is a Perfect Square**
    - *Question*: An integer $P$ is chosen uniformly at random from $\{100, 101, \dots, 400\}$. What is the probability that $P^P$ is a perfect square?
    - *Solution*: There are $400 - 100 + 1 = 301$ integers. $P^P$ is a perfect square if and only if $P$ is even ($P/2$ is an integer) OR $P$ is odd and $P$ itself is a perfect square. Between $100$ and $400$, there are $151$ even numbers and $5$ odd squares ($11^2=121, 13^2=169, 15^2=225, 17^2=289, 19^2=361$). Thus $P = \frac{151 + 5}{301} = \mathbf{\frac{156}{301}}$.

49. **Odd Number of Heads in $n$ Biased Coin Tosses (`Citadel Quant Interview Problem`)**
    - *Question*: A coin with heads probability $p$ is tossed $n$ times. What is the probability of obtaining an odd number of heads?
    - *Solution*: Using the binomial expansion $(q + p)^n + (q - p)^n$ where $q = 1 - p$, the odd terms cancel in the sum and isolate in the difference: $P(\text{odd heads}) = \mathbf{\frac{1 - (1 - 2p)^n}{2}}$. Notably, if $p = \frac{1}{2}$, the probability is $\frac{1}{2}$ for every $n \ge 1$.

50. **Two-Urn Marble Allocation (`Hudson River Trading — Marble Swap`)**
    - *Question*: Given 50 red marbles, 50 blue marbles, and two empty urns, distribute all 100 marbles between the two urns so that when a blindfolded player picks one urn at random and draws one marble, the probability of drawing a red marble is maximized.
    - *Solution*: Place **1 red marble** in Urn 1, and the remaining **49 red + 50 blue marbles** in Urn 2. Then $P(\text{Red}) = \frac{1}{2}(1) + \frac{1}{2}\left(\frac{49}{99}\right) = \frac{148}{198} = \mathbf{\frac{74}{99} \approx 74.75\%}$.

51. **Graph Pursuit Problems (Bipartite Parity & Cycle Variants)**
    - *Question*: A hunter and a visible target start on distinct vertices of a finite connected graph $G = (V, E)$ and move simultaneously along edges each turn (neither may stay still; capture occurs only if both occupy the same vertex at the same step). Under what conditions on a tree or cycle can the hunter guarantee capture?
    - *Solution*: On any **bipartite graph** (including all trees and even cycles), the shortest-path distance $d(H_t, T_t)$ changes by $+2, 0,$ or $-2$ on each simultaneous move, so distance parity is an **invariant**: if $d(H_0, T_0)$ is odd, capture can never occur. On a **tree**, if $d(H_0, T_0)$ is even, the hunter guarantees capture in at most $\text{diam}(G)$ steps by moving one step along the unique simple path toward the target at every turn. If $G$ contains an **odd cycle**, the hunter can traverse the odd cycle to flip distance parity.

52. **The Drunk Passenger / Airplane Boarding Problem**
    - *Question*: 100 passengers board a 100-seat airplane in order. Passenger 1 is drunk and sits in a uniformly random seat. Each subsequent passenger $k$ sits in seat $k$ if available, or picks a uniformly random available seat otherwise. What is the probability that Passenger 100 sits in seat 100?
    - *Solution*: $\mathbf{\frac{1}{2}}$. Seats $2 \dots 99$ only act as temporary relays. The process is resolved the moment *any* displaced passenger chooses either **Seat 1** (locking Passenger 100 into Seat 100) or **Seat 100** (displacing Passenger 100). At every choice point where both Seat 1 and Seat 100 are still open, both are equally likely to be picked.

53. **Symmetry Game on a Rectangular Table**
    - *Question*: Two players take turns placing identical circular coins of diameter $D$ on a flat rectangular table of dimensions $W \times H$ ($W, H \ge D$, no overlaps allowed). The last player able to place a coin wins. Does Player 1 or Player 2 have a winning strategy?
    - *Solution*: **Player 1 wins**. Player 1 places the first coin dead-center on the table, and thereafter mirrors every move $(x, y)$ of Player 2 at the centrally symmetric point $(-x, -y)$ relative to the center. Because the table is centrally symmetric, whenever Player 2 finds a valid non-overlapping spot, its mirror image is guaranteed to be free and disjoint from the new coin.

54. **Expected Fixed Points in a Random Permutation**
    - *Question*: A deck of $n$ cards labeled $1 \dots n$ is randomly shuffled. What is the expected number of cards that end up in their original position ($a_i = i$), and what is the variance?
    - *Solution*: Define indicator variables $I_i = \mathbf{1}\{a_i = i\}$. Since $E[I_i] = \frac{1}{n}$, by Linearity of Expectation $E\left[\sum_{i=1}^n I_i\right] = n \times \frac{1}{n} = \mathbf{1}$ for all $n \ge 1$. For $n \ge 2$, $E[I_i I_j] = \frac{1}{n(n-1)}$, giving $\text{Var}(X) = \mathbf{1}$.

55. **Probability Two Elements Share a Permutation Cycle**
    - *Question*: In a uniformly random permutation of $\{1, 2, \dots, n\}$, what is the probability that $1$ and $2$ belong to the same cycle, and what is the probability that the cycle containing $1$ has length $k$?
    - *Solution*: Every cycle length $k \in \{1, 2, \dots, n\}$ for element $1$ has the exact same probability $\mathbf{\frac{1}{n}}$. Consequently, element $2$ is in the same cycle as element $1$ with probability $\mathbf{\frac{1}{2}}$ for all $n \ge 2$.

---

### Section 6: Coin Flip & Pattern Waiting-Time Classics (Questions 56–64)

56. **Expected Flips for `HH` vs. `HT`**
    - *Question*: A fair coin is flipped repeatedly. What is the expected number of flips to first see `HH`? What about `HT`?
    - *Solution*:
      - For `HH`: $E_0 = 1 + \frac{1}{2}E_1 + \frac{1}{2}E_0$ and $E_1 = 1 + \frac{1}{2}(0) + \frac{1}{2}E_0 \implies E_0 = \mathbf{6}$.
      - For `HT`: $E_0 = 2$ flips to get the first `H`, followed by $2$ expected flips to get the first `T` $\implies E_0 = \mathbf{4}$.
      - *Intuition*: `HH` overlaps with itself (a `T` destroys all progress), whereas on `HT` an extra `H` keeps the process in state $E_1$.

57. **Expected Flips for `HHH` vs. `HTH` (Conway's Leading Number / Martingale Trick)**
    - *Question*: What is the expected number of fair coin flips until `HHH` appears, vs. `HTH`?
    - *Solution*: By Li's Martingale (where a new gambler bets $\$1$ on the first letter of the pattern at each step and doubles on matches):
      - `HHH` matches prefixes of length $3, 2, 1 \implies E[\text{HHH}] = 2^3 + 2^2 + 2^1 = \mathbf{14}$.
      - `HTH` matches prefixes of length $3$ and $1 \implies E[\text{HTH}] = 2^3 + 2^1 = \mathbf{10}$.

58. **Penney's Game: `HHT` vs. `THH`**
    - *Question*: Player A picks `HHT` and Player B picks `THH`. A fair coin is flipped until one 3-letter sequence appears. What is the probability Player B (`THH`) wins?
    - *Solution*: $\mathbf{\frac{3}{4}}$. The only way `HHT` can appear *before* `THH` is if the coin sequence starts with `HH...` (probability $\frac{1}{4}$), because any `T` that precedes `HH` immediately completes `THH` first!

59. **Von Neumann's Fair Coin from a Biased Coin**
    - *Question*: Given a coin that lands Heads with unknown probability $p \in (0, 1)$, construct a 50/50 fair outcome and find the expected number of flips required.
    - *Solution*: Flip the coin in pairs: map `HT` $\to$ Heads, `TH` $\to$ Tails, and discard `HH` or `TT`. Since $P(\text{HT}) = P(\text{TH}) = p(1-p)$, the outcome is exact 50/50. Each pair succeeds with probability $2p(1-p)$, requiring $\frac{1}{2p(1-p)}$ pairs = $\mathbf{\frac{1}{p(1-p)}}$ expected flips.

60. **Expected Flips Until Both Heads and Tails Appear**
    - *Question*: Flip a fair coin until at least one `H` and at least one `T` have appeared. What is the expected number of flips?
    - *Solution*: The first flip ($1$ step) yields either `H` or `T`; waiting for the opposite face is a Geometric($\frac{1}{2}$) variable ($2$ expected steps). Total $= 1 + 2 = \mathbf{3}$.

61. **The Extra Coin Toss Advantage ($n+1$ vs. $n$ Coins)**
    - *Question*: Player A flips $n+1$ fair coins and Player B flips $n$ fair coins. What is the probability that Player A gets strictly more Heads than Player B?
    - *Solution*: $\mathbf{\frac{1}{2}}$. After the first $n$ coins, either $H_A > H_B$, $H_A < H_B$ (equal probability by symmetry), or $H_A = H_B$ (in which case the $(n+1)$-th coin breaks the tie with probability $\frac{1}{2}$).

62. **Expected Difference $|H - T|$ After $n$ Coin Flips**
    - *Question*: A fair coin is flipped $100$ times. Approximate the expected absolute difference $|H - T|$ between the number of Heads and Tails.
    - *Solution*: Write $X_i \in \{+1, -1\}$, so $S_n = H - T = \sum_{i=1}^n X_i$ has mean $0$ and variance $n$ ($\sigma = \sqrt{n}$). By the Central Limit Theorem, $S_n \approx \mathcal{N}(0, n)$, and for a normal variable $E[|Z|] = \sqrt{\frac{2}{\pi}}\sigma \approx 0.8\sqrt{n}$. For $n = 100$, $E[|H - T|] \approx \mathbf{8}$.

63. **Bayesian Fake Coin Detection (10 Heads in a Row)**
    - *Question*: A bag holds $999$ fair coins and $1$ two-headed coin. A coin is drawn at random and flipped $10$ times, landing Heads all $10$ times. What is the probability the $11$-th flip is Heads?
    - *Solution*: By Bayes' Rule, $\frac{P(\text{Fake} \mid 10H)}{P(\text{Fair} \mid 10H)} = \frac{1 \times 1}{999 \times (1/2)^{10}} = \frac{1024}{999}$. Thus $P(\text{Fake} \mid 10H) = \frac{1024}{2023}$, and $P(11\text{th is } H) = \frac{1024}{2023}(1) + \frac{999}{2023}\left(\frac{1}{2}\right) = \mathbf{\frac{3047}{4046} \approx 75.31\%}$.

64. **Coin Flips to Distinguish $p = 0.50$ from $p = 0.51$**
    - *Question*: How many coin flips $n$ are required so that the standard error of the sample proportion is $0.005$ (placing $\Delta p = 0.01$ at $2$ standard errors), and how many flips are required for a two-sided $\alpha = 0.05$ test to achieve $80\%$ statistical power ($1 - \beta = 0.80$)?
    - *Solution*:
      - **2-Standard-Error Precision Rule**: $\text{SE}(\hat{p}) = \frac{\sigma}{\sqrt{n}} \approx \frac{0.5}{\sqrt{n}}$. Setting $2 \times \frac{0.5}{\sqrt{n}} = 0.01 \implies \sqrt{n} = 100 \implies n = \mathbf{10{,}000}$ flips (at which a one-sided $\alpha = 0.025$ test has $50\%$ power at $p = 0.51$).
      - **Full Hypothesis Test Power Formula**: $n \approx \left(\frac{z_{1-\alpha/2} + z_{1-\beta}}{\Delta p / \sigma}\right)^2 = \left(\frac{1.96 + 0.84}{0.01 / 0.5}\right)^2 = (140)^2 = \mathbf{19{,}600}$ flips for $80\%$ power (or $\approx \mathbf{32{,}500}$ flips for $95\%$ power).

---

### Section 7: Dice & Embedded-Option Optimal Stopping Games (Questions 65–72)

65. **Optimal Stopping: 1-Roll, 2-Roll, and 3-Roll Die Game**
    - *Question*: A fair 6-sided die is rolled. The player can take the dollar value shown, or reroll (up to 2 rerolls, i.e., 3 rolls maximum). What is the fair value of this game under optimal play?
    - *Solution*: Solve by backward induction:
      - $V_1$ (1 roll remaining) $= E[D] = \mathbf{3.5}$.
      - $V_2$ (up to 2 rolls): keep $\{4, 5, 6\}$ (since $> 3.5$), reroll $\{1, 2, 3\}$. $V_2 = \frac{3}{6}(5) + \frac{3}{6}(3.5) = \mathbf{4.25}$.
      - $V_3$ (up to 3 rolls): keep $\{5, 6\}$ (since $> 4.25$), reroll $\{1, 2, 3, 4\}$. $V_3 = \frac{2}{6}(5.5) + \frac{4}{6}(4.25) = \mathbf{\frac{14}{3} \approx 4.67}$.

66. **Expected Maximum and Minimum of Two 6-Sided Dice**
    - *Question*: Roll two fair 6-sided dice. What are $E[\max(D_1, D_2)]$ and $E[\min(D_1, D_2)]$?
    - *Solution*: Using the tail-sum formula $E[\max] = \sum_{k=1}^6 P(\max \ge k) = 6 - \sum_{k=1}^5 \left(\frac{k}{6}\right)^2 = 6 - \frac{55}{36} = \mathbf{\frac{161}{36} \approx 4.472}$. Since $\max(D_1, D_2) + \min(D_1, D_2) = D_1 + D_2$, $E[\min] = 7 - \frac{161}{36} = \mathbf{\frac{91}{36} \approx 2.528}$.

67. **Coupon Collector on a 6-Sided Die**
    - *Question*: What is the expected number of rolls required to see all 6 faces of a fair die at least once?
    - *Solution*: Having seen $k$ distinct faces, the number of rolls to see the $(k+1)$-th new face is $\text{Geometric}\left(\frac{6-k}{6}\right)$. Summing independent waiting times: $E = \frac{6}{6} + \frac{6}{5} + \frac{6}{4} + \frac{6}{3} + \frac{6}{2} + \frac{6}{1} = \mathbf{14.7}$ rolls.

68. **Conditioned Die Roll Trap (Roll Until 6 Given Only Even Rolls)**
    - *Question*: A fair die is rolled repeatedly until a $6$ appears. Given that every roll in the sequence was an even number ($2, 4,$ or $6$), what is the expected number of rolls?
    - *Solution*: $\mathbf{1.5}$ rolls (NOT $3$). Let $N$ be the roll count and $A$ the event that all rolls are even. $P(N = k \text{ and } A) = \left(\frac{2}{6}\right)^{k-1}\left(\frac{1}{6}\right)$, and $P(A) = \sum_{k=1}^\infty \left(\frac{1}{3}\right)^{k-1}\frac{1}{6} = \frac{1}{4}$. Thus $P(N = k \mid A) = \left(\frac{1}{3}\right)^{k-1}\left(\frac{2}{3}\right)$, which is a Geometric($p = \frac{2}{3}$) distribution with mean $\frac{1}{2/3} = \mathbf{1.5}$.

69. **Expected Sum of Dice Rolls Before Rolling a $1$**
    - *Question*: Roll a fair die repeatedly and accumulate the sum until a $1$ appears (the terminal $1$ itself is not added). What is the expected total accumulated?
    - *Solution*: Let $\tau \sim \text{Geometric}(1/6)$ be the roll on which the first $1$ appears ($E[\tau] = 6$). By **Wald's Identity**, the expected sum including the terminal roll is $E[S_\tau] = E[\tau] E[X_1] = 6 \times 3.5 = 21$. Subtracting the terminal roll of $1$ gives $21 - 1 = \mathbf{20}$ (equivalently, $5$ expected surviving rolls prior to the first $1$, each uniform on $\{2,3,4,5,6\}$ with mean $4 \implies 5 \times 4 = \mathbf{20}$).

70. **Expected Rolls for `4-5` vs. `5-5` on a Fair Die**
    - *Question*: How many rolls of a fair die are expected to see `4` immediately followed by `5`, vs. `5` immediately followed by `5`?
    - *Solution*: `5-5` has self-overlap ($6^2 + 6^1 = \mathbf{42}$ rolls), whereas `4-5` has no prefix/suffix overlap ($6^2 = \mathbf{36}$ rolls).

71. **Stop-or-Bust Accumulator Die Game**
    - *Question*: A player rolls a die repeatedly, adding each face $\{1,2,3,4,5\}$ to a running pot. If a $6$ is rolled, the pot resets to $0$ and the game ends. The player can stop and walk away with the pot at any time. At what pot value $S$ should a risk-neutral player stop?
    - *Solution*: One additional roll from pot $S$ yields expected change $\frac{1}{6}(1+2+3+4+5) - \frac{1}{6}S = \frac{15 - S}{6}$. Because every surviving roll strictly increases $S$ ($S_{t+1} \ge S_t + 1$), once $S \ge 15$ the set of states $\{S \ge 15\}$ is closed under surviving transitions, so the one-step lookahead rule is globally optimal by the Bellman equation: **stop as soon as $S \ge \mathbf{15}$** (at $S = 15$, stopping and rolling once more tie at expected value $15$).

72. **Non-Transitive Dice (Efron's Dice)**
    - *Question*: Can three 6-sided dice $A, B, C$ be constructed such that $P(A > B) > \frac{1}{2}$, $P(B > C) > \frac{1}{2}$, and $P(C > A) > \frac{1}{2}$?
    - *Solution*: Yes. Example: $A = (2,2,4,4,9,9)$, $B = (1,1,6,6,8,8)$, $C = (3,3,5,5,7,7)$. Each beats the next with probability $\frac{20}{36} = \frac{5}{9} > \frac{1}{2}$.

---

### Section 8: Cards, Urns, Permutations & Optimal Stopping (Questions 73–80)

73. **Expected Cards Turned Until the First Ace**
    - *Question*: In a standard shuffled 52-card deck, what is the expected number of cards turned over (including the Ace) until the first Ace appears?
    - *Solution*: The $4$ Aces partition the $48$ non-Aces into $5$ exchangeable segments, each of expected size $\frac{48}{5} = 9.6$. Adding $1$ for the first Ace gives $9.6 + 1 = \mathbf{10.6} = \frac{53}{5}$.

74. **Red vs. Black Card Stopping Game ("No-Edge Theorem")**
    - *Question*: A standard deck (26 Red, 26 Black) is dealt face-up one card at a time. The player must call "Stop" at least once before the deck runs out (by the 52nd card at the latest), betting that the next undealt card is Red. What is the unconditional win probability under the optimal stopping strategy?
    - *Solution*: $\mathbf{\frac{1}{2}}$. Let $R_t$ be the number of Red cards remaining when $52 - t$ cards are left ($0 \le t < 52$). The conditional win probability $M_t = \frac{R_t}{52 - t}$ is a **martingale**; by Doob's Optional Stopping Theorem, for any valid stopping time $\tau \in \{0, \dots, 51\}$, $P(\text{Win}) = E[M_\tau] = M_0 = \frac{26}{52} = \frac{1}{2}$.

75. **Two Aces Conditional Probability Paradox**
    - *Question*: A player draws 2 cards from a 52-card deck. Compare: (a) $P(\text{both Aces} \mid \text{at least one Ace})$ vs. (b) $P(\text{both Aces} \mid \text{has Ace of Spades})$.
    - *Solution*:
      - (a) $\frac{\binom{4}{2}}{\binom{52}{2} - \binom{48}{2}} = \frac{6}{1326 - 1128} = \frac{6}{198} = \mathbf{\frac{1}{33}}$.
      - (b) $\frac{3}{51} = \mathbf{\frac{1}{17}}$. Specifying the exact suit nearly doubles the conditional probability!

76. **Bertrand's Box Paradox (Three Cards in a Hat)**
    - *Question*: A hat holds three cards: one Red-Red (`RR`), one Black-Black (`BB`), and one Red-Black (`RB`). One card is drawn uniformly at random and placed on the table with a uniformly random side facing up; the visible top face is Red. What is the probability the hidden bottom face is also Red?
    - *Solution*: $\mathbf{\frac{2}{3}}$. Out of the $6$ equally likely card-side outcomes, $3$ display a Red top face (both faces of `RR` and the Red face of `RB`), and in $2$ of those $3$ cases the hidden bottom face belongs to `RR` and is Red.

77. **The Secretary Problem (Optimal Stopping on Relative Ranks)**
    - *Question*: $N$ candidates of distinct quality are interviewed in uniformly random order; only the relative ranks among candidates seen so far are observable, and each candidate must be accepted or rejected immediately. What strategy maximizes the probability of selecting the single best overall candidate?
    - *Solution*: Reject the first $r$ candidates as a baseline sample, then select the first subsequent candidate $k \in \{r+1, \dots, N\}$ who is better than all previously interviewed candidates. For a given cutoff $r \ge 1$, the exact success probability is $P_N(r) = \sum_{k=r+1}^N \frac{1}{N} \cdot \frac{r}{k-1} = \frac{r}{N} \sum_{j=r}^{N-1} \frac{1}{j}$. As $N \to \infty$, the optimal cutoff satisfies $r^* / N \to \frac{1}{e} \approx 36.79\%$, and the maximum success probability converges to $\mathbf{\frac{1}{e} \approx 36.79\%}$.

78. **Spaghetti / Rope Loops Problem**
    - *Question*: A bag contains $N$ ropes ($2N$ loose ends). At each step, two loose ends are chosen at random and tied together until no loose ends remain. What is the expected number of closed loops formed?
    - *Solution*: At step $k$ (when $m = N - k + 1$ ropes remain with $2m$ loose ends), picking the first end leaves $2m - 1$ choices for the second end, exactly $1$ of which closes a loop. By Linearity of Expectation, $E[\text{loops}] = \mathbf{\sum_{m=1}^N \frac{1}{2m - 1}} = 1 + \frac{1}{3} + \frac{1}{5} + \dots + \frac{1}{2N-1}$.

79. **Local Maxima in a Random Array / Circle**
    - *Question*: Distinct numbers $1 \dots n$ are arranged uniformly at random around a circle ($n \ge 3$). What is the expected number of local maxima (elements strictly larger than both neighbors)?
    - *Solution*: For any triple of adjacent positions, the middle element is the largest of the three with probability $\frac{1}{3}$. By Linearity of Expectation across all $n$ positions, $E[\text{local maxima}] = \mathbf{\frac{n}{3}}$.

80. **100 Prisoners and 100 Boxes (Cycle-Following Strategy)**
    - *Question*: Numbers $1 \dots 100$ are randomly placed in 100 closed boxes. Each of 100 prisoners may open up to 50 boxes to find their own number; if *all* 100 prisoners succeed, they go free. What strategy gives $> 30\%$ survival probability?
    - *Solution*: Prisoner $k$ opens box $k$, then opens the box whose number was inside, following the permutation cycle. Every prisoner succeeds iff the permutation of $1 \dots 100$ contains **no cycle of length $L > 50$**. Because at most one cycle in a 100-element permutation can exceed length $50$, the events $\{ \text{exists a cycle of length } L \}$ for $L \in \{51, \dots, 100\}$ are mutually exclusive, each with probability $\frac{1}{L}$. Thus the exact survival probability for $N = 100$ is $1 - \sum_{L=51}^{100} \frac{1}{L} = 1 - (H_{100} - H_{50}) \approx \mathbf{31.18\%}$ (which converges asymptotically as $N \to \infty$ to $1 - \ln(2) \approx \mathbf{30.69\%}$).

---

### Section 9: Continuous Uniforms & Geometric Probability (Questions 81–87)

81. **Sum of Uniform$(0,1)$ Variables Exceeding $1$**
    - *Question*: Let $U_1, U_2, \dots \stackrel{\text{i.i.d.}}{\sim} \text{Uniform}(0,1)$, and let $N = \min\{n \ge 1 : U_1 + \dots + U_n > 1\}$. What is $E[N]$?
    - *Solution*: The simplex volume satisfies $P(U_1 + \dots + U_n \le 1) = \frac{1}{n!}$. Therefore $E[N] = \sum_{n=0}^\infty P(N > n) = \sum_{n=0}^\infty \frac{1}{n!} = \mathbf{e \approx 2.71828}$.

82. **Broken Stick Forming a Triangle**
    - *Question*: A stick of length $1$ is broken at two points chosen independently and uniformly at random along its length. What is the probability that the three pieces can form a triangle?
    - *Solution*: Three segments summing to $1$ form a triangle iff no segment exceeds $\frac{1}{2}$. For ordered cut points $X < Y$ inside the upper triangle of $[0,1]^2$ (area $\frac{1}{2}$), the constraints $X < \frac{1}{2}$, $Y - X < \frac{1}{2}$, and $1 - Y < \frac{1}{2}$ carve out a central triangle of area $\frac{1}{8}$. Ratio $= \frac{1/8}{1/2} = \mathbf{\frac{1}{4}}$.

83. **Order Statistics of Uniform$(0,1)$ Random Variables**
    - *Question*: Draw $n$ independent $\text{Uniform}(0,1)$ variables $U_1, \dots, U_n$. What are $E[\min_i U_i]$, $E[\max_i U_i]$, and the distribution of the $k$-th smallest value $U_{(k)}$?
    - *Solution*: The $n$ points partition $[0,1]$ into $n+1$ exchangeable intervals of expected length $\frac{1}{n+1}$. Thus $E[U_{(k)}] = \mathbf{\frac{k}{n+1}}$ ($E[\min] = \frac{1}{n+1}$, $E[\max] = \frac{n}{n+1}$), and $U_{(k)} \sim \text{Beta}(k, n - k + 1)$.

84. **Expected Distance Between Two Random Points on $[0,1]$**
    - *Question*: For independent $X, Y \sim \text{Uniform}(0,1)$, what is $E[|X - Y|]$?
    - *Solution*: $|X - Y| = \max(X, Y) - \min(X, Y)$ is the middle segment of $n = 2$ uniform points, so $E[|X - Y|] = \frac{2}{3} - \frac{1}{3} = \mathbf{\frac{1}{3}}$.

85. **Points on a Circle Lying in a Common Semicircle**
    - *Question*: $n$ points are placed independently and uniformly on the circumference of a circle. What is the probability that all $n$ points lie within some semicircle?
    - *Solution*: For each point $i$, the event $E_i$ that all other $n-1$ points lie within the $180^\circ$ arc clockwise from point $i$ has probability $\left(\frac{1}{2}\right)^{n-1}$, and the events $E_1, \dots, E_n$ are mutually exclusive (assuming $< 180^\circ$ span). Thus $P = \mathbf{\frac{n}{2^{n-1}}}$ (for $n = 3$, $P = \frac{3}{4}$).

86. **Two Friends Meeting at a Cafe**
    - *Question*: Two people arrive independently and uniformly between 12:00 and 1:00 PM and each waits 15 minutes ($\frac{1}{4}$ hour). What is the probability they meet?
    - *Solution*: In the unit square $[0,1]^2$, the region $|X - Y| \le \frac{1}{4}$ excludes two corner triangles of side $\frac{3}{4}$. Area $= 1 - 2 \times \frac{1}{2}\left(\frac{3}{4}\right)^2 = 1 - \frac{9}{16} = \mathbf{\frac{7}{16}}$.

87. **Gaussian Correlation & Bivariate Normal Conditional Expectation**
    - *Question*: Let $X \sim \mathcal{N}(0, 1)$. What is $\text{Cov}(X, X^2)$? Furthermore, if $(U, V)$ are **jointly bivariate normal** with standard normal marginals and correlation $\rho$, what are $E[V \mid U = u]$ and $\text{Var}(V \mid U = u)$?
    - *Solution*: By odd symmetry of the standard normal density, $E[X^3] = 0 \implies \text{Cov}(X, X^2) = E[X^3] - E[X]E[X^2] = \mathbf{0}$. For jointly bivariate normal $(U, V)$, decompose $V = \rho U + \sqrt{1 - \rho^2} Z$ with $Z \sim \mathcal{N}(0, 1)$ independent of $U$, giving $E[V \mid U = u] = \mathbf{\rho u}$ and $\text{Var}(V \mid U = u) = \mathbf{1 - \rho^2}$ (note that joint normality is required; marginal normality plus correlation alone does not determine the conditional distribution).

---

### Section 10: Markov Chains, Random Walks & Martingales (Questions 88–93)

88. **Ant on a 3D Cube (Expected Steps to Opposite Vertex)**
    - *Question*: An ant starts at vertex $0$ of a 3D cube and walks along a uniformly chosen edge every second. What is the expected number of steps to reach the diagonally opposite vertex (distance 3), and the expected steps to return to the starting vertex?
    - *Solution*: Group vertices by Hamming distance $d \in \{0, 1, 2, 3\}$ from the start:
      - $E_0 = 1 + E_1$
      - $E_1 = 1 + \frac{1}{3}E_0 + \frac{2}{3}E_2$
      - $E_2 = 1 + \frac{2}{3}E_1 + \frac{1}{3}(0)$
      Solving gives $E_2 = 7$, $E_1 = 9$, and $E_0 = \mathbf{10}$ steps. By stationary distribution $\pi_v = \frac{\text{deg}(v)}{2|E|} = \frac{3}{24} = \frac{1}{8}$, the expected return time to the starting vertex is $\frac{1}{\pi_v} = \mathbf{8}$ steps.

89. **Ants on a Triangle / Square Avoiding Collision**
    - *Question*: Three ants sit at the 3 vertices of an equilateral triangle. Simultaneously, each ant picks one of its 2 adjacent edges at random and walks to the next vertex. What is the probability that no two ants collide?
    - *Solution*: Out of $2^3 = 8$ directional choices, only $2$ avoid collision (all clockwise or all counter-clockwise). Thus $P = \frac{2}{8} = \mathbf{\frac{1}{4}}$ (for an $n$-gon, $\frac{2}{2^n} = 2^{1-n}$).

90. **100 Elastic Ants on a 1-Meter Stick**
    - *Question*: 100 ants are placed at arbitrary positions and directions on a $1\text{-meter}$ stick, walking at $1\text{ m/s}$. Whenever two ants collide head-on, both instantaneously reverse direction. What is the worst-case time until all ants fall off the stick?
    - *Solution*: $\mathbf{1\text{ second}}$. Because ants are indistinguishable, two ants bouncing off each other is mathematically identical to the two ants walking straight through each other without interacting.

91. **Symmetric Random Walk: Gambler's Ruin & Absorption Time**
    - *Question*: For integers $A, B \ge 1$, a symmetric simple random walk starts at $X_0 = 0$ and steps $+1$ or $-1$ with probability $\frac{1}{2}$. What is the probability of hitting $+A$ before $-B$, and what is the expected number of steps $\tau$ to hit either boundary?
    - *Solution*: Because $\tau$ has finite expectation ($E[\tau] < \infty$) and the stopped walk $|X_{t \wedge \tau}| \le \max(A, B)$ is uniformly bounded, Doob's Optional Stopping Theorem applies:
      - Since $X_t$ is a martingale, $E[X_\tau] = X_0 = 0 \implies p A + (1 - p)(-B) = 0 \implies p = \mathbf{\frac{B}{A + B}}$.
      - Since $X_t^2 - t$ is a martingale, $E[X_\tau^2] = E[\tau] \implies E[\tau] = \frac{B}{A+B}A^2 + \frac{A}{A+B}B^2 = \mathbf{A \times B}$.

92. **Asymmetric Random Walk Gambler's Ruin (Biased Coin $p \ne q$)**
    - *Question*: A gambler starts with integer bankroll $k \in \{1, \dots, N-1\}$, bets $\$1$ per round with win probability $p \in (0, 1), p \ne \frac{1}{2}$ and loss probability $q = 1 - p$, and plays until reaching $\$N$ or $\$0$. What is the probability of reaching $\$N$ before ruin?
    - *Solution*: Using the bounded stopped exponential martingale $M_{t \wedge \tau} = (q/p)^{X_{t \wedge \tau}}$, Optional Stopping gives $P(\text{hit } N \text{ before } 0) = \mathbf{\frac{1 - (q/p)^k}{1 - (q/p)^N}}$ (which converges to $\frac{k}{N}$ as $p \to \frac{1}{2}$).

93. **Stationary Distribution of a Graph Random Walk (Degree Shortcut)**
    - *Question*: A particle performs a simple random walk on a finite connected undirected graph $G = (V, E)$. What is the stationary distribution $\pi(v)$ and the expected return time to vertex $v$?
    - *Solution*: Detailed balance $\pi(u) \frac{1}{\deg(u)} = \pi(v) \frac{1}{\deg(v)}$ holds when $\pi(v)$ is proportional to vertex degree: $\pi(v) = \mathbf{\frac{\deg(v)}{2|E|}}$. By Kac's Lemma, the expected number of steps between visits to $v$ is $\frac{1}{\pi(v)} = \mathbf{\frac{2|E|}{\deg(v)}}$ (if $G$ is also non-bipartite, the $t$-step transition probability $P^t(u, v)$ converges to $\pi(v)$ as $t \to \infty$).

---

### Section 11: Logic, Invariants, Weighing & Game Theory Brainteasers (Questions 94–100)

94. **100 Rational Sharks / Lions / Magical Sheep**
    - *Question*: On an island are $N$ strictly ordered, perfectly rational sharks and $1$ prey. At each step, the highest-priority shark may either eat the current prey—which immediately turns that shark into the new sleepy prey for the remaining $N-1$ sharks—or decline, ending the game immediately. Sharks value survival first, and eating second. For $N = 100$, is the initial prey eaten?
    - *Solution*: Work by backward induction:
      - $N = 1$: Shark eats the prey (no remaining sharks to threaten it).
      - $N = 2$: Eating turns the shark into the prey of the $N=1$ subgame (guaranteed death), so the shark declines and the prey survives.
      - General rule: **If $N$ is odd, the prey is eaten; if $N$ is even ($N = 100$), the prey survives untouched**.

95. **25 Horses, 5 Racing Lanes, No Stopwatch**
    - *Question*: What is the minimum number of races needed to identify the 1st, 2nd, and 3rd fastest horses out of 25 horses using a 5-lane track without a timer?
    - *Solution*: $\mathbf{7}$ races:
      - Races 1–5: Race 5 groups of 5 horses.
      - Race 6: Race the 5 group winners ($A_1, B_1, C_1, D_1, E_1$). Suppose $A_1 > B_1 > C_1 > D_1 > E_1$; then $A_1$ is fastest overall, and groups $D, E$ (plus slower horses in $B, C$) are eliminated.
      - Race 7: Race the only 5 remaining contenders for 2nd and 3rd place: $\{A_2, A_3, B_1, B_2, C_1\}$.

96. **2 Eggs and 100 Floors**
    - *Question*: Given 2 identical fragile eggs and a 100-story building, what is the minimum number of drops required in the worst case to determine the highest safe floor?
    - *Solution*: $\mathbf{14}$ drops. Decrease the step size by $1$ after each surviving first-egg drop so that if the first egg breaks on attempt $k$, the second egg needs at most $14 - k$ linear checks. Choose the smallest $x$ such that $\frac{x(x+1)}{2} \ge 100 \implies x = \mathbf{14}$ (drop first egg at floors $14, 27, 39, 50, 60, 69, 77, 84, 90, 95, 99, 100$).

97. **12 Coins and a Balance Scale (Unknown Heavier or Lighter)**
    - *Question*: Out of 12 identical-looking coins, 1 is counterfeit and is either slightly heavier or slightly lighter than the other 11. Using a two-pan balance scale without reference weights, how many weighings are required to identify the counterfeit coin *and* whether it is heavy or light?
    - *Solution*: $\mathbf{3}$ weighings (information lower bound: $12 \times 2 = 24$ possible coin/direction hypotheses, which fits within $3^3 = 27$ ternary outcomes).
    - *Constructive Strategy*:
      - **Weighing 1**: Weigh $\{1,2,3,4\}$ vs. $\{5,6,7,8\}$.
      - If **balanced**, the counterfeit is in $\{9,10,11,12\}$ and $\{1\dots 8\}$ are known genuine: weigh $\{9,10,11\}$ against $\{1,2,3\}$ on Weighing 2 to narrow to at most 3 states, resolved on Weighing 3.
      - If **unbalanced** (say $\{1,2,3,4\}$ is heavier), then either one of $\{1,2,3,4\}$ is heavy or one of $\{5,6,7,8\}$ is light ($8$ hypotheses): on Weighing 2, weigh $\{1,2,5\}$ vs. $\{3,6,9\}$ (rotating suspect coins across pans and bringing in genuine coin $9$), which partitions the $8$ hypotheses into groups of size $3, 3, 2$ for Weighing 3. In general, $w$ weighings resolve up to $\frac{3^w - 3}{2}$ coins.

98. **1000 Wine Bottles, 1 Poisoned, 10 Test Strips**
    - *Question*: One of 1,000 wine bottles is poisoned; the poison takes 24 hours to react on a test strip. How can the exact poisoned bottle be identified in a single 24-hour round using 10 test strips?
    - *Solution*: Number the bottles $0$ to $999$ in 10-bit binary ($2^{10} = 1024 \ge 1000$). Apply a drop from bottle $b$ to strip $j$ if bit $j$ of $b$ is $1$. After 24 hours, the bit pattern of positive strips reads out the poisoned bottle number directly.

99. **Burning Two Non-Uniform Ropes to Measure 45 Minutes**
    - *Question*: Each of two ropes takes exactly 60 minutes to burn from end to end, but their burn rates vary non-uniformly along their length. How can 45 minutes be measured?
    - *Solution*: At $t = 0$, light **both ends** of Rope 1 and **one end** of Rope 2. Rope 1 burns completely at $t = 30\text{ minutes}$, at which moment Rope 2 has 30 minutes of burn time remaining. Immediately light the **other end** of Rope 2; it burns out in $\frac{30}{2} = 15$ more minutes, marking $30 + 15 = \mathbf{45\text{ minutes}}$.

100. **Kelly Criterion Optimal Bankroll Sizing**
     - *Question*: A trader is offered repeated independent bets on a biased coin that lands Heads with probability $p = 0.60$ and pays $1\text{-to-}1$ even money (or in general pays net odds $b$-to-$1$ with $b > 0$ and no shorting, $0 \le f < 1$). What fraction $f^*$ of current bankroll should be wagered on each flip to maximize long-run logarithmic wealth growth?
     - *Solution*: Maximize $E[\ln(1 + f X)] = p \ln(1 + b f) + q \ln(1 - f)$ over $f \in [0, 1)$. Setting the derivative to zero gives the **Kelly formula**: $f^* = \mathbf{\max\left(0, \frac{b p - q}{b}\right)}$. For even-money odds ($b = 1$, $p = 0.60$, $q = 0.40$), $f^* = p - q = 0.60 - 0.40 = \mathbf{20\%}$ of bankroll.

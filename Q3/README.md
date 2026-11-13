# Question 3 – Perfect numbers with POSIX threads vs a single thread

For this question, I crafted a program (`perfect.c`) that finds every perfect number between 1 and 100,000 two times: first with a normal single-threaded loop, and then with 8 POSIX threads. It times both versions with `clock_gettime(CLOCK_MONOTONIC)` so that we can compare them.

A perfect number is a number which is equal to the sum of its proper divisors, for example 6 = 1 + 2 + 3.

## Build and run

```bash
gcc -Wall -Wextra -O2 -pthread perfect -o perfect -lm
./perfect
./perfect -v
./experiment.sh
```

Program output:

```
perfect numbers found: 6 28 496 8128
synchronized total number of perfect numbers between 1 and 100000 is 4
single-threaded time: 0.204254 seconds
multi-threaded time:  0.089726 seconds
```

The single-threaded search finds the same four numbers. The program checks this and prints a warning if the two results are ever different.

Test machine: Intel Core i7-5600U, **2 physical cores, 4 logical CPUs**
(hyper-threading), Linux 7.2.11, gcc `-O2`.

## Program design

Let's take a look at its logic:

**#1:** we wrote the `is_perfect()` function, which starts from `sum = 1` and tests `i = 2 … ⌊√n⌋`. Each divisor `i` found below the square root has a partner `n / i` above it, so we add both of them (only once if `i == n / i`). This costs O(√n) instead of O(n), which means at most 315 loop iterations per number instead of up to 100,000.

**#2:** we split the range into 8 segments using `struct segment { id, start, end, ... }`. Thread `i` gets `start = i × 12,500 + 1` and `end = (i + 1) × 12,500`, so the segments are 1–12,500, 12,501–25,000, …, 87,501–100,000. Each thread receives a pointer to its own struct as the `pthread_create()` argument.

**#3:** we declared the shared state, which is `total_count` (the synchronized counter) and `results[]` with `num_results` (the shared array). Both of them are protected by one `pthread_mutex_t lock`.

**#4**: inside each thread (the `work()` function):
  - it counts perfect numbers in a local variable `local_count` on its own stack;
  - when it finds a perfect number, it locks the mutex, adds the number to
    `results[]`, and unlocks;
  - after finishing its segment, it locks the mutex **only once** to do
    `total_count += local_count`.

Step 5: in the main thread, we call `pthread_mutex_init()`, then 8 × `pthread_create()`, 8 × `pthread_join()`, and then `pthread_mutex_destroy()`. The threads finish in any
order, so we sort `results[]` with `qsort()` before printing.

Do check the [perfect.c](perfect.c) file under the same dicrectory

## Results

### Timing (5 runs)

| Run | Single-threaded (s) | Multi-threaded, 8 threads (s) | Speedup |
|----:|--------------------:|------------------------------:|--------:|
| 1 | 0.211417 | 0.087847 | 2.41x |
| 2 | 0.213651 | 0.092974 | 2.30x |
| 3 | 0.200625 | 0.083067 | 2.42x |
| 4 | 0.195868 | 0.086688 | 2.26x |
| 5 | 0.199221 | 0.086066 | 2.31x |
| **Average** | **0.204156** | **0.087328** | **2.34x** |

### Same program pinned to different CPUs (`taskset`, 3 runs each, averaged)

| CPUs allowed | Hardware | Single (s) | 8 threads (s) | Speedup |
|--------------|----------|-----------:|--------------:|--------:|
| `0`     | 1 logical CPU | 0.1969 | 0.1944 | 1.01x |
| `0,1`   | 1 core, both hyper-threads | 0.1943 | 0.1557 | 1.25x |
| `0,2`   | 2 cores, 1 thread each | 0.1939 | 0.1018 | 1.91x |
| `0-3`   | 2 cores, 4 hyper-threads | 0.1996 | 0.0849 | 2.35x |

### Work per thread (`./perfect -v`)

"Loop iterations" is the number of times the divisor loop ran, and it measures the CPU work of each thread exactly.

| Thread | Range | Loop iterations | Share of total work |
|-------:|-------|----------------:|--------------------:|
| 0 | 1 – 12,500 | 913,055 | 4.4 % |
| 1 | 12,501 – 25,000 | 1,684,824 | 8.0 % |
| 2 | 25,001 – 37,500 | 2,187,305 | 10.4 % |
| 3 | 37,501 – 50,000 | 2,593,615 | 12.4 % |
| 4 | 50,001 – 62,500 | 2,944,326 | 14.1 % |
| 5 | 62,501 – 75,000 | 3,257,699 | 15.6 % |
| 6 | 75,001 – 87,500 | 3,543,451 | 16.9 % |
| 7 | 87,501 – 100,000 | 3,807,895 | 18.2 % |
| | **Total** | **20,932,170** | 100 % |

## Analysis

### Why a mutex is needed for the shared counter and the shared array

`total_count += local_count` looks like one operation, but the CPU does it in three steps: it **loads** `total_count` into a register, **adds** to it,and **stores** it back. Two threads can mix these steps like this:

```
Thread A: load total_count (0)
Thread B: load total_count (0)
Thread A: add 3, store -> total_count = 3
Thread B: add 1, store -> total_count = 1      <- A's update is lost
```

This is called a **race condition**; the result depends on the timing of the threads, so the total could be 1, 2 or 3 instead of 4, and only in some runs, which makes the bug really hard to reproduce.

The shared array is even worse, because adding a number takes two steps that depend on each other: `results[num_results] = n; num_results++;`. Without a lock:

- two threads can read the same `num_results`, write to the **same slot**, and one perfect number is lost;
- or both of them increase the index, which leaves a slot that was never written, and it then prints as garbage (or 0);
- since the index update is not atomic, `num_results` can also end up different from the number of values actually stored, so the printed list and the total disagree.

`pthread_mutex_lock()` makes each update a **critical section**; only one thread at a time can be inside it, so each read-modify-write runs as one unit that cannot be split. The mutex also works as a memory barrier, so a value written by one thread before it unlocks is visible to the next thread after it locks. Without the barrier, the compiler
or the CPU could keep the value in a register or reorder the stores. Reading the results in `main` after `pthread_join()` is safe, because `join` also works as a memory barrier.

### Why a local count with one lock per thread is more efficient

Each thread checks 12,500 numbers but finds at most 3 perfect numbers. Let's compare the options:

| Strategy | Lock/unlock pairs for the counter |
|----------|----------------------------------:|
| Lock for every number checked | 100,000 |
| Lock only when a perfect number is found | 4 |
| **Local count, add once at the end (used here)** | **8** (one per thread) |

Locking costs time even when no other thread holds the lock, because it uses an atomic read-modify-write instruction (`lock cmpxchg`) and works as a memory barrier. When the threads compete for the lock, it gets much worse:

- The cache line that holds the mutex and `total_count` moves between cores on every lock (cache line "ping-pong"), and each move costs tens to hundreds of ycles.
- A thread that finds the lock taken may spin or sleep in the kernel (`futex`), which is a system call and a context switch.
- While the threads wait for each other, the work runs one by one, which removes the benefit of having many threads at all.

Here, one `is_perfect()` call costs only about 2 µs on average (0.204 s / 100,000), so locking 100,000 times would add a noticeable amount of extra work. The local variable lives on each thread's own stack, so increasing it needs no synchronization at all. The shared total is touched only 8 times, so there is almost no chance of competition. Adding to the array still takes a lock for each number found, but that happens only 4
times in the whole run.

### Single-threaded vs multi-threaded times

On average, 8 threads are **2.34x faster** (from 0.204 s down to 0.087 s), and not 8x faster. The `taskset` runs show us why:

- **Only 2 physical cores.** With one logical CPU, 8 threads are not faster than one; they give 1.01x, which is the same time, so all the work still runs on one CPU and the cost of creating threads and context switches is too small to see. With two separate cores, the speedup is 1.91x, which is close to the ideal 2x.
- **Hyper-threading adds little.** Two hyper-threads of the same core give only 1.25x, and using all four logical CPUs improves on two cores only from 1.91x to 2.35x. The inner loop is almost entirely 64-bit integer division (`n % i`). Both hyper-threads share the single divider unit of the core, so they mostly take turns using it instead of running in parallel.
- **More threads than CPUs.** 8 threads run on 4 logical CPUs, so the OS gives them time slices one after the other. This doesn't add CPU capacity, but it helps with load balancing (see below).
- **Fixed costs.** Creating and joining 8 threads costs tens of microseconds. That's small compared to about 0.09 s of work, but it would matter for a much smaller range.
- **Clock frequency.** The laptop CPU runs at a higher turbo frequency when only one core is busy than when all cores are busy, which gives a small advantage to the single-threaded run.

The program is CPU-bound with almost no shared data (8 + 4 lock operations), so the speedup is limited by the hardware, not by synchronization. On a machine with 8 or more physical cores, the next limit would be the load balance, which is explained next.

### Workload balance

Checking `n` costs about √n − 1 loop iterations, so bigger numbers are more expensive. The total work for numbers up to `N` grows like ∫√n dn = (2/3)·N^1.5. As a result, the segments, which are equal in **size**, are very unequal in **work**:

- Thread 0 (1–12,500) does 0.91 M iterations, while thread 7 (87,501–100,000) does 3.81 M iterations, which is **4.2x more**. The work of a segment is roughly proportional to √(midpoint): √93,750 / √6,250 ≈ 3.9.
- Ideally each thread would do 12.5 % of the work, but thread 7 does 18.2 %. On a machine with 8 real cores, the run cannot finish before thread 7 does, so the speedup would be limited to about 1 / 0.182 ≈ **5.5x instead of 8x**. Thread 0 would finish in less than a quarter of that time and then stay idle.
- On my 4-CPU machine, the imbalance is mostly hidden. There are more threads than CPUs, so when a cheap low-range thread finishes, the scheduler puts one of the remaining expensive threads on the free CPU. The total time is bounded by
  max(total / 4 CPUs = 25 %, largest thread = 18.2 %) = 25 % of the work, so the lack of CPU capacity matters more than the imbalance. The per-thread wall times printed by `-v` include time slicing as well as work, so the iteration counts are the reliable measure of each thread's load.

Some better ways to balance the work for this problem:

- **Cyclic (interleaved) distribution**; thread `t` checks `n = t+1, t+9, t+17, …`, so every thread gets a similar mix of small and large numbers.
- **Segments of equal work**; we choose the segment boundaries so that the `N_k^1.5`
  differences of each segment are equal. This gives shorter segments at the top of the range.
- **Dynamic scheduling**; we split the range into many small chunks that the threads take from a shared counter, which is the same idea as the work queue in Question 4.

Please check [results.txt](results.txt) for more.

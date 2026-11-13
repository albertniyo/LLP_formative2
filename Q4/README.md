# Question 4 – Concurrent log scanning with a thread pool and a shared work queue

For this question, I built a program (`logscan.c`) that counts the lines containing a pattern (using `strstr()`) in any number of log files. Instead of creating one thread per file, it uses a fixed pool of worker threads which take file names from a shared work queue protected by a mutex.

```
./logscan <pattern> <report.txt> <number_of_threads> file1.log file2.log ...
```

## Build and run

```bash
gcc -Wall -Wextra -O2 -pthread logscan.c -o logscan.c
./gen_logs.sh
./logscan ERROR report.txt 4 logs/*.log
./experiment.sh
```

Our host machine was Intel Core i7-5600U (**2 physical cores, 4 logical CPUs**), 7.6 GB RAM, with Samsung SATA SSD, ext4, Linux 7.2.11.

## Program design

We have three shared resources, and each one has its own mutex:

| Shared resource | Variables | Protected by |
|-----------------|-----------|--------------|
| Work queue | `queue_files[]` (= `argv[4..]`), `queue_len`, `queue_next` | `queue_mutex` |
| Report file | `FILE *report` | `report_mutex` |
| Grand totals | `grand_matching`, `grand_lines`, `files_done` | `totals_mutex` |

Let's take a look at its logic:

**#1:** in `main()`, we check the arguments (the pattern must not be empty and the number of threads must be between 1 and 1024), open the report file, and initialize the three mutexes with `pthread_mutex_init()`.

**#2:** the queue is handled by `queue_take()`, which locks `queue_mutex`, reads `queue_files[queue_next++]` (or returns `NULL` when the queue is empty), and unlocks. This way, each file name is given to exactly one worker.

**#3:** each worker runs `while ((name = queue_take()) != NULL)`, and for each file it:
  1. opens it with `fopen()`; if it fails, it calls `perror(name)` and `continue`s with the next file;
  2. reads it line by line with `fgets()` inside `read_line()`. Lines longer than the buffer are joined together before matching, so a match can never be split between two `fgets()` chunks;
  3. counts the total lines and the lines where `strstr(line, pattern) != NULL`;
  4. closes it with `fclose()` (read errors are detected with `ferror()` and reported with `perror()`);
  5. locks `report_mutex` and writes **one** `fprintf()` line: `<filename>: <matching> matching lines out of <total> (thread <id>)`;
  6. locks `totals_mutex` and adds its counts to the grand totals.

**#4:** `run_pool()` starts the pool with `pthread_create()` and waits with
`pthread_join()`. When the number of threads is 1, it calls `worker()` directly in the main thread without `pthread_create()`, so the single-threaded baseline has no
threading cost at all.

**#5:** after the pool finishes, `main()` adds
`Total matching lines across <N> files: <total>` to the report, prints the same line, and destroys the mutexes with `pthread_mutex_destroy()`. `<N>` counts only the filee that were actually scanned; missing files are reported with `perror()` and not counted.

For timing, we used `clock_gettime(CLOCK_MONOTONIC)` around the whole scan, which includes creating the threads, processing all files, and joining.

For more, please check [logscan.c](logscan.c).

## Test data

[gen_logs.sh](gen_logs.sh) generates 12 synthetic logs which are the same every time (fixed random seeds), with lines like this one:

```
2026-11-13T08:00:00.197 gateway-14 pid=7680 level=INFO msg="connection opened" latency_ms=816 req_id=b9e483c8
```

About 5 % of the lines have `level=ERROR`. I made the file sizes uneven on purpose, and the alphabetical order (which is the queue order with `logs/*.log`) is not the same as the size order:

| File | Size (MB) | Lines | `ERROR` lines |
|------|----------:|------:|--------------:|
| `auth.log` | 25 | 248,238 | 12,466 |
| `billing.log` | 3 | 28,968 | 1,382 |
| `cache.log` | 50 | 491,853 | 24,637 |
| `db.log` | 10 | 101,216 | 5,111 |
| `gateway.log` | 1 | 9,659 | 465 |
| `mail.log` | 40 | 397,176 | 19,615 |
| `metrics.log` | 5 | 48,274 | 2,455 |
| `payments.log` | 15 | 143,524 | 7,149 |
| `queue.log` | 2 | 19,675 | 964 |
| `scheduler.log` | 30 | 284,409 | 14,293 |
| `search.log` | 8 | 77,964 | 3,889 |
| `web.log` | 20 | 200,496 | 10,032 |
| **Total** | **209 (219,153,118 B)** | **2,051,452** | **102,458** |

## Sample run and correctness

8 threads, with a missing file at the end of the queue:

```
$ ./logscan ERROR report.txt 8 logs/*.log missing.log
missing.log: No such file or directory
total matching lines across 12 files: 102458
total lines: 2051452, skipped files: 1, threads: 8
thread 1 processed 1 file(s)
thread 2 processed 4 file(s)
thread 3 processed 1 file(s)
thread 4 processed 1 file(s)
thread 5 processed 1 file(s)
thread 6 processed 2 file(s)
thread 7 processed 1 file(s)
thread 8 processed 1 file(s)
elapsed time: 0.109982 seconds
```

`report.txt` from that run:

```
logs/billing.log: 1382 matching lines out of 28968 (thread 2)
logs/gateway.log: 465 matching lines out of 9659 (thread 6)
logs/queue.log: 964 matching lines out of 19675 (thread 2)
logs/search.log: 3889 matching lines out of 77964 (thread 2)
logs/metrics.log: 2455 matching lines out of 48274 (thread 7)
logs/db.log: 5111 matching lines out of 101216 (thread 4)
logs/web.log: 10032 matching lines out of 200496 (thread 2)
logs/payments.log: 7149 matching lines out of 143524 (thread 8)
logs/auth.log: 12466 matching lines out of 248238 (thread 1)
logs/scheduler.log: 14293 matching lines out of 284409 (thread 6)
logs/mail.log: 19615 matching lines out of 397176 (thread 5)
logs/cache.log: 24637 matching lines out of 491853 (thread 3)
total matching lines across 12 files: 102458
```

Every line is complete and none of them are mixed. The lines appear in the order the files **finished**, not the order they were queued: small files finish first, and `mail.log` (40 MB) and `cache.log` (50 MB) finish last. Thread 2 got small files,finished them quickly, and went back to the queue three more times (4 files in total), which is the load balancing of the pool at work.

Verification against `grep` (from `experiment.sh`):

```bash
grep -c ERROR logs/*.log | sort                          # per-file counts
grep -c ERROR logs/*.log | awk -F: '{s+=$2} END {print s}'
cat logs/*.log | wc -l
```

```
Per-file matching counts identical to grep -c        (diff of both lists is empty)
grep total:    102458
logscan total: 102458
wc -l total:   2051452                               (= logscan total lines)
```

## Performance results

Each configuration was run 3 times. The time reported is the program's own
`CLOCK_MONOTONIC` measurement, and Speedup = T1 / Tn.

### Warm page cache (all 209 MB already in RAM)

| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | **Average (s)** | **Speedup** |
|--------:|----------:|----------:|----------:|----------------:|------------:|
| 1 (baseline) | 0.157336 | 0.169623 | 0.167233 | **0.1647** | **1.00x** |
| 2 | 0.097620 | 0.093133 | 0.110021 | **0.1003** | **1.64x** |
| 4 | 0.096054 | 0.085389 | 0.090278 | **0.0906** | **1.82x** |
| 8 | 0.090561 | 0.095772 | 0.085099 | **0.0905** | **1.82x** |

### Cold page cache (files evicted before every run, so they are read from the SSD)

Before each run, I evicted the logs with `posix_fadvise(POSIX_FADV_DONTNEED)`, which doesn't need root.

| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | **Average (s)** | **Speedup** |
|--------:|----------:|----------:|----------:|----------------:|------------:|
| 1 (baseline) | 0.504744 | 0.528647 | 0.640747 | **0.5580** | **1.00x** |
| 2 | 0.413588 | 0.414399 | 0.412162 | **0.4134** | **1.35x** |
| 4 | 0.400885 | 0.404264 | 0.404123 | **0.4031** | **1.38x** |
| 8 | 0.403100 | 0.402843 | 0.428933 | **0.4116** | **1.36x** |

### Additional experiments (warm cache, 3-run averages)

| Experiment | Average (s) |
|------------|------------:|
| `cache.log` alone, 1 thread (the largest single task) | 0.0379 |
| 2 threads, alphabetical / largest-first / smallest-first queue | 0.0981 / 0.1070 / 0.1024 |
| 4 threads, alphabetical / largest-first / smallest-first queue | 0.0904 / **0.0808** / 0.0951 |

Mutex contention, measured with `strace -f -c -e trace=futex,openat,read` and 8 threads:

```
% time     seconds  usecs/call     calls    errors syscall
 73.13    1.180681          22     53530           read
 26.85    0.433525      216762         2           futex
  0.02    0.000309          19        16           openat
```

A glibc mutex enters the kernel (`futex`) only when a thread has to **wait** for it. There were only 2 `futex` calls in the whole run, and each one waited for about 0.2 s. These are the `pthread_join()` calls of the main thread waiting for the workers, not mutex waits. The ≈ 44 mutex lock operations (20 queue takes, 12 report writes, 12 total updates) never had to wait at all.

## Analysis

### Why a mutex is needed for the work queue, the report file and the grand total

1.`name = queue_files[queue_next++]` is a read-modify-write of `queue_next`. Without `queue_mutex`, two workers can both read `queue_next == 5`, and then:

- both of them process **the same file**, so its count is added twice and it appears twice in the report;
- both write back `6`, so one increase is lost, or they write back 6 and 7 after both using index 5, so **a file is skipped** and never processed;
- a worker can see `queue_next < queue_len` as true, and another worker takes the last file before the first one uses the index. The first worker then reads past the end of the array (`argv[argc]` is `NULL`, and `fopen(NULL)` crashes).

2. as `FILE *` has one shared buffer and one write position, and formatting a line and adding it to the buffer takes many steps. Without `report_mutex`, two threads can mix their characters (`logs/aulogs/db.log: 51th.log: 12466...`), or both copy into the same part of the buffer so that **one line overwrites the other and is lost**. 

If a flush happens in the middle of a line, a partial line can reach the file. glibc's stdio does lock each `FILE` inside every call, but POSIX only guarantees that for one single call. The explicit mutex makes sure each result is one complete line, even if writing a result later takes several calls (for example `fputs` + `fprintf`). It is also portable and makes the intention of the program clear.

3. `grand_matching += matching` is a load, an add and a store Two threads can both load the old value, and the second store overwrites the first, so the count of one file is silently **lost** and the total doesn't match `grep` anymore. `grand_matching`, `grand_lines` and `files_done` are updated together under one lock, so they always stay consistent with each other.

4. we used one mutex per resource, so a worker writing its report line never blocks another worker that only wants to take the next file from the queue. Each critical section is also kept short: the slow work (reading and matching a file) happens outside all locks, and each lock protects only a few instructions or one
`fprintf`.

### Why a shared queue balances the load better than one file per thread

The work for each file depends on its size, and the sizes go from 1 to 50 MB. With a
**static** assignment, the program cannot finish before the thread with the most data
finishes:

- **One file per thread (12 threads)**: the thread with `cache.log` does 24 % of all the work alone, while the `gateway.log` thread finishes after 0.5 % and then stays idle. The other threads cannot help with `cache.log`.
- **Fixed blocks of files**, for example 4 threads with 3 files each in command line order: the blocks are {auth, billing, cache} = 78 MB, {db, gateway, mail} = 51 MB, {metrics, payments, queue} = 22 MB, {scheduler, search, web} = 58 MB. The slowest thread does 37 % of the work, so the speedup can be at most 209 / 78 = **2.7x**, even with 4 free cores.

With the **shared queue**, a thread that finishes a small file takes the next one immediately. The work is handed out dynamically, so a thread that gets a big file processes fewer files, and a thread that gets small files processes more. 

The sample run shows this: threads 3 and 5 processed 3 small files each, while threads 1 and 7 processed only `cache.log` and `mail.log`. Idle time only happens at the very end, when no files are left and the last large files are still running.

The order of the queue still matters for that last stage, as the 4-thread experiment
shows:

- **largest-first** order (0.081 s) starts the big files early, so the small files fill the gaps at the end. This is the classic "longest processing time first" scheduling rule;
- **smallest-first** order (0.095 s) leaves `cache.log` until the end, so one thread is still busy with it while the others have nothing left to do.

With 2 threads the order makes little difference, because both threads are always working on something until near the end.

### Why the speedup stops growing (or drops) as the number of threads increases

1. **CPU cores.** My machine has 2 physical cores. 2 threads give 1.64x, and going to 4 threads (adding hyper-threads) only raises it to 1.82x. Hyper-threads share the execution units and caches of one core, and this workload is a tight `fgets`/`strstr` loop that already keeps a core busy. 8 threads cannot run at the same time on 4 logical CPUs, so the extra threads only add context switches. That's why 8 threads give the same result as 4 (0.0905 s vs 0.0906 s).

2. **Disk I/O.** When the files are not in RAM, the total time is about 0.41-0.45 s, because the SSD can only deliver about 219 MB / 0.41 s ≈ **535 MB/s**, which is roughly the limit of a SATA link. Even one thread is almost limited by the disk, because the kernel's readahead keeps the SSD busy while the thread scans. Extra threads mostly wait in `read()` for the same device, so the speedup is only about 1.1x, and adding more than 2 threads gains nothing. With an HDD it could even get worse, because several threads reading different files would force the disk head to jump back and forth between them.

3. **Page cache.** With a warm cache, `read()` is a `memcpy` from the kernel's page cache into the stdio buffer, about 53,500 calls of 4 KB each. Page cache lookups scale well across threads, but every byte still has to be copied once by the kernel and scanned once by `strstr`. All the threads share the same memory bandwidth and L3 cache, which is a second limit on scaling. The size of the page cache also matters: the warm runs are 2.7-4.6x faster than the cold runs because the 209 MB fit in RAM. With logs bigge than the free memory, every run would be a cold run.

4. **Uneven file sizes.** One file is scanned by one thread only. `cache.log` alone takes 0.038 s, which is 23 % of the 1-thread time. Even with unlimited cores, the run cannot be faster than 0.038 s, so the speedup is limited to 0.165 / 0.038 ≈ **4.3x**. At 4 threads this limit is already close to the ideal (0.165 / 4 = 0.041 s). With more cores, the large files that start last would take most of the time, and we would need to split large files into chunks to go further.

5. **Mutex contention.** This is **not** a limit in this program. Each file causes only 3 lock operations (queue, report, totals), and the `futex` count (2, both from `pthread_join`) shows that no thread ever waited for a lock. Contention would only matter with thousands of tiny files, or if the program locked once per line instead of once per file. In that case, the threads would spend their time taking turns on the lock and the program would become serial again.

6. **Thread overhead.** Creating and joining threads costs tens of microseconds per thread, and more threads than CPUs means extra context switches and cache misses. This is small here, but it grows with the number of threads, which is why speedup curves often drop a little after passing the number of cores.
 
 
For the full outputs check [results.txt](results.txt) and [report.txt](report.txt).

# Question 2 – System calls vs standard I/O

For this question, I crafted two programs that copy a file, one using raw system calls and the other one using the C standard I/O library, so that we can compare how many times each one enters the kernel and how fast they are.

| Program | Source | API used for the copy |
|---------|--------|-----------------------|
| `copy_sys`   | `copy_sys.c`   | `open()`, `read()`, `write()`, `close()` |
| `copy_stdio` | `copy_stdio.c` | `fopen()`, `fread()`, `fwrite()`, `fclose()` |

Both programs take `<source> <destination> <buffer_size_in_bytes>`, report errors with `perror()`, and time themselves with `clock_gettime(CLOCK_MONOTONIC)` from just before the source is opened until just after both files are closed. For `copy_stdio`, this also includes `fclose()`, which flushes the last buffered block.

## Build and run

```bash
gcc -Wall -Wextra -O2 -o copy_sys copy_sys.c
gcc -Wall -Wextra -O2 -o copy_stdio copy_stdio.c
dd if=/dev/urandom of=big.bin bs=1M count=100       # 104,857,600-byte test file

./copy_sys   big.bin out.bin 4096 && cmp big.bin out.bin
./copy_stdio big.bin out.bin 4096 && cmp big.bin out.bin
strace -c ./copy_sys   big.bin out.bin 4096
strace -c ./copy_stdio big.bin out.bin 4096
```

[experiment.sh](experiment.sh) runs the whole experiment at once. It builds both programs, creates `big.bin` if it is missing, runs every version and buffer size 3 times, checks every
copy with `cmp`, prints the averages, runs the error handling tests, and saves a `strace -c` summary for each combination in `strace/`. The full output is in [results.txt](results.txt).

As **host machine**, I got Intel Core i7-5600U (2 cores / 4 threads), Linux 7.2.11, ext4, glibc and the kernel uses the Meltdown mitigation **PTI** (page table isolation), which makes every switch from user mode to kernel mode more expensive than on newer CPUs.

## Program logic

Let's take a look at the logic of `copy_sys`:

**#1:** we check that the user gave exactly 3 arguments, and that the buffer size is a positive number, otherwise we print the usage or an error and stop.

**#2:** we allocate the buffer with `malloc()`, start the timer, and open the source with `O_RDONLY` and the destination with `O_WRONLY | O_CREAT | O_TRUNC`.

**#3:** we added a while loop which keeps calling `read()` as long as it returns more than 0 bytes. Each block is written with `write_all()`, which loops because `write()` can write fewer bytes than asked.

**#4:**  we close both files, stop the timer, and print how many bytes were copied and how long it took.

`copy_stdio` follows the same steps, but with `fopen()`, `fread()`, `fwrite()` and `fclose()`, and it uses `ferror()` to check if `fread()` stopped because of an error instead of the end of the file.

Look at [copy_sys.c](copy_sys.c) and [copy_stdio.c](copy_stdio.c) for more.

## Correctness

Every copy was compared with `cmp` (no output means the files are the same), and the last copy was also checked with `sha256sum`:

```
1de3e53a17b3c9273b09e6cb8932a28b6f685c50ff6e079a2cc38dc4c70aaba2  big.bin
1de3e53a17b3c9273b09e6cb8932a28b6f685c50ff6e079a2cc38dc4c70aaba2  out.bin
```

Error handling (`perror()` prints the file name and the `errno` message):

```
$ ./copy_sys missing.bin out.bin 4096
missing.bin: No such file or directory
$ ./copy_stdio missing.bin out.bin 4096
missing.bin: No such file or directory
$ ./copy_sys big.bin /no_such_dir/out.bin 4096
/no_such_dir/out.bin: No such file or directory
$ ./copy_stdio big.bin /no_such_dir/out.bin 4096
/no_such_dir/out.bin: No such file or directory
```

## Results

### Elapsed time (program's own timing, 3 runs each)

`big.bin` was read once before the runs, so it was already in the page cache. The destination is also written into the page cache, and the kernel writes it to the disk later, so these times measure copying through the kernel and not the raw disk speed.

I deleted `out.bin` before each run so that `O_TRUNC` never had to free an old 100 MB file.

| Version | Buffer | Run 1 (s) | Run 2 (s) | Run 3 (s) | **Average (s)** |
|---------|-------:|----------:|----------:|----------:|----------------:|
| `copy_sys`   | 512 B  | 0.983472 | 0.823160 | 0.784530 | **0.8637** |
| `copy_stdio` | 512 B  | 0.231648 | 0.202613 | 0.223656 | **0.2193** |
| `copy_sys`   | 4 KB   | 0.179144 | 0.184541 | 0.177433 | **0.1804** |
| `copy_stdio` | 4 KB   | 0.189905 | 0.195078 | 0.191800 | **0.1923** |
| `copy_sys`   | 64 KB  | 0.072887 | 0.059102 | 0.059314 | **0.0638** |
| `copy_stdio` | 64 KB  | 0.091857 | 0.085639 | 0.074547 | **0.0840** |

### System call counts and times

| Version | Buffer | `read` calls | `write` calls | All syscalls | Avg time without strace (s) | Time under strace (s) |
|---------|-------:|-------------:|--------------:|-------------:|----------------------------:|----------------------:|
| `copy_sys`   | 512 B | 204,802 | 204,801 | 409,639 | 0.8637 | 9.962 |
| `copy_stdio` | 512 B |  25,602 |  25,601 |  51,241 | 0.2193 | 1.583 |
| `copy_sys`   | 4 KB  |  25,602 |  25,601 |  51,240 | 0.1804 | 1.538 |
| `copy_stdio` | 4 KB  |  25,602 |  25,601 |  51,241 | 0.1923 | 1.457 |
| `copy_sys`   | 64 KB |   1,602 |   1,601 |   3,240 | 0.0638 | 0.174 |
| `copy_stdio` | 64 KB |   1,602 |   3,200 |   4,840 | 0.0840 | 0.215 |

The `read` count includes one `read` done by the dynamic loader when loading libc, and the last `read` that returns 0 (EOF). The `write` count includes the one `write(1, ...)` of the final `printf`. For example, 104,857,600 / 512 = 204,800 data reads + 1 EOF read + 1 loader read = 204,802.

The `read` and `write` rows from `strace -c` (`seconds` is the CPU time spent in the kernel for that call, and `% time` is its share of all system call time):

| Version | Buffer | `read` calls | `read` seconds | `read` % time | `write` calls | `write` seconds | `write` % time |
|---------|-------:|-------------:|---------------:|--------------:|--------------:|----------------:|---------------:|
| `copy_sys`   | 512 B | 204,802 | 0.705119 | 33.41 | 204,801 | 1.404319 | 66.53 |
| `copy_stdio` | 512 B |  25,602 | 0.093372 | 28.28 |  25,601 | 0.236588 | 71.66 |
| `copy_sys`   | 4 KB  |  25,602 | 0.100841 | 28.65 |  25,601 | 0.250856 | 71.26 |
| `copy_stdio` | 4 KB  |  25,602 | 0.091297 | 28.66 |  25,601 | 0.227278 | 71.34 |
| `copy_sys`   | 64 KB |   1,602 | 0.023024 | 33.94 |   1,601 | 0.044406 | 65.45 |
| `copy_stdio` | 64 KB |   1,602 | 0.022080 | 26.40 |   3,200 | 0.061530 | 73.58 |

`read` and `write` together take at least 99.3 % of the system call time in every run. `write` is always the more expensive one, because writing to a file must allocate page cache pages, mark them dirty, and update the file size and timestamps, while reading a
cached file is mostly a memory copy.

### strace changes the timing

`strace` stops the program at **every** system call, on entry and on exit, and the tracer reads the registers with `ptrace`, so each system call costs several extra context switches. That's why the times under strace are much longer than the programs'

- **own timing:** `copy_sys` with 512 B took **9.96 s** under strace but only **0.86 s** without it, which is about 12x slower. The slowdown grows with the number of system
- **calls:** it is about 8x at 4 KB and about 2.5-3x at 64 KB. The per-call times from `strace -c` are also not accurate. For example, at 4 KB, `copy_stdio` looks faster than `copy_sys` under strace (1.457 s vs 1.538 s), which is the opposite of the measurements without strace (0.192 s vs 0.180 s).

For this reason, I used strace only for the **counts** of system calls and their shares, and the performance comparison uses the programs' own
`clock_gettime(CLOCK_MONOTONIC)` timing.

## Analysis

### How `fread()`/`fwrite()` buffering reduces the number of `read()`/`write()` calls

A `FILE *` has its own buffer in user space. glibc chooses its size from the file's `st_blksize`, which is **4096 bytes** on this ext4 file system (this is the `fstat` call in the strace output). `fread()` and `fwrite()` copy data in and out of this buffer in user space, and they only enter the kernel when the buffer is empty (for reading) or
full (for writing).

With a 512-byte buffer, the program calls `fread()` 204,800 times, but glibc refills its 4 KB buffer with one `read(fd, buf, 4096)` only every 8 `fread()` calls. In the same way, 8 `fwrite()` calls of 512 B fill the buffer once, and then it is flushed with a single `write(fd, buf, 4096)`. A trace of the calls shows this:

```
25600 read(3, ..., 4096) = 4096
25600 write(4, ..., 4096) = 4096
```

So `copy_stdio` with 512 B makes **8x fewer** system calls than `copy_sys` with 512 B (51,241 vs 409,639), and its time drops from 0.864 s to 0.219 s. That is close to the time of `copy_sys` with 4 KB (0.180 s), because the kernel sees the same 4 KB calls from both programs. The small difference left is the user-space work of copying every 512-byte block in and out of the stdio buffer.

When the requested size is the same as the stdio buffer or bigger, glibc stops using its buffer for most of the data:

- **4 KB:** each `fread()`/`fwrite()` becomes exactly one `read()`/`write()`, so both
  versions make the same 25,601/25,602 calls and take almost the same time (0.180 vs
  0.192 s). Buffering gives no benefit here.
- **64 KB:** `fread()` reads straight into the caller's buffer (1,602 reads, the same as `copy_sys`). `fwrite()` first fills the 4 KB internal buffer, flushes it, and then writes the remaining whole blocks directly. This turns each 64 KB `fwrite()` into **two** system calls:

  ```
  read(3, ..., 65536)  = 65536
  write(4, ..., 4096)  = 4096      <- flush of the stdio buffer
  write(4, ..., 61440) = 61440     <- rest written directly from the caller's buffer
  ```

  That's why `copy_stdio` makes 3,200 `write` calls at 64 KB instead of 1,601. Together with the extra 4 KB `memcpy` into the stdio buffer, this makes `copy_stdio` a little **slower** than `copy_sys` at 64 KB (0.084 s vs 0.064 s). Buffering only helps when the program asks for less than the size of the stdio buffer.

### Why each system call adds overhead (user mode to kernel mode switch)

A library call like `fread()` is a normal function call inside the process, while a system call like `read()` must cross the protection boundary between user mode and kernel mode. This happens in 4 steps:

**#1:** the program puts the system call number and its arguments in registers and runs the `syscall` instruction. The CPU switches to privilege level 0 and jumps to the kernel's entry point.

**#2:** the kernel saves the user registers and switches to the kernel stack. On this CPU, PTI also switches page tables, which flushes the TLB entries that are not tagged by PCID. Spectre mitigations (retpolines, IBPB, RSB filling) add more work on entry and on exit.

**#3:** the kernel looks up the call in the system call table, checks the arguments and the file descriptor, finds the `struct file`, takes locks, and runs the read or write path of the file system (page cache lookup, `copy_to_user`/`copy_from_user`).

**#4:** on return, it restores the registers and page tables and runs `sysret` to go back to user mode. At this point, the caches and branch predictors are partly filled
with kernel state.

Steps 1, 2 and 4 cost the same no matter how many bytes are moved, so this fixed cost is paid again on every call. Fitting the `copy_sys` results to
time = calls x c + bytes x k gives:

- 512 B: 409,600 calls, 0.864 s. 64 KB: 3,200 calls, 0.064 s.
- c ≈ (0.864 − 0.064) / (409,600 − 3,200) ≈ **2.0 µs fixed cost per system call**
  (the mode switch plus the kernel's own work for each call).
- The remaining ≈ 0.057 s is the cost of actually moving 2 × 100 MB of data.

With 512-byte buffers, about 93 % of the run time is this fixed overhead rather than useful copying.

### Why the performance gap shrinks as the buffer size grows

The number of system calls is about 2 × file size / buffer size, so the fixed overhead goes down as the buffer gets bigger:

| Buffer | `copy_sys` calls | Fixed overhead (≈ calls × 2.0 µs) | Measured `copy_sys` time | `copy_stdio` time |
|-------:|-----------------:|------------------------------:|-------------------------:|------------------:|
| 512 B  | 409,600 | ≈ 0.81 s  | 0.864 s | 0.219 s |
| 4 KB   |  51,200 | ≈ 0.10 s  | 0.180 s | 0.192 s |
| 64 KB  |   3,200 | ≈ 0.006 s | 0.064 s | 0.084 s |

- With small buffers, the system call overhead takes most of the time. The 4 KB stdio   buffer merges 8 calls into one, so `copy_stdio` is **3.9x faster** than `copy_sys` at
  512 B.
- Once the program's buffer is at least as big as the stdio buffer (4 KB), stdio has nothing to merge. Both programs make the same calls and the gap disappears.
- At 64 KB, only about 3,200 calls remain, so the fixed overhead is small compared to the cost of copying 200 MB through the kernel, which is the same for both versions. The difference left comes from the extra work of stdio (an extra `memcpy` and an extra `write` per block), which gives a small advantage to `copy_sys`.

To conclude, `fread()` and `fwrite()` make small-buffer I/O efficient without any extra effort from the programmer. With large, block-aligned buffers, raw `read()` and `write()` are just as fast or a little faster, because the cost of each system call is spread over enough bytes that it doesn't really matter anymore.

Please check [results.txt](results.txt) and the `strace/` folder on the shared repository for the full outputs.

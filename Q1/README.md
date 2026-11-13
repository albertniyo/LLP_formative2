# Question 1 – Three-stage pipeline
For this question, I built a program that runs the same pipeline as the shell would do
for us, but this time using system calls directly:

```
cat /etc/passwd | cut -d: -f7 | sort -u
```

The program reads the users file, cuts the login shell of every user, sorts them and
removes duplicates, and then the parent saves the final output in [result.txt](result.txt).

## Build and run

```bash
gcc -Wall -Wextra main.c -o pipeline
./pipeline
strace -f -tt -o trace.log ./pipeline
strace -f -o trace_filtered.log \
  -e trace=clone,clone3,fork,vfork,execve,pipe,pipe2,dup2,open,openat,read,write,close,wait4 ./pipeline
```

Program output:

```
first 5 lines of result.txt:
/usr/bin/bash
/usr/bin/git-shell
/usr/bin/nologin
total lines: 3
child 8975 (cat) exited with status 0
child 8976 (cut) exited with status 0
child 8977 (sort) exited with status 0
```

Only 3 different login shells exist on my machine, that's why fewer than 5 lines are printed. I also compared the output with `diff` against the real shell pipeline and
they matched.

## Program design

We used three pipes, and each one connects two stages:

| Pipe | Write end held by | Read end held by |
|------|-------------------|------------------|
| `p[0]` | `cat` (stdout) | `cut` (stdin) |
| `p[1]` | `cut` (stdout) | `sort` (stdin) |
| `p[2]` | `sort` (stdout) | parent |

Let's take a look at its logic:


**#1:** the parent creates all three pipes first, then calls `fork()` three times
through the `spawn()` function, one time for each command.


**#2:** inside each child, we use `dup2()` to connect its stdin and stdout to the right
pipe ends, then we close **all six** original pipe descriptors with `close_all_pipes()`
and call `execvp()` to run the command.

**#3:** the parent closes every pipe end except the read end of `p[2]`, reads from it
until EOF, and writes everything into `result.txt` using `open()` and `write()`.

**#4:** the parent opens `result.txt` again, reads it back, prints the first 5 lines and
counts the newlines to get the total number of lines.

**#5:** finally, the parent calls `waitpid()` on each child and prints its exit status.

***Notes:*** For more hands-on do check the [main.c](main.c) under the same repository.

## strace analysis

The excerpts below are taken from `trace.log` (parent PID 5605, children 5606 = `cat`,
5607 = `cut`, 5608 = `sort`). Because `-f` mixes the lines of all processes, strace
splits some calls into `<unfinished ...>` and `<... resumed>` pairs. I merged these
pairs into single lines to make them easy to read, and I left out the dynamic loader
noise like loading libc among others.

### 1. Process creation

```
5605 09:25:17.543202 execve("./pipeline", ["./pipeline"], ...) = 0
5605 09:25:17.547041 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 5606
5605 09:25:17.547279 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 5607
5605 09:25:17.547586 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 5608

5606 09:25:17.548263 execve("/home/kaya4a/.local/bin/cat", ["cat", "/etc/passwd"], ...) = -1 ENOENT
     ...                (execvp tries each $PATH directory in turn)
5606 09:25:17.549852 execve("/usr/bin/cat", ["cat", "/etc/passwd"], ...) = 0
5607 09:25:17.550034 execve("/usr/bin/cut", ["cut", "-d:", "-f7"], ...) = 0
5608 09:25:17.549522 execve("/usr/bin/sort", ["sort", "-u"], ...) = 0
```

`clone`; this is the system call that glibc uses to implement `fork()`. There are no
`CLONE_VM` or `CLONE_FILES` flags, which means the child gets its own copy of the
address space and its own copy of the parent's file descriptor table. `SIGCHLD` tells
the kernel to send that signal to the parent when the child exits. The return value is
the child's PID in the parent, and 0 in the child.

`execve`; this replaces the child's program with `cat`, `cut` or `sort`. The file
descriptor table survives `execve`, that's why the `dup2` redirections done before
still work in the new program. `execvp` searches `$PATH`, so the trace shows some
failed `execve` calls (`ENOENT`) before the one that succeeds in `/usr/bin`.

### 2. Pipe setup and redirection

Parent:

```
5605 09:25:17.546674 pipe2([3, 4], 0) = 0      # p[0]: 3 = read, 4 = write
5605 09:25:17.546914 pipe2([5, 6], 0) = 0      # p[1]: 5 = read, 6 = write
5605 09:25:17.546953 pipe2([7, 8], 0) = 0      # p[2]: 7 = read, 8 = write
...                                            (three clones)
5605 09:25:17.547896 close(3) = 0
5605 09:25:17.547979 close(4) = 0
5605 09:25:17.548087 close(5) = 0
5605 09:25:17.548194 close(6) = 0
5605 09:25:17.548311 close(8) = 0              # keeps only fd 7
```

Child `cut` (5607), which looks like all three children:

```
5607 09:25:17.547658 dup2(3, 0) = 0            # stdin  <- p[0] read end
5607 09:25:17.547753 dup2(6, 1) = 1            # stdout -> p[1] write end
5607 09:25:17.547867 close(3) = 0
5607 09:25:17.547962 close(4) = 0
5607 09:25:17.548056 close(5) = 0
5607 09:25:17.548164 close(6) = 0
5607 09:25:17.548295 close(7) = 0
5607 09:25:17.548488 close(8) = 0
```

Redirections in the other children:

```
5606 09:25:17.547378 dup2(4, 1) = 1            # cat:  stdout -> p[0] write end
5608 09:25:17.547990 dup2(5, 0) = 0            # sort: stdin  <- p[1] read end
5608 09:25:17.548098 dup2(8, 1) = 1            # sort: stdout -> p[2] write end
```

`pipe2`; glibc's `pipe()` calls `pipe2(fds, 0)`. It creates a buffer inside the kernel
and returns two descriptors, `fds[0]` for reading and `fds[1]` for writing. The kernel
always picks the lowest free numbers, which gives us 3/4, 5/6 and 7/8.

`dup2(old, new)`; this makes `new` point to the same open pipe as `old`, and closes
`new` first if it was open. This is how a pipe end becomes fd 0 or fd 1, so that `cat`,
`cut` and `sort` just read and write their normal stdin and stdout without knowing that
pipes are involved at all.

`close`; after `dup2`, the original descriptor is just an extra copy. Closing it drops
one reference to the pipe end, and the kernel frees a pipe end only when no descriptor
in any process points to it anymore.

### 3. File operations (data flow)

```
5606 openat(AT_FDCWD, "/etc/passwd", O_RDONLY) = 3
5606 write(1, "root:x:0:0::/root:/usr/bin/bash\n"..., 1754) = 1754       # cat -> p[0]
5607 read(0, "root:x:0:0::/root:/usr/bin/bash\n"..., 262144) = 1754    # cut <- p[0]
5607 read(0, "", 262144) = 0                                             # EOF: cat exited
5607 write(1, "/usr/bin/bash\n/usr/bin/nologin\n/"..., 506) = 506        # cut -> p[1]
5608 read(0, "/usr/bin/bash\n/usr/bin/nologin\n/"..., 131072) = 506     # sort <- p[1]
5608 read(0, "", 126976) = 0                                            # EOF: cut exited
5608 write(1, "/usr/bin/bash\n/usr/bin/git-shell"..., 50) = 50          # sort -> p[2]

5605 09:25:17.548381 openat(AT_FDCWD, "result.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
5605 09:25:17.548501 read(7, ...) = ? ERESTARTSYS                       # blocked; interrupted by SIGCHLD
5605 09:25:17.558042 read(7, "/usr/bin/bash\n/usr/bin/git-shell"..., 4096) = 50
5605 09:25:17.558474 write(3, "/usr/bin/bash\n/usr/bin/git-shell"..., 50) = 50
5605 09:25:17.558567 read(7, "", 4096) = 0                              # EOF on p[2]
5605 09:25:17.558697 close(7) = 0
5605 09:25:17.558748 close(3) = 0
5605 09:25:17.558856 openat(AT_FDCWD, "result.txt", O_RDONLY) = 3
5605 09:25:17.559224 read(3, "/usr/bin/bash\n/usr/bin/git-shell"..., 4096) = 50
5605 09:25:17.559276 read(3, "", 4096) = 0
5605 09:25:17.559314 close(3) = 0
```

`openat`; glibc's `open()` calls `openat(AT_FDCWD, ...)`. `result.txt` gets fd 3
because fd 3 was freed before, following the lowest free descriptor rule.

`read`; on a pipe, `read` waits until there is data. It returns the number of bytes
read, or **0 when every write end is closed and the buffer is empty**, which means EOF.
The `ERESTARTSYS` line shows the parent's waiting `read(7)` being interrupted by
`SIGCHLD`, and then the kernel restarts it automatically.

`write`; this copies bytes into the pipe buffer, or into the file. If a pipe buffer is
full, `write` waits until the reader empties it, and this is how the stages stay in
step with each other.

### 4. Process termination

```
5606 09:25:17.557437 exit_group(0) = ?
5606 09:25:17.557650 +++ exited with 0 +++
5605 09:25:17.557721 --- SIGCHLD {si_code=CLD_EXITED, si_pid=5606, si_status=0, ...} ---
5607 09:25:17.557801 exit_group(0) = ?
5605 09:25:17.557999 --- SIGCHLD {si_code=CLD_EXITED, si_pid=5607, si_status=0, ...} ---
5608 09:25:17.558760 exit_group(0) = ?
5605 09:25:17.558991 --- SIGCHLD {si_code=CLD_EXITED, si_pid=5608, si_status=0, ...} ---

5605 09:25:17.559357 wait4(5606, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 5606
5605 09:25:17.559428 wait4(5607, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 5607
5605 09:25:17.559483 wait4(5608, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 5608
5605 09:25:17.559546 write(1, "First 5 lines of result.txt:\n/us"..., 209) = 209
5605 09:25:17.559590 exit_group(0) = ?
```

`exit_group`; this is where each child ends. The kernel closes all of its descriptors,
and this is what gives EOF to the next stage. The child stays a zombie until the parent
collects it.

`wait4`; glibc's `waitpid(pid, &status, 0)` calls `wait4(pid, &status, 0, NULL)`. It
collects the zombie and fills `status`, then the program reads `status` with
`WIFEXITED`/`WEXITSTATUS`, or with `WIFSIGNALED`/`WTERMSIG` if the child was killed by
a signal.

In this run, stdout was redirected to `/dev/null`, which makes it fully buffered. That's
why all the `printf` output of the parent shows up as one `write(1, ...)` at the end.

## File descriptor tables

After the three `pipe2` calls, the parent (and so every child at `fork` time) has:

| fd | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|----|---|---|---|---|---|---|---|---|---|
| refers to | tty | tty | tty | p0 R | p0 W | p1 R | p1 W | p2 R | p2 W |

After `dup2` and `close`, just before `execve` (or before reading, for the parent):

| Process | fd 0 (stdin) | fd 1 (stdout) | fd 2 | other fds |
|---------|--------------|---------------|------|-----------|
| `cat`  | tty | **p0 W** | tty | none |
| `cut`  | **p0 R** | **p1 W** | tty | none |
| `sort` | **p1 R** | **p2 W** | tty | none |
| parent | tty | tty | tty | 7 = p2 R (later 3 = `result.txt`) |

Now each pipe end is held by exactly one process, so each pipe has one writer and one
reader.

### What happens if unused pipe ends are not closed

A `read` on a pipe returns 0 (EOF) only when **every** descriptor of the write end, in
every process, has been closed. If a process keeps an unused write end open, we get
problems like these:

- If `cut` or `sort` kept their copy of the write end of `p[0]` (fd 4), `cut` would never
  see EOF, even after `cat` exits. `cut` would wait forever in `read(0, ...)`, so `sort`
  would never get EOF either, and the whole pipeline would hang.
- If the parent kept fd 8 (the write end of `p[2]`), its own `read(7)` would never return
  0. The parent would deadlock, waiting for EOF from a pipe that it is holding open by
  itself. The same thing happens if the parent keeps fd 4 or fd 6, because the stages
  before it never get EOF.
- Unused **read** ends left open are less serious, but still wrong. A writer whose reader
  has died would not get `SIGPIPE`/`EPIPE`, and could wait forever once the pipe buffer
  is full. They also leak descriptors into the programs run by `execvp`.

## How the parent detects EOF and collects exit statuses

- **EOF:** when `sort` finishes, it writes its output and exits, and the kernel closes its
fd 1, which is the only remaining write end of `p[2]` because the parent closed fd 8
and the other children closed their copies. The next `read(7, ...)` of the parent then
returns **0** (`read(7, "", 4096) = 0` in the trace), and this ends the copy loop. The
same thing happens along the whole chain: `cat` exits so `cut` reads 0, then `cut`
exits so `sort` reads 0.

- **Exit statuses:** every child that ends becomes a zombie and the parent receives
`SIGCHLD`. The parent calls `waitpid()` (the `wait4` system call) on each saved PID one
after the other. The kernel returns the packed status, and the parent reads it with
`WIFEXITED`/`WEXITSTATUS`. All three children exited normally with status 0.

Check [`trace.log`](/trace.log) and [trace_filtered.log](./trace_filtered.log) on for the full traces.

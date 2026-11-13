#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define out "result.txt"

int p[3][2];

void die(const char *msg) {
    perror(msg);
    exit(1);
}

void close_all_pipes() {
    for (int i = 0; i < 3; i++) {
        close(p[i][0]);
        close(p[i][1]);
    }
}

/* fork a child, connect its stdin to in_fd and its stdout to out_fd,
 * close every original pipe end and then run argv with execvp.
 * in_fd is -1 for the first stage, which keeps the normal stdin
 */
pid_t spawn(int in_fd, int out_fd, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        die("fork");
    }

    if (pid == 0) {
        if (in_fd != -1 && dup2(in_fd, STDIN_FILENO) < 0) {
            die("dup2 stdin");
        }
        if (dup2(out_fd, STDOUT_FILENO) < 0) {
            die("dup2 stdout");
        }
        close_all_pipes();
        execvp(argv[0], argv);
        die("execvp");
    }
    return pid;
}

/* runs cat /etc/passwd | cut -d: -f7 | sort -u
 * p[0] goes from cat to cut, p[1] from cut to sort and p[2] from sort
 * back to the parent, which saves it in result.txt, prints the first
 * 5 lines, counts all lines and then waits for the three children
 */
int main() {
    char *cat_argv[] = {"cat", "/etc/passwd", NULL};
    char *cut_argv[] = {"cut", "-d:", "-f7", NULL};
    char *sort_argv[] = {"sort", "-u", NULL};
    const char *names[3] = {"cat", "cut", "sort"};

    pid_t pids[3];
    char buf[4096];
    ssize_t n;
    int fd;
    int lines = 0;

    for (int i = 0; i < 3; i++) {
        if (pipe(p[i]) < 0) {
            die("pipe");
        }
    }

    pids[0] = spawn(-1, p[0][1], cat_argv);
    pids[1] = spawn(p[0][0], p[1][1], cut_argv);
    pids[2] = spawn(p[1][0], p[2][1], sort_argv);

    close(p[0][0]);
    close(p[0][1]);
    close(p[1][0]);
    close(p[1][1]);
    close(p[2][1]);

    fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        die("open " out);
    }

    while ((n = read(p[2][0], buf, sizeof buf)) > 0) {
        if (write(fd, buf, n) != n) {
            die("write");
        }
    }
    if (n < 0) {
        die("read pipe");
    }

    close(p[2][0]);
    close(fd);

    fd = open(out, O_RDONLY);
    if (fd < 0) {
        die("open " out);
    }

    printf("first 5 lines of %s:\n", out);
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (lines < 5) {
                putchar(buf[i]);
            }
            if (buf[i] == '\n') {
                lines++;
            }
        }
    }
    if (n < 0) {
        die("read file");
    }

    close(fd);
    printf("total lines: %d\n", lines);

    for (int i = 0; i < 3; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            die("waitpid");
        }
        if (WIFEXITED(status)) {
            printf("child %d (%s) exited with status %d\n",
                   pids[i], names[i], WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("child %d (%s) killed by signal %d\n",
                   pids[i], names[i], WTERMSIG(status));
        }
    }

    return 0;
}

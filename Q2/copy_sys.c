#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

double elapsed(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

/* write all n bytes of buf to fd.
 * write() can write fewer bytes than asked, so it loops until done.
 * returns 0 on success and -1 on error
 */
int write_all(int fd, const char *buf, ssize_t n) {
    ssize_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w < 0) {
            return -1;
        }
        done += w;
    }
    return 0;
}

/* copy_sys <source> <destination> <buffer_size>
 * copies the file with open(), read(), write() and close() only,
 * and times the copy from opening the source to closing both files
 */
int main(int argc, char *argv[]) {
    struct timespec t0, t1;
    long long total = 0;
    int status = 0;
    char *end;
    ssize_t n;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <source> <destination> <buffer_size>\n", argv[0]);
        return 1;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    errno = 0;
    long bufsize = strtol(argv[3], &end, 10);
    if (errno != 0 || *end != '\0' || bufsize <= 0) {
        fprintf(stderr, "invalid buffer size: %s\n", argv[3]);
        return 1;
    }

    char *buf = malloc(bufsize);
    if (buf == NULL) {
        perror("malloc");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    int in = open(src, O_RDONLY);
    if (in < 0) {
        perror(src);
        free(buf);
        return 1;
    }

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        perror(dst);
        close(in);
        free(buf);
        return 1;
    }

    while ((n = read(in, buf, bufsize)) > 0) {
        if (write_all(out, buf, n) < 0) {
            perror("write");
            status = 1;
            break;
        }
        total += n;
    }
    if (n < 0) {
        perror("read");
        status = 1;
    }

    if (close(in) < 0) {
        perror("close source");
        status = 1;
    }
    if (close(out) < 0) {
        perror("close destination");
        status = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(buf);

    if (status == 0) {
        printf("copy_sys: copied %lld bytes with %ld-byte buffer in %.6f seconds\n",
               total, bufsize, elapsed(t0, t1));
    }

    return status;
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

double elapsed(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

/* copy_stdio <source> <destination> <buffer_size>
 * copies the file with fopen(), fread(), fwrite() and fclose() only,
 * and times the copy from opening the source to closing both files.
 * fclose() flushes the last buffered block and checked as well
 */
int main(int argc, char *argv[]) {
    struct timespec t0, t1;
    long long total = 0;
    int status = 0;
    char *end;
    size_t n;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <source> <destination> <buffer_size>\n", argv[0]);
        return 1;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    errno = 0;
    long bufsize = strtol(argv[3], &end, 10);
    if (errno != 0 || *end != '\0' || bufsize <= 0) {
        fprintf(stderr, "Invalid buffer size: %s\n", argv[3]);
        return 1;
    }

    char *buf = malloc(bufsize);
    if (buf == NULL) {
        perror("malloc");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    FILE *in = fopen(src, "rb");
    if (in == NULL) {
        perror(src);
        free(buf);
        return 1;
    }

    FILE *out = fopen(dst, "wb");
    if (out == NULL) {
        perror(dst);
        fclose(in);
        free(buf);
        return 1;
    }

    while ((n = fread(buf, 1, bufsize, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("fwrite");
            status = 1;
            break;
        }
        total += n;
    }
    if (ferror(in)) {
        perror("fread");
        status = 1;
    }

    if (fclose(in) == EOF) {
        perror("fclose source");
        status = 1;
    }
    if (fclose(out) == EOF) {
        perror("fclose destination");
        status = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(buf);

    if (status == 0) {
        printf("copy_stdio: copied %lld bytes with %ld-byte buffer in %.6f seconds\n",
               total, bufsize, elapsed(t0, t1));
    }

    return status;
}

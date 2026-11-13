#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char **queue_files;
int queue_len;
int queue_next;
pthread_mutex_t queue_mutex;

FILE *report;
pthread_mutex_t report_mutex;

long long grand_matching;
long long grand_lines;
int files_done;
pthread_mutex_t totals_mutex;

const char *pattern;

struct worker_arg {
    int id;
    int files_processed;
};

double elapsed(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

const char *queue_take() {
    const char *name = NULL;
    pthread_mutex_lock(&queue_mutex);
    if (queue_next < queue_len) {
        name = queue_files[queue_next++];
    }
    pthread_mutex_unlock(&queue_mutex);
    return name;
}

/* read one full line with fgets() into *buf, growing the buffer when a
 * line is longer than it, so a match is never split between two chunks
 * returns the line length, or 0 at the end of the file
 */
size_t read_line(FILE *fp, char **buf, size_t *cap) {
    size_t len = 0;
    while (1) {
        if (*cap - len < 2) {
            size_t new_cap = *cap * 2;
            char *p = realloc(*buf, new_cap);
            if (p == NULL) {
                perror("realloc");
                exit(1);
            }
            *buf = p;
            *cap = new_cap;
        }
        if (fgets(*buf + len, (int)(*cap - len), fp) == NULL) {
            return len;
        }
        len += strlen(*buf + len);
        if ((*buf)[len - 1] == '\n') {
            return len;
        }
    }
}

/* count all lines of one file and the lines that contain pattern.
 * returns 0 on success and -1 if the file could not be opened or read
 */
int scan_file(const char *name, long long *matching, long long *lines) {
    FILE *fp = fopen(name, "r");
    if (fp == NULL) {
        perror(name);
        return -1;
    }

    size_t cap = 4096;
    char *line = malloc(cap);
    if (line == NULL) {
        perror("malloc");
        fclose(fp);
        return -1;
    }

    *matching = 0;
    *lines = 0;
    while (read_line(fp, &line, &cap) > 0) {
        (*lines)++;
        if (strstr(line, pattern) != NULL) {
            (*matching)++;
        }
    }

    int rc = 0;
    if (ferror(fp)) {
        perror(name);
        rc = -1;
    }
    free(line);
    fclose(fp);
    return rc;
}

/* each worker takes file names from the queue until it is empty,
 * scans the file outside all locks, then writes one report line under
 * report_mutex and adds its counts to the totals under totals_mutex
 */
void *worker(void *argp) {
    struct worker_arg *arg = argp;
    const char *name;

    while ((name = queue_take()) != NULL) {
        long long matching, lines;
        if (scan_file(name, &matching, &lines) != 0) {
            continue;
        }

        pthread_mutex_lock(&report_mutex);
        fprintf(report, "%s: %lld matching lines out of %lld (thread %d)\n",
                name, matching, lines, arg->id);
        pthread_mutex_unlock(&report_mutex);

        pthread_mutex_lock(&totals_mutex);
        grand_matching += matching;
        grand_lines += lines;
        files_done++;
        pthread_mutex_unlock(&totals_mutex);

        arg->files_processed++;
    }
    return NULL;
}

/* run the pool of nthreads workers and wait for all of them.
 * with one thread the worker runs directly in the main thread, which is
 * the single-threaded baseline. returns -1 if no thread could start
 */
int run_pool(pthread_t *threads, struct worker_arg *args, long nthreads) {
    long started = 0;

    if (nthreads == 1) {
        args[0].id = 1;
        worker(&args[0]);
        return 0;
    }

    for (long i = 0; i < nthreads; i++) {
        args[i].id = (int)i + 1;
        int err = pthread_create(&threads[i], NULL, worker, &args[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            break;
        }
        started++;
    }
    for (long i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }

    return started == 0 ? -1 : 0;
}

/* logscan <pattern> <report.txt> <number_of_threads> file1.log 
 * counts the lines that contain pattern in every file, writes one line
 * per file and the grand total to the report, and prints the summary
 */
int main(int argc, char *argv[]) {
    struct timespec t0, t1;
    char *end;

    if (argc < 5) {
        fprintf(stderr, "usage: %s <pattern> <report.txt> <number_of_threads> file1.log [file2.log ...]\n",
                argv[0]);
        return 1;
    }

    pattern = argv[1];
    const char *report_name = argv[2];
    errno = 0;
    long nthreads = strtol(argv[3], &end, 10);
    if (errno != 0 || *end != '\0' || nthreads < 1 || nthreads > 1024) {
        fprintf(stderr, "invalid number of threads: %s\n", argv[3]);
        return 1;
    }
    queue_files = &argv[4];
    queue_len = argc - 4;

    if (pattern[0] == '\0') {
        fprintf(stderr, "pattern must not be empty\n");
        return 1;
    }

    report = fopen(report_name, "w");
    if (report == NULL) {
        perror(report_name);
        return 1;
    }

    pthread_mutex_init(&queue_mutex, NULL);
    pthread_mutex_init(&report_mutex, NULL);
    pthread_mutex_init(&totals_mutex, NULL);

    pthread_t *threads = malloc(nthreads * sizeof *threads);
    struct worker_arg *args = calloc(nthreads, sizeof *args);
    if (threads == NULL || args == NULL) {
        perror("malloc");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (run_pool(threads, args, nthreads) != 0) {
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    fprintf(report, "total matching lines across %d files: %lld\n", files_done, grand_matching);
    printf("total matching lines across %d files: %lld\n", files_done, grand_matching);
    if (fclose(report) == EOF) {
        perror(report_name);
    }

    printf("total lines: %lld, skipped files: %d, threads: %ld\n",
           grand_lines, queue_len - files_done, nthreads);
    for (long i = 0; i < nthreads; i++) {
        printf("thread %d processed %d file(s)\n", args[i].id, args[i].files_processed);
    }
    printf("elapsed time: %.6f seconds\n", elapsed(t0, t1));

    pthread_mutex_destroy(&queue_mutex);
    pthread_mutex_destroy(&report_mutex);
    pthread_mutex_destroy(&totals_mutex);
    free(threads);
    free(args);

    return 0;
}

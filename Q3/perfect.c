#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define lim 100000
#define th_num  8
#define segmen (lim / th_num )
#define max 64

struct segment {
    int id;
    long start;
    long end;
    long long iterations;
    double seconds;
};

pthread_mutex_t lock;
int total_count;
long results[max];
int num_results;

double elapsed(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

/* sum the proper divisors of n by testing only i up to sqrt(n),
 * because every divisor i below the square root has a partner n / i
 * above it. iterations counts how many times the loop ran
 */
int is_perfect(long n, long long *iterations) {
    if (n < 2) {
        return 0;
    }

    long sum = 1;
    long root = (long)sqrt((double)n);
    for (long i = 2; i <= root; i++) {
        if (n % i == 0) {
            sum += i;
            if (i != n / i) {
                sum += n / i;
            }
        }
    }
    *iterations += root > 1 ? root - 1 : 0;
    return sum == n;
}

/* each thread checks its own segment and counts in local_count.
 * a found number is added to results[] under the lock, and the
 * shared total_count is updated only once at the end of the segment
 */
void *work(void *arg) {
    struct segment *seg = arg;
    struct timespec t0, t1;
    int local_count = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    seg->iterations = 0;
    for (long n = seg->start; n <= seg->end; n++) {
        if (is_perfect(n, &seg->iterations)) {
            local_count++;
            pthread_mutex_lock(&lock);
            results[num_results++] = n;
            pthread_mutex_unlock(&lock);
        }
    }

    pthread_mutex_lock(&lock);
    total_count += local_count;
    pthread_mutex_unlock(&lock);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    seg->seconds = elapsed(t0, t1);
    return NULL;
}

int single_threaded(long *found) {
    long long iterations = 0;
    int count = 0;
    for (long n = 1; n <= lim; n++) {
        if (is_perfect(n, &iterations)) {
            found[count++] = n;
        }
    }
    return count;
}

int cmp_long(const void *a, const void *b) {
    long x = *(const long *)a;
    long y = *(const long *)b;
    return (x > y) - (x < y);
}

void print_work(struct segment *segs, double single_time, double multi_time) {
    long long total_iter = 0;
    for (int i = 0; i < th_num ; i++) {
        total_iter += segs[i].iterations;
    }

    printf("\nThread  Range              Loop iterations  Share   Time (s)\n");
    for (int i = 0; i < th_num ; i++) {
        printf("%6d  %6ld - %6ld  %15lld  %5.1f%%  %.6f\n",
               segs[i].id, segs[i].start, segs[i].end, segs[i].iterations,
               100.0 * segs[i].iterations / total_iter, segs[i].seconds);
    }
    printf("Speedup: %.2fx\n", single_time / multi_time);
}

/* perfect [-v]
 * finds the perfect numbers in 1 .. lim first with one thread and then
 * with th_num  threads, prints both times and checks that the two
 * results are the same. -v also prints how much work each thread did
 */
int main(int argc, char *argv[]) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    long single_results[max];
    pthread_t threads[th_num ];
    struct segment segs[th_num ];
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    int single_count = single_threaded(single_results);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double single_time = elapsed(t0, t1);

    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < th_num ; i++) {
        segs[i].id = i;
        segs[i].start = (long)i * segmen + 1;
        segs[i].end = (long)(i + 1) * segmen;
        int err = pthread_create(&threads[i], NULL, work, &segs[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return 1;
        }
    }
    for (int i = 0; i < th_num ; i++) {
        pthread_join(threads[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double multi_time = elapsed(t0, t1);

    pthread_mutex_destroy(&lock);

    qsort(results, num_results, sizeof results[0], cmp_long);

    printf("perfect numbers found:");
    for (int i = 0; i < num_results; i++) {
        printf(" %ld", results[i]);
    }
    printf("\n");
    printf("synchronized total number of perfect numbers between 1 and %d is %d\n",
           lim, total_count);
    printf("single-threaded time: %.6f seconds\n", single_time);
    printf("multi-threaded time:  %.6f seconds\n", multi_time);

    if (single_count != total_count ||
        memcmp(single_results, results, single_count * sizeof results[0]) != 0) {
        fprintf(stderr, "single and multi-threaded results differ\n");
    }

    if (verbose) {
        print_work(segs, single_time, multi_time);
    }

    return 0;
}

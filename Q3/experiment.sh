#!/usr/bin/env bash
# builds perfect.c, runs it 5 times, records the work of each thread (-v)
# and repeats the run pinned to different cpu sets. output goes to results.txt
set -euo pipefail
cd "$(dirname "$0")"

gcc -Wall -Wextra -O2 -pthread -o perfect perfect.c -lm

{
    echo "--- program output --"
    ./perfect

    echo
    echo "-- 5 runs (single-threaded / multi-threaded seconds) ---"
    for i in 1 2 3 4 5; do
        ./perfect | awk '/time/ {printf "%s ", $(NF-1)} END {print ""}'
    done | tee /tmp/perfect_runs.txt
    awk '{s+=$1; m+=$2} END {printf "AVERAGE %.6f %.6f speedup %.2fx\n", s/NR, m/NR, s/m}' \
        /tmp/perfect_runs.txt

    echo
    echo "--- per-thread work (-v) ---"
    ./perfect -v | tail -n 11

    echo
    echo "--- pinned to CPU sets with taskset (3 runs each, averaged) ---"
    for set in 0 0,1 0,2 0-3; do
        for i in 1 2 3; do
            taskset -c "$set" ./perfect | awk '/time/ {printf "%s ", $(NF-1)} END {print ""}'
        done | awk -v set="$set" '{s+=$1; m+=$2}
            END {printf "cpus=%-4s single %.6f  multi %.6f  speedup %.2fx\n", set, s/NR, m/NR, s/m}'
    done
} | tee results.txt

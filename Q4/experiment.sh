#!/usr/bin/env bash
# builds logscan, generates the logs if needed and runs every experiment.
# output goes to results.txt and the last report is kept as report.txt
set -euo pipefail
cd "$(dirname "$0")"

PATTERN=ERROR
RUNS=3
gcc -Wall -Wextra -O2 -pthread -o logscan logscan.c
[ -d logs ] || ./gen_logs.sh

FILES=(logs/*.log)
LARGEST_FIRST=($(ls -S logs/*.log))
SMALLEST_FIRST=($(ls -Sr logs/*.log))

# evicts the logs from the page cache, works without root for clean pages
drop_cache() {
    python3 -c 'import os, sys
for f in sys.argv[1:]:
    fd = os.open(f, os.O_RDONLY); os.fsync(fd)
    os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED); os.close(fd)' "${FILES[@]}"
}

# avg_time <label> <threads> <cold:0|1> <files...> prints the average of RUNS runs
avg_time() {
    local label=$1 threads=$2 cold=$3; shift 3
    local times=()
    for run in $(seq 1 $RUNS); do
        [ "$cold" = 1 ] && drop_cache
        times+=("$(./logscan "$PATTERN" report.txt "$threads" "$@" | awk '/elapsed/ {print $3}')")
    done
    printf '%-16s threads=%d  runs: %s  avg: %s\n' "$label" "$threads" "${times[*]}" \
        "$(printf '%s\n' "${times[@]}" | awk '{s+=$1} END {printf "%.6f", s/NR}')"
}

{
    echo "--- files ---"
    ls -l "${FILES[@]}" | awk '{printf "%-20s %9d bytes\n", $9, $5}'
    du -cb "${FILES[@]}" | tail -1

    echo
    echo "--- sample run (8 threads, with a missing file) ---"
    ./logscan "$PATTERN" report.txt 8 "${FILES[@]}" missing.log 2>&1
    echo "--- report.txt ---"
    cat report.txt

    echo
    echo "--- Verification against grep ---"
    ./logscan "$PATTERN" report.txt 4 "${FILES[@]}" > /dev/null
    grep -c "$PATTERN" "${FILES[@]}" | sort > /tmp/grep_counts.txt
    grep ' matching lines out of ' report.txt | awk -F'[: ]' '{print $1 ":" $3}' | sort > /tmp/logscan_counts.txt
    if diff /tmp/grep_counts.txt /tmp/logscan_counts.txt; then
        echo "Per-file matching counts identical to grep -c"
    fi
    echo "grep total:    $(awk -F: '{s+=$2} END {print s}' /tmp/grep_counts.txt)"
    echo "logscan total: $(tail -1 report.txt | awk '{print $NF}')"
    echo "wc -l total:   $(cat "${FILES[@]}" | wc -l)"

    echo
    echo "--- warm page cache (files already in RAM) ---"
    cat "${FILES[@]}" > /dev/null
    for t in 1 2 4 8; do avg_time warm "$t" 0 "${FILES[@]}"; done

    echo
    echo "--- cold page cache (evicted before every run) ---"
    for t in 1 2 4 8; do avg_time cold "$t" 1 "${FILES[@]}"; done

    echo
    echo "--- queue order, warm cache ---"
    cat "${FILES[@]}" > /dev/null
    for t in 2 4; do
        avg_time alphabetical "$t" 0 "${FILES[@]}"
        avg_time largest-first "$t" 0 "${LARGEST_FIRST[@]}"
        avg_time smallest-first "$t" 0 "${SMALLEST_FIRST[@]}"
    done
    avg_time cache.log-only 1 0 logs/cache.log

    echo
    echo "--- mutex contention: futex calls with 8 threads ---"
    strace -f -c -e trace=futex,openat,read -o /tmp/logscan_strace.txt \
        ./logscan "$PATTERN" report.txt 8 "${FILES[@]}" > /dev/null
    cat /tmp/logscan_strace.txt

    ./logscan "$PATTERN" report.txt 4 "${FILES[@]}" > /dev/null
} 2>&1 | tee results.txt

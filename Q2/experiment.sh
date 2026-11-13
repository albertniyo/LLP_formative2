#!/usr/bin/env bash
# builds both copy programs, creates the 100 mb test file, times every
# version and buffer size three times, checks every copy with cmp and
# saves the strace -c summaries and then results go to results.txt and strace/
set -euo pipefail
cd "$(dirname "$0")"

SIZES=(512 4096 65536)
RUNS=3

gcc -Wall -Wextra -O2 -o copy_sys copy_sys.c
gcc -Wall -Wextra -O2 -o copy_stdio copy_stdio.c

[ -f big.bin ] || dd if=/dev/urandom of=big.bin bs=1M count=100 status=none
sha256sum big.bin > big.sha256
cat big.bin > /dev/null

{
    echo "--- Timing (each run verified with cmp) ---"
    for prog in copy_sys copy_stdio; do
        for size in "${SIZES[@]}"; do
            times=()
            for run in $(seq 1 $RUNS); do
                rm -f out.bin
                line=$(./$prog big.bin out.bin "$size")
                echo "$line"
                cmp big.bin out.bin
                times+=("$(awk '{print $(NF-1)}' <<< "$line")")
            done
            avg=$(printf '%s\n' "${times[@]}" | awk '{s+=$1} END {printf "%.6f", s/NR}')
            echo "AVERAGE $prog $size $avg"
        done
    done

    echo
    echo "--- sha256sum check of the last copy ---"
    sha256sum big.bin out.bin

    echo
    echo "--- Error handling ---"
    ./copy_sys missing.bin out.bin 4096 2>&1 || true
    ./copy_stdio missing.bin out.bin 4096 2>&1 || true
    ./copy_sys big.bin /no_such_dir/out.bin 4096 2>&1 || true
    ./copy_stdio big.bin /no_such_dir/out.bin 4096 2>&1 || true
} | tee results.txt

mkdir -p strace
for prog in copy_sys copy_stdio; do
    for size in "${SIZES[@]}"; do
        rm -f out.bin
        strace -c -o "strace/${prog}_${size}.txt" ./$prog big.bin out.bin "$size" \
            | tee -a results.txt
        cmp big.bin out.bin
    done
done
rm -f out.bin

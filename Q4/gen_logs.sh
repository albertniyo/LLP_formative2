#!/usr/bin/env bash
# generates 12 synthetic log files (1 mb to 50 mb, 209 mb in total) in logs/
# about 5 % of the lines are level=ERROR and each file uses a fixed seed
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p logs

declare -A SIZES_MB=(
    [auth]=25 [billing]=3 [cache]=50 [db]=10 [gateway]=1 [mail]=40
    [metrics]=5 [payments]=15 [queue]=2 [scheduler]=30 [search]=8 [web]=20
)

seed=1
for name in $(printf '%s\n' "${!SIZES_MB[@]}" | sort); do
    target=$(( SIZES_MB[$name] * 1024 * 1024 ))
    awk -v seed="$seed" -v target="$target" -v svc="$name" 'BEGIN {
        srand(seed)
        split("request completed|cache miss|user login|connection opened|connection closed|job scheduled|payment authorised|config reloaded", info, "|")
        split("connection refused|timeout talking to upstream|disk quota exceeded|null pointer in handler|invalid token|deadlock detected", err, "|")
        split("slow query|retrying request|high memory usage|deprecated API call", warn, "|")
        bytes = 0; t = 1794556800
        while (bytes < target) {
            t += int(rand() * 3)
            r = rand()
            if (r < 0.05)      { lvl = "ERROR"; msg = err[int(rand() * 6) + 1] }
            else if (r < 0.15) { lvl = "WARN";  msg = warn[int(rand() * 4) + 1] }
            else if (r < 0.30) { lvl = "DEBUG"; msg = info[int(rand() * 8) + 1] }
            else               { lvl = "INFO";  msg = info[int(rand() * 8) + 1] }
            line = sprintf("%s.%03d %s-%02d pid=%d level=%s msg=\"%s\" latency_ms=%d req_id=%08x",
                           strftime("%Y-%m-%dT%H:%M:%S", t, 1), int(rand() * 1000), svc,
                           int(rand() * 16), 1000 + int(rand() * 9000), lvl, msg,
                           int(rand() * 2000), int(rand() * 4294967295))
            print line
            bytes += length(line) + 1
        }
    }' > "logs/$name.log"
    seed=$((seed + 1))
done
ls -l logs

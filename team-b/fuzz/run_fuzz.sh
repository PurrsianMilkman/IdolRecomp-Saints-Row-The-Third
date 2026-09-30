#!/usr/bin/env bash
# Seed and run every libFuzzer harness for a bounded time.
#   fuzz/run_fuzz.sh <fuzz build dir> <seconds per target> [work dir]
# Exit status 1 if any target reported a finding; crash inputs are left in
# <work dir>/artifacts/<target>/. Corpora live in <work dir>/corpus/<target>/.
set -u
BUILD=${1:?build dir}; SECS=${2:?seconds}; WORK=${3:-fuzz-work}
mkdir -p "$WORK/corpus" "$WORK/artifacts" "$WORK/logs"
for e in "$BUILD"/seedcap_*; do
    CRREISH_SEED_DIR="$WORK/corpus" "$e" >/dev/null 2>&1 || echo "seed capture failed: $e"
done
TARGETS=$(cd "$BUILD" && ls fuzz_* | grep -v '\.' )
JOBS=${FUZZ_JOBS:-$(nproc)}
run_one() {
    t=$1; name=${t#fuzz_}
    mkdir -p "$WORK/corpus/$name" "$WORK/artifacts/$name"
    "$BUILD/$t" "$WORK/corpus/$name" -max_total_time="$SECS" -timeout=10 -rss_limit_mb=2048 \
        -malloc_limit_mb=1024 -print_final_stats=1 -artifact_prefix="$WORK/artifacts/$name/" \
        > "$WORK/logs/$name.log" 2>&1
    rc=$?
    execs=$(grep -m1 'stat::number_of_executed_units' "$WORK/logs/$name.log" | awk '{print $2}')
    printf '%-12s rc=%-3s execs=%-10s corpus=%s\n' "$name" "$rc" "${execs:-?}" "$(ls "$WORK/corpus/$name" | wc -l)"
    return $rc
}
export -f run_one; export BUILD SECS WORK
echo "$TARGETS" | xargs -P "$JOBS" -I{} bash -c 'run_one {}' ; status=$?
found=$(find "$WORK/artifacts" -type f | wc -l)
echo "findings: $found"
[ "$status" -eq 0 ] && [ "$found" -eq 0 ]

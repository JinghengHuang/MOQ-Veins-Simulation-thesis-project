#!/bin/bash
# Reproduce every result in findings/REPORT.md from a clean checkout.
#
#   ./scripts/run-all.sh [result-dir] [num-seeds]
#
# Runs, in order:
#   1. build            - INET, Simu5G and MoQVeinsSim (the forks carry required bug fixes)
#   2. comparison       - 7 protocol configs x N seeds, urban AND highway  (RQ2, RQ3)
#   3. window sweep     - MoQ partial vs reliable across 6 flow-control windows (RQ3)
#   4. verification     - asserts every run passed the silent-loss gate
#
# THE SILENT-LOSS GATE: INET's QUIC silently discards writes once its send queue is full. A run
# with quicSendRejected > 0 threw away part of its offered load and its latency/throughput numbers
# are meaningless -- a protocol can "win" simply by dropping the traffic. Two earlier versions of
# this comparison were invalidated exactly this way. Any run failing the gate is reported loudly
# below and must not be used.

OUT=${1:-results}
SEEDS=${2:-5}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

OMNETPP=/home/jhuang/omnetpp/omnetpp-6.3.0
INET=/home/jhuang/thesiswork/inet-4.6.0
SIMU5G=/home/jhuang/thesiswork/simu5g-git
MOQ=$ROOT/MoQVeinsSim

echo "=== 0. services ==="
"$HERE/start-services.sh" || exit 1

echo
echo "=== 1. build ==="
source "$OMNETPP/setenv" -q >/dev/null 2>&1
for p in "$INET" "$SIMU5G" "$MOQ"; do
    printf '  %-20s ' "$(basename "$p")"
    if make -C "$p" MODE=release -j"$(nproc)" > "/tmp/build_$(basename "$p").log" 2>&1; then
        echo "ok"
    else
        echo "FAILED - see /tmp/build_$(basename "$p").log"
        exit 1
    fi
done

echo
echo "=== 2. protocol comparison ($SEEDS seeds x 7 configs x 2 scenarios) ==="
for scenario in urban highway; do
    "$HERE/run-comparison.sh" "$OUT/$scenario" "$SEEDS" "$scenario" | grep -v "^$"
done

echo
echo "=== 3. window sweep (single seed; the endpoints differ by an order of magnitude) ==="
cd "$MOQ/simulations" || exit 1
NED="$MOQ/src:.:$INET/src:/home/jhuang/thesiswork/veins-git/src/veins:/home/jhuang/thesiswork/veins-git/subprojects/veins_inet/src/veins_inet:$SIMU5G/src"
LIBS="-l $INET/src/INET -l /home/jhuang/thesiswork/veins-git/src/veins -l /home/jhuang/thesiswork/veins-git/subprojects/veins_inet/src/veins_inet -l $SIMU5G/src/simu5g -l $MOQ/src/MoQVeinsSim"
mkdir -p "$OUT/sweep"
for cfg in MOQ_Partial_Window MOQ_SW_Window; do
    for r in 0 1 2 3 4 5; do
        timeout 1800 opp_run -u Cmdenv -c "$cfg" -r "$r" -f omnetpp.ini \
            --result-dir="$OUT/sweep" -n "$NED" $LIBS > "$OUT/sweep/${cfg}_$r.log" 2>&1
        echo "  $cfg run $r -> $(grep -oE 'limit reached|<!> Error: [A-Za-z_]+' "$OUT/sweep/${cfg}_$r.log" | head -1)"
    done
done

echo
echo "=== 4. verification: silent-loss gate ==="
bad=0
total=0
while IFS= read -r sca; do
    total=$((total + 1))
    rej=$(grep -h quicSendRejected "$sca" 2>/dev/null | awk '{t+=$NF} END{print t+0}')
    if [ "${rej:-0}" -ne 0 ]; then
        echo "  FAIL quicSendRejected=$rej  $sca"
        bad=$((bad + 1))
    fi
done < <(find "$OUT" -name '*.sca')
echo "  checked $total runs, $bad failed the gate"
[ "$bad" -eq 0 ] || { echo "REFUSING to report: some runs discarded load."; exit 1; }

echo
echo "=== 5. results ==="
for scenario in urban highway; do
    echo
    echo "######## $scenario ########"
    python3 "$HERE/aggregate_seeds.py" "$OUT/$scenario"
done
echo
echo "######## window sweep ########"
python3 "$MOQ/simulations/analyze_sweep.py" "$OUT/sweep"

echo
echo "ALL DONE"

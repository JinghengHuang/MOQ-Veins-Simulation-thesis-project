#!/bin/bash
# Regenerate the 300kB operating-point comparison data (the TESTED-AND-REJECTED alternative to the
# 128kB canonical operating point). Writes to results/bdp300/{urban,highway}/seedN/, a folder the
# main pipeline (run-comparison.sh / run-all.sh) never clears -- so this data survives a full re-run
# of the protocol comparison. Run it once after run-all.sh; the 128kB canonical data lives in
# results/{urban,highway} and is produced by run-comparison.sh as usual.
#
#   ./scripts/run-bdp300.sh [num-seeds]
#
# Why this exists: at 300kB the safety track misses ~9.5% of BBox deadlines (vs 0.2% at 128kB) with
# large run-to-run variance, because 300kB sits just below the ~350kB latency knee. Keeping the data
# lets the "we tested a larger window and it was worse" claim rest on measurement, not assertion.
# See findings/moq-operating-envelope.md and the "operating point: 128kB vs 300kB" chart in MOQ.anf.

SEEDS=${1:-5}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

OMNETPP=/home/jhuang/omnetpp/omnetpp-6.3.0
INET=/home/jhuang/thesiswork/inet-4.6.0
SIMU5G=/home/jhuang/thesiswork/simu5g-git
MOQ=$ROOT/MoQVeinsSim

"$HERE/start-services.sh" || exit 1
source "$OMNETPP/setenv" -q >/dev/null 2>&1

cd "$MOQ/simulations" || exit 1
NED="$MOQ/src:.:$INET/src:/home/jhuang/thesiswork/veins-git/src/veins:/home/jhuang/thesiswork/veins-git/subprojects/veins_inet/src/veins_inet:$SIMU5G/src"
LIBS="-l $INET/src/INET -l /home/jhuang/thesiswork/veins-git/src/veins -l /home/jhuang/thesiswork/veins-git/subprojects/veins_inet/src/veins_inet -l $SIMU5G/src/simu5g -l $MOQ/src/MoQVeinsSim"
OUT="$MOQ/simulations/results/bdp300"
PARALLEL=$(( $(nproc) / 2 )); [ "$PARALLEL" -lt 1 ] && PARALLEL=1

# One (scenario, label, config, seed). Mirrors run-comparison.sh's run_one gate logic.
run_one() {
    local scen=$1 label=$2 cfg=$3 s=$4
    local dir="$OUT/$scen/seed$s"
    mkdir -p "$dir"
    timeout 1800 opp_run -u Cmdenv -c "$cfg" -f omnetpp.ini --seed-set="$s" \
        --result-dir="$dir" -n "$NED" $LIBS > "$OUT/$scen/${label}_s$s.log" 2>&1
    for f in "$dir/$cfg"-*.sca; do [ -e "$f" ] && mv "$f" "$dir/$label.sca"; break; done
    local rej; rej=$(grep -h quicSendRejected "$dir/$label.sca" 2>/dev/null | awk '{t+=$NF} END{print t+0}')
    if grep -q 'limit reached' "$OUT/$scen/${label}_s$s.log"; then
        echo "[$scen] seed=$s $label -> OK rejected=${rej:-?}"
    else
        mv "$dir/$label.sca" "$dir/$label.sca.aborted" 2>/dev/null
        echo "[$scen] seed=$s $label -> ABORTED: $(grep -oE '<!> Error:.*' "$OUT/$scen/${label}_s$s.log" | head -1)"
    fi
}

rm -rf "$OUT"; mkdir -p "$OUT"
running=0
for s in $(seq 0 $((SEEDS-1))); do
    for spec in "urban MOQ_Partial_BDP_300 MOQ_Partial_BDP_300" \
                "urban MOQ_SW_BDP_300 MOQ_SW_BDP_300" \
                "highway MOQ_Partial_BDP_300 MOQ_Partial_BDP_300_HW" \
                "highway MOQ_SW_BDP_300 MOQ_SW_BDP_300_HW"; do
        set -- $spec
        run_one "$1" "$2" "$3" "$s" &
        running=$((running+1))
        [ "$running" -ge "$PARALLEL" ] && { wait -n; running=$((running-1)); }
    done
done
wait
echo "bdp300 comparison data written to $OUT"

#!/bin/bash
# Protocol comparison across seeds, for one scenario (RQ2/RQ3).
#
# The channel model is stochastic (log-normal shadowing, Jakes fading) and vehicles hand over
# mid-run, so a single run per config cannot support a claim. Each config is run once per seed-set;
# aggregate with scripts/aggregate_seeds.py, which reports means with 95% CIs.
#
# Usage: run-comparison.sh <result-dir> [num-seeds] [urban|highway]

OUT=${1:?usage: run-comparison.sh <result-dir> [num-seeds] [urban|highway]}
SEEDS=${2:-5}
SCENARIO=${3:-urban}

case "$SCENARIO" in
    urban)   SUF="" ;;
    highway) SUF="_HW" ;;   # the Highway ini mixin; same workload, 3km corridor at 120km/h
    *) echo "unknown scenario: $SCENARIO (expected urban|highway)" >&2; exit 1 ;;
esac

# Note: OMNeT++'s setenv references unset variables, so it cannot be sourced under `set -u`.
source /home/jhuang/omnetpp/omnetpp-6.3.0/setenv -q >/dev/null 2>&1
M=/mnt/d/Work/Thesis/MoQVeinsSim
I=/home/jhuang/thesiswork/inet-4.6.0
V=/home/jhuang/thesiswork/veins-git
S=/home/jhuang/thesiswork/simu5g-git
NED="$M/src:.:$I/src:$V/src/veins:$V/subprojects/veins_inet/src/veins_inet:$S/src"
LIBS="-l $I/src/INET -l $V/src/veins -l $V/subprojects/veins_inet/src/veins_inet -l $S/src/simu5g -l $M/src/MoQVeinsSim"

# The bounded-window operating point identified by the window sweep. It is not a separate ini
# config, so it is applied as an override.
TUNED="--**.quic.initialMaxData=128kB --**.quic.initialMaxStreamData=128kB --*.car[*].quic.sendQueueLimit=16384B"

cd "$M/simulations" || exit 1
mkdir -p "$OUT"

run() {  # run <label> <config> [extra opp_run args...]
    label=$1
    cfg=$2
    shift 2
    dir="$OUT/seed$s"
    mkdir -p "$dir"

    timeout 1800 opp_run -u Cmdenv -c "$cfg" -f omnetpp.ini --seed-set="$s" \
        --result-dir="$dir" "$@" -n "$NED" $LIBS > "$OUT/${label}_s$s.log" 2>&1

    # Name the .sca by label, not by ini config, so tuned/untuned variants of one config differ.
    for f in "$dir/$cfg"-*.sca; do
        [ -e "$f" ] && mv "$f" "$dir/$label.sca"
        break
    done

    rej=$(grep -h quicSendRejected "$dir/$label.sca" 2>/dev/null | awk '{t+=$NF} END{print t+0}')
    status=$(grep -oE 'limit reached|<!> Error: [A-Za-z_]+' "$OUT/${label}_s$s.log" | head -1)
    # rejected must be 0: INET's QUIC silently discards writes once its send queue is full, so a
    # nonzero count means the run threw load away and its numbers are meaningless.
    echo "[$SCENARIO] seed=$s $label -> ${status:-NO RESULT} rejected=${rej:-?}"
}

for s in $(seq 0 $((SEEDS - 1))); do
    run MOQ_QUIC        "MOQ$SUF"
    run MOQ_TCP         "MOQ_TCP$SUF"
    run MOQ_UDP         "MOQ_UDP$SUF"
    run MQTT_TCP        "MQTT_TCP$SUF"
    run MQTT_QUIC       "MQTT_QUIC$SUF"
    run MOQ_Partial_128 "MOQ_Partial$SUF" $TUNED
    run MOQ_SW_128      "MOQ_SW$SUF"      $TUNED
done

echo COMPARISON_DONE

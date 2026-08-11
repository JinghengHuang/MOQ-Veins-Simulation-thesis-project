#!/bin/bash
# Protocol comparison across seeds, for one scenario (RQ2/RQ3).
#
# The channel model is stochastic (log-normal shadowing, Jakes fading) and vehicles hand over
# mid-run, so a single run per config cannot support a claim. Each config is run once per seed-set;
# aggregate with scripts/aggregate_seeds.py, which reports means with 95% CIs.
#
# Runs are independent, so they execute in parallel (each opp_run gets its own SUMO instance from
# veins_launchd). PARALLEL defaults to half the cores, leaving headroom for those SUMO processes.
#
# Usage: run-comparison.sh <result-dir> [num-seeds] [urban|highway] [parallel] [configuration]

OUT=${1:?usage: run-comparison.sh <result-dir> [num-seeds] [urban|highway] [parallel] [configuration]}
SEEDS=${2:-5}
SCENARIO=${3:-urban}
PARALLEL=${4:-$(( $(nproc) / 2 ))}
CONFIGURATION=${5:-}

case "$SCENARIO" in
    urban)   SUF="" ;;
    highway) SUF="_HW" ;;   # the Highway ini mixin; same workload, 3km corridor at 120km/h
    *) echo "unknown scenario: $SCENARIO (expected urban|highway)" >&2; exit 1 ;;
esac

SPECS=("MOQ_QUIC MOQ$SUF" "MOQ_TCP MOQ_TCP$SUF" "MOQ_UDP MOQ_UDP$SUF" "MQTT_TCP MQTT_TCP$SUF" "MQTT_QUIC MQTT_QUIC$SUF")

case "$CONFIGURATION" in
    moq)   SPECS=("MOQ_QUIC MOQ$SUF") ;;
    moq_tcp)   SPECS=("MOQ_TCP MOQ_TCP$SUF") ;;
    moq_udp)   SPECS=("MOQ_UDP MOQ_UDP$SUF") ;;
    mqtt_quic) SPECS=("MQTT_QUIC MQTT_QUIC$SUF") ;;
    mqtt_tcp)  SPECS=("MQTT_TCP MQTT_TCP$SUF") ;;
    *) SPECS=("MOQ_QUIC MOQ$SUF" "MOQ_TCP MOQ_TCP$SUF" "MOQ_UDP MOQ_UDP$SUF" "MQTT_TCP MQTT_TCP$SUF" "MQTT_QUIC MQTT_QUIC$SUF");;
esac

IS_MOQ_QUIC=false
for spec in "${SPECS[@]}"; do
    if [[ "$spec" == "MOQ_QUIC MOQ$SUF" ]]; then
        IS_MOQ_QUIC=true
        break
    fi
done

# Note: OMNeT++'s setenv references unset variables, so it cannot be sourced under `set -u`.
source /home/jhuang/omnetpp/omnetpp-6.3.0/setenv -q >/dev/null 2>&1
M=/mnt/d/Work/Thesis/MoQVeinsSim
I=/home/jhuang/thesiswork/inet-4.6.0
V=/home/jhuang/thesiswork/veins-git
S=/home/jhuang/thesiswork/simu5g-git
NED="$M/src:.:$I/src:$V/src/veins:$V/subprojects/veins_inet/src/veins_inet:$S/src"
LIBS="-l $I/src/INET -l $V/src/veins -l $V/subprojects/veins_inet/src/veins_inet -l $S/src/simu5g -l $M/src/MoQVeinsSim"

echo "$OUT":

cd "$M/simulations" || exit 1
rm -rf "${OUT:?}"
mkdir -p "$OUT"

# One (seed, config) pair.
run_one() {
    s=$1; label=$2; cfg=$3; shift 3
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
    # Two independent checks, both must pass:
    #  - the run reached the time limit. A run that aborts early still writes a .sca, and its partial
    #    results are indistinguishable from a real "delivered nothing" outcome.
    #  - rejected == 0. INET's QUIC silently discards writes once its send queue is full, so a
    #    nonzero count means the run threw load away and its numbers are meaningless.
    if grep -q 'limit reached' "$OUT/${label}_s$s.log"; then
        status=OK
    else
        status="ABORTED: $(grep -oE '<!> Error:.*' "$OUT/${label}_s$s.log" | head -1)"
        mv "$dir/$label.sca" "$dir/$label.sca.aborted" 2>/dev/null   # keep it out of the analysis
        # Vectors too. They are named by ini config rather than by label, so renaming only the
        # .sca left them loadable: an .anf input pattern of **/*.vec would pull a crashed run into
        # a vector chart with nothing marking it as dead. Same survivor-bias hazard as the .sca.
        for f in "$dir/$cfg"-*.vec "$dir/$cfg"-*.vci; do
            [ -e "$f" ] && mv "$f" "$f.aborted"
        done
    fi
    echo "[$SCENARIO] seed=$s $label -> $status rejected=${rej:-?}"
}

# Runs are independent, so fan them out, capped at PARALLEL concurrent simulations.
running=0
for s in $(seq 0 $((SEEDS - 1))); do
    for spec in "${SPECS[@]}"; do
        set -- $spec
        run_one "$s" "$1" "$2" &
        running=$((running + 1))
        [ "$running" -ge "$PARALLEL" ] && { wait -n; running=$((running - 1)); }
    done
    # Bounded-window operating point: a named config, so the .sca carries a distinct configname
    # and the two variants of MoQ can be told apart in the analysis tool.
    if [ "$IS_MOQ_QUIC" == true ]; then
        for spec in "MOQ_Partial_BDP MOQ_Partial_BDP$SUF" "MOQ_SW_BDP MOQ_SW_BDP$SUF"; do
            set -- $spec
            run_one "$s" "$1" "$2" &
            running=$((running + 1))
            [ "$running" -ge "$PARALLEL" ] && { wait -n; running=$((running - 1)); }
        done
    fi
done
wait

echo COMPARISON_DONE

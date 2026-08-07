#!/bin/bash
# Publisher-count sweep at constant offered load (experiments 1 & 2).
#   Exp 1: PubScale_N{1,2,3,4}        -- QUIC default round-robin scheduler
#   Exp 2: PubScale_N{1,2,3,4}_Prio   -- transport PriorityScheduler enabled
# Same total load at every N; only the number of publishers (streams/connection = 2N) varies.
#
# Usage: run-pubscale.sh <result-dir> [num-seeds] [parallel]
# NB: no `set -u` -- OMNeT++'s setenv references unset vars and aborts under it.
OUT=${1:?usage: run-pubscale.sh <result-dir> [num-seeds] [parallel]}
SEEDS=${2:-5}
PARALLEL=${3:-$(( $(nproc) / 2 ))}

CONFIGS=(PubScale_N1 PubScale_N2 PubScale_N3 PubScale_N4
         PubScale_N1_Prio PubScale_N2_Prio PubScale_N3_Prio PubScale_N4_Prio)

# Resolve OUT to absolute BEFORE cd, so a relative arg is not re-rooted under $M/simulations.
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac

source /home/jhuang/omnetpp/omnetpp-6.3.0/setenv -q >/dev/null 2>&1
M=/mnt/d/Work/Thesis/MoQVeinsSim
I=/home/jhuang/thesiswork/inet-4.6.0
V=/home/jhuang/thesiswork/veins-git
S=/home/jhuang/thesiswork/simu5g-git
NED="$M/src:.:$I/src:$V/src/veins:$V/subprojects/veins_inet/src/veins_inet:$S/src"
LIBS="-l $I/src/INET -l $V/src/veins -l $V/subprojects/veins_inet/src/veins_inet -l $S/src/simu5g -l $M/src/MoQVeinsSim"

cd "$M/simulations" || exit 1
rm -rf "${OUT:?}"; mkdir -p "$OUT"

run_one() {
    cfg=$1; s=$2; dir="$OUT/$cfg/seed$s"; mkdir -p "$dir"
    timeout 2400 opp_run -u Cmdenv -c "$cfg" -f omnetpp.ini --seed-set="$s" \
        --result-dir="$dir" -n "$NED" $LIBS > "$OUT/${cfg}_s$s.log" 2>&1
    for f in "$dir/$cfg"-*.sca; do [ -e "$f" ] && mv "$f" "$dir/$cfg.sca"; break; done
    rej=$(grep -h quicSendRejected "$dir/$cfg.sca" 2>/dev/null | awk '{t+=$NF} END{print t+0}')
    if grep -q 'limit reached' "$OUT/${cfg}_s$s.log"; then status=OK; else
        status="ABORTED: $(grep -oE '<!> Error:.*' "$OUT/${cfg}_s$s.log" | head -1)"
        mv "$dir/$cfg.sca" "$dir/$cfg.sca.aborted" 2>/dev/null; fi
    echo "$cfg seed=$s -> $status rejected=${rej:-?}"
}

running=0
for cfg in "${CONFIGS[@]}"; do
    for s in $(seq 0 $((SEEDS - 1))); do
        run_one "$cfg" "$s" &
        running=$((running + 1))
        [ "$running" -ge "$PARALLEL" ] && { wait -n; running=$((running - 1)); }
    done
done
wait
echo PUBSCALE_DONE

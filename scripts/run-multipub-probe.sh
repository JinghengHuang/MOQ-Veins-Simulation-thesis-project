#!/bin/bash
# Probe run for MOQ_Partial_MultiPub (never run before): confirm whether tripling the
# offered load with 3 publishers really produces a degenerate "everyone misses every
# deadline" result, or whether the tiny high-priority BBox track is still protected.
#
# Usage: run-multipub-probe.sh <result-dir> <num-seeds> [config] [parallel]
# NB: no `set -u` -- OMNeT++'s setenv references unset vars and aborts under it.
OUT=${1:?usage: run-multipub-probe.sh <result-dir> <num-seeds> [config] [parallel]}
SEEDS=${2:?need num-seeds}
CFG=${3:-MOQ_Partial_MultiPub}
PARALLEL=${4:-$(( $(nproc) / 2 ))}

source /home/jhuang/omnetpp/omnetpp-6.3.0/setenv -q >/dev/null 2>&1
M=/mnt/d/Work/Thesis/MoQVeinsSim
I=/home/jhuang/thesiswork/inet-4.6.0
V=/home/jhuang/thesiswork/veins-git
S=/home/jhuang/thesiswork/simu5g-git
NED="$M/src:.:$I/src:$V/src/veins:$V/subprojects/veins_inet/src/veins_inet:$S/src"
LIBS="-l $I/src/INET -l $V/src/veins -l $V/subprojects/veins_inet/src/veins_inet -l $S/src/simu5g -l $M/src/MoQVeinsSim"

# Resolve OUT to an absolute path BEFORE cd, so a relative arg is not re-rooted
# under $M/simulations (which silently doubles the path).
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
cd "$M/simulations" || exit 1
rm -rf "${OUT:?}"; mkdir -p "$OUT"

run_one() {
    s=$1; dir="$OUT/seed$s"; mkdir -p "$dir"
    timeout 2400 opp_run -u Cmdenv -c "$CFG" -f omnetpp.ini --seed-set="$s" \
        --result-dir="$dir" -n "$NED" $LIBS > "$OUT/${CFG}_s$s.log" 2>&1
    for f in "$dir/$CFG"-*.sca; do [ -e "$f" ] && mv "$f" "$dir/$CFG.sca"; break; done
    rej=$(grep -h quicSendRejected "$dir/$CFG.sca" 2>/dev/null | awk '{t+=$NF} END{print t+0}')
    if grep -q 'limit reached' "$OUT/${CFG}_s$s.log"; then status=OK; else
        status="ABORTED: $(grep -oE '<!> Error:.*' "$OUT/${CFG}_s$s.log" | head -1)"; fi
    echo "seed=$s $CFG -> $status rejected=${rej:-?}"
}

running=0
for s in $(seq 0 $((SEEDS - 1))); do
    run_one "$s" &
    running=$((running + 1))
    [ "$running" -ge "$PARALLEL" ] && { wait -n; running=$((running - 1)); }
done
wait
echo MULTIPUB_PROBE_DONE

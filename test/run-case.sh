#!/usr/bin/env bash
# Run one simulator scenario in an isolated nested session and print the verdict.
#
#   test/run-case.sh [-x XWAYLAND] [-S SIM_BINARY] [-n NAME] -- SIM_ARGS...
#
# Results go to $WCF_RUNS/NAME (default /tmp/wcf-runs/NAME).
set -u
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
xw=/usr/bin/Xwayland
sim="$here/sim-lwjgl"
name="case-$$"
while getopts 'x:S:n:' o; do
  case $o in
    x) xw=$OPTARG ;;
    S) sim=$OPTARG ;;
    n) name=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1)); [[ ${1:-} == -- ]] && shift
runs=${WCF_RUNS:-/tmp/wcf-runs}
out="$runs/$name"
rm -rf "$out"; mkdir -p "$out"
WCF_SIM="$sim" WCF_SIM_ARGS="$*" "$here/nest.sh" -x "$xw" -o "$out" -t "${WCF_TIMEOUT:-240}" -- "$here/session.sh" \
  > "$out/nest.out" 2>&1
rc=$?
if [[ $rc -ne 0 ]] || ! grep -q '^SIM done' "$out/sim.out" 2>/dev/null; then
  echo "[$name] RUN FAILED rc=$rc (see $out)"; tail -3 "$out/sim.err" 2>/dev/null; exit 1
fi
if [[ -x "$here/analyze.py" ]]; then
  "$here/analyze.py" "$out" ${WCF_ANALYZE_ARGS:-} | sed "s/^/[$name] /"
fi

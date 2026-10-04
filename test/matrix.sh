#!/usr/bin/env bash
# Run the regression matrix against one Xwayland binary and print one summary
# line per scenario plus the worst settle time.
#
#   test/matrix.sh XWAYLAND NAME [quick]
#
# Scenarios: both simulators (LWJGL2-style recenter+release, SDL3 X11 driver)
# x GPU load (0/150/400 quads) x release while moving / still.
# "quick" runs the heavy-load column only. Results: $WCF_RUNS/NAME-*.
set -u
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
xw=$(readlink -f "$1"); name=$2; quick=${3:-}
loads=(0 150 400); [[ $quick == quick ]] && loads=(400)
cycles=${WCF_CYCLES:-10}
fail=0
for sim in lwjgl sdl; do
  for load in "${loads[@]}"; do
    for still in 0 1; do
      case=$name-$sim-g$load-s$still
      args=(--cycles "$cycles" --grab-ms 400 --menu-ms 400 --still-release $still
            --speed 20 --gpu-quads "$load" --start 250,180)
      [[ $sim == sdl ]] && args+=(--warp-center 1)
      out=$("$here/run-case.sh" -x "$xw" -S "$here/sim-$sim" -n "$case" -- "${args[@]}" 2>&1)
      res=$(grep RESULT <<<"$out" | sed 's/.*RESULT //')
      worst=$(grep -o 'settle=[0-9.]* ms' <<<"$out" | sed 's/settle=//; s/ ms//' | sort -n | tail -1)
      printf '%-8s load=%-3s %-6s %-12s worst_settle=%s ms\n' "$sim" "$load" \
        "$([[ $still == 1 ]] && echo still || echo moving)" "${res:-RUN-FAILED}" "${worst:--}"
      [[ ${res%/*} == "${res#*/}" ]] || fail=1
    done
  done
done
exit $fail

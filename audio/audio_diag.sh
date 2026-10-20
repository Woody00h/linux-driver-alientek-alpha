#!/bin/sh
# Capture live clock/Codec state without writing hardware registers.
# Usage: sh audio_diag.sh [card-number, default 1]
set -eu
card=${1:-1}
case "$card" in ''|*[!0-9]*) echo 'Card must be a number' >&2; exit 1;; esac
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_program=$script_dir/audio_test
[ -x "$test_program" ] || { echo 'Missing executable audio_test' >&2; exit 1; }
work=$(mktemp -d /tmp/alpha-audio-diag.XXXXXX)
child=
trap 'if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi' EXIT
trap 'exit 130' INT TERM

if [ ! -r /sys/kernel/debug/clk/clk_summary ]; then
    mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || :
fi

snapshot() {
    direction=$1
    echo "Live $direction state:"
    for name in hw_params status; do
        file=/proc/asound/card"$card"/pcm0"$direction"/sub0/"$name"
        if [ -r "$file" ]; then
            echo "$file"
            cat "$file"
        fi
    done
    echo 'Clock rates (Hz):'
    if [ -r /sys/kernel/debug/clk/clk_summary ]; then
        grep -E 'clock|pll4|sai2|osc ' /sys/kernel/debug/clk/clk_summary || :
    else
        echo 'clk_summary unavailable'
    fi
    echo 'WM8960 cached registers (hex):'
    found=no
    for file in /sys/kernel/debug/regmap/*001a*/registers; do
        [ -r "$file" ] || continue
        found=yes
        echo "$file"
        grep -E '^(04|05|07|08|09|17|18|19|1a|2f):' "$file" || :
    done
    [ "$found" = yes ] || echo 'Codec regmap unavailable'
}

echo "Diagnostic files: $work"
echo 'Playback timing test (5 seconds of samples):'
"$test_program" tone /dev/snd/pcmC"$card"D0p 5 1000 both >"$work/play.log" 2>&1 &
child=$!
sleep 1
snapshot p
rc=0
wait "$child" || rc=$?
child=
cat "$work/play.log"
echo "Playback exit status: $rc"

echo 'Capture timing test (3 seconds warm-up, then 12 seconds of samples):'
"$test_program" record /dev/snd/pcmC"$card"D0c "$work/capture.pcm" 12 >"$work/record.log" 2>&1 &
child=$!
sleep 1
snapshot c
rc=0
wait "$child" || rc=$?
child=
cat "$work/record.log"
echo "Capture exit status: $rc"
echo 'Recent kernel messages:'
dmesg | tail -20
echo "Raw capture and timing logs retained in $work"

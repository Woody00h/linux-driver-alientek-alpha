#!/bin/sh
# Usage: sh audio_setup.sh [ALSA-card-number] [headphone|speaker] [mic|line]
set -eu
card=${1:-0}
output=${2:-headphone}
input=${3:-mic}
case "$output" in headphone|speaker) ;; *) echo 'output: headphone|speaker' >&2; exit 1;; esac
case "$input" in mic|line) ;; *) echo 'input: mic|line' >&2; exit 1;; esac
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$script_dir/audio_ctl" ]; then
    set_ctl() { "$script_dir/audio_ctl" "$card" "$1" "$2"; }
elif command -v amixer >/dev/null 2>&1; then
    set_ctl() { amixer -q -c "$card" cset "name=$1" "$2"; }
else
    echo "Copy audio_ctl beside this script and run chmod +x audio_ctl" >&2
    exit 1
fi

# Silence output before changing signal paths. Enable only the selected jack.
set_ctl 'Headphone Switch' off
set_ctl 'Speaker Switch' off
# These are raw register values, not percentages. DAC 255 = 0 dB;
# headphone 110 = -11 dB. With the 10% tone, total level is about -31 dBFS.
set_ctl 'Playback Volume' 255,255
set_ctl 'Headphone Playback Volume' 110,110
set_ctl 'Speaker Playback Volume' 100,100
set_ctl 'PCM Playback -6dB Switch' off
set_ctl 'Speaker DC Volume' 0
set_ctl 'Speaker AC Volume' 0
set_ctl 'Left Output Mixer PCM Playback Switch' on
set_ctl 'Right Output Mixer PCM Playback Switch' on
set_ctl 'Left Output Mixer Boost Bypass Switch' off
set_ctl 'Right Output Mixer Boost Bypass Switch' off
set_ctl 'Left Output Mixer LINPUT3 Switch' off
set_ctl 'Right Output Mixer RINPUT3 Switch' off

set_ctl 'Onboard Mic Switch' off
set_ctl 'Line In Switch' off
set_ctl 'Capture Switch' on,on
set_ctl 'Capture Volume' 32,32
set_ctl 'ADC PCM Capture Volume' 195,195
set_ctl 'ADC Data Output Select' 0
set_ctl 'ALC Function' Off
set_ctl 'Left Input Mixer Boost Switch' on
set_ctl 'Right Input Mixer Boost Switch' on
set_ctl 'Left Boost Mixer LINPUT1 Switch' off
set_ctl 'Left Boost Mixer LINPUT2 Switch' off
set_ctl 'Left Boost Mixer LINPUT3 Switch' off
set_ctl 'Right Boost Mixer RINPUT1 Switch' off
set_ctl 'Right Boost Mixer RINPUT2 Switch' off
set_ctl 'Right Boost Mixer RINPUT3 Switch' off
set_ctl 'Left Input Boost Mixer LINPUT1 Volume' 0
set_ctl 'Right Input Boost Mixer RINPUT1 Volume' 0
set_ctl 'Left Input Boost Mixer LINPUT2 Volume' 0
set_ctl 'Left Input Boost Mixer LINPUT3 Volume' 0
set_ctl 'Right Input Boost Mixer RINPUT2 Volume' 0
set_ctl 'Right Input Boost Mixer RINPUT3 Volume' 0
if [ "$input" = mic ]; then
    # LIN1 is AC-grounded negative input; LIN2 is MIC positive input.
    set_ctl 'Left Boost Mixer LINPUT1 Switch' on
    set_ctl 'Left Boost Mixer LINPUT2 Switch' on
    # PGA 48 = +18.75 dB, boost 2 = +20 dB: +19 dB over old mic preset.
    set_ctl 'Capture Volume' 48,48
    set_ctl 'Left Input Boost Mixer LINPUT1 Volume' 2
    # Board mic reaches only the left ADC; duplicate it into both PCM slots.
    set_ctl 'ADC Data Output Select' 1
    set_ctl 'Onboard Mic Switch' on
else
    # PCB: LINE_INR -> LIN3 (left ADC), LINE_INL -> RIN2 (right ADC).
    set_ctl 'Left Input Boost Mixer LINPUT3 Volume' 3
    set_ctl 'Right Input Boost Mixer RINPUT2 Volume' 3
    set_ctl 'Line In Switch' on
fi
if [ "$output" = headphone ]; then
    set_ctl 'Headphone Switch' on
else
    set_ctl 'Speaker Switch' on
fi
echo "Configured card $card: $output / $input (start with low volume)"

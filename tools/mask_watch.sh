#!/usr/bin/env bash
# Waits until work/edits holds submissions not yet listed in work/edits/done.txt, then prints every one of them (oldest
# first, all their touch-ups) and exits. Claude actions each one, appends its folder name to done.txt, and runs this
# again. Nothing sent in between is missed: the list of handled folders decides, not the time the watch started.
cd "$(dirname "$0")/.." || exit 1
mkdir -p work/edits
touch work/edits/done.txt
while true; do
    pending=$(ls work/edits | grep -v '^done.txt$' | sort | while read -r d; do
        [ -f "work/edits/$d/request.txt" ] && ! grep -qxF "$d" work/edits/done.txt && echo "$d"
    done)
    if [ -n "$pending" ]; then
        sleep 1                                   # a submission sent right after another: take it in the same batch
        for d in $pending; do
            echo "=== SUBMISSION: work/edits/$d"
            cat "work/edits/$d/request.txt"
        done
        exit 0
    fi
    sleep 2
done

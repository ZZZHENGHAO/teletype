#!/bin/sh
set -eu

output=$(
    {
        for channel in 0 1 2 3 4 5 6 7; do
            printf 'PN %s 63 %s\n' "$channel" "$((channel - 4))"
        done
        for channel in 0 1 2 3 4 5 6 7; do
            printf 'PN %s 63\n' "$channel"
        done
        printf '%s\n' 'P.N 8; P 63' 'P.N -1; P 63' '@X 7; @Y 63; @' \
            'Q.P2 7; Q.2P 4; PN 4 63' 'INIT.P 7; PN 7 63'
    } | ./tt
)

actual=$(printf '%s\n' "$output" | awk '/^>>> / {print $2}')
expected=$(printf '%s\n' -4 -3 -2 -1 0 1 2 3 3 -4 3 3 0)
if [ "$actual" != "$expected" ]; then
    printf '%s\n' "$output" >&2
    exit 1
fi
printf '%s\n' 'TRACKER simulator smoke test passed'

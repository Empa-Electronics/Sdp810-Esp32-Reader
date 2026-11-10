#!/bin/sh
# ./replay.sh test                      run the detector tests on this computer
# ./replay.sh logs/x.csv [key=value...] run a recorded log through the detector
DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN="$DIR/../main"
OUT="$DIR/.host_build"
mkdir -p "$OUT"
if [ "$1" = "test" ]; then
    cc -std=c11 -Wall -Wextra -O1 -I"$MAIN" "$MAIN/puff_detector.c" "$DIR/test_puff_detector.c" -lm -o "$OUT/test" && exec "$OUT/test"
else
    cc -std=c11 -Wall -Wextra -O1 -I"$MAIN" "$MAIN/puff_detector.c" "$DIR/puff_replay.c" -lm -o "$OUT/replay" && exec "$OUT/replay" "$@"
fi

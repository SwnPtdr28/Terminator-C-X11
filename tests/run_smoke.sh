#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make >/dev/null
cc -w -pthread -Iinclude tests/engine_smoke.c src/engine.c src/shell.c src/history.c -o /tmp/terminator_engine_smoke
/tmp/terminator_engine_smoke 'echo hello' 'echo "two words"' 'echo abc | wc -c' 'false' 'echo $?' 'cd /tmp' 'pwd' 'echo demo > /tmp/terminator_smoke_file' 'cat < /tmp/terminator_smoke_file' 'sleep 0.1 &' 'jobs'
if command -v xvfb-run >/dev/null; then
    timeout 2 xvfb-run -a ./terminator >/dev/null 2>&1 || test "$?" -eq 124
    timeout 2 xvfb-run -a ./terminator useProcess=True >/dev/null 2>&1 || test "$?" -eq 124
fi

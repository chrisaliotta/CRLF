#!/usr/bin/env bash
# Long-lived completion session. Reads one JSON request per line and writes
# one JSON reply per line. See docs/completion.md.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
export PYTHONUNBUFFERED=1
exec python3 "$here/complete.py"

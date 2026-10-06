#!/usr/bin/env bash
# Start HelioSpan Monitor on http://127.0.0.1:8000
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ ! -d .venv ]]; then
  python3 -m venv .venv
fi

.venv/bin/pip install -q -r requirements.txt
exec .venv/bin/uvicorn heliospan.main:app \
  --reload \
  --reload-dir heliospan \
  --host 127.0.0.1 \
  --port 8000

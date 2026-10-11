#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PORT=${PORT:-8000}

printf 'MOSdesk visual preview: http://localhost:%s/mosdesk/preview.html\n' "$PORT"
exec python3 -m http.server "$PORT" --bind 0.0.0.0 --directory "$PROJECT_ROOT"
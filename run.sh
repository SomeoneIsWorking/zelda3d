#!/usr/bin/env bash
set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$REPO"
exec uv run --frozen python "$REPO/bootstrap.py" "$@"

#!/usr/bin/env bash
# oracle_boot.sh — one-command OoT3D oracle boot / re-entry primitive.
#
# Idempotent: if Azahar is already running OoT3D and the RPC is live, just prints status and
# exits. If not running, launches it headlessly, waits for the RPC to come up, then performs
# fast re-entry (File 1 save → Kokiri Forest) to skip the 3-minute intro cutscene.
#
# Usage:
#   source .env && tools/oracle_boot.sh             # boot if needed, re-enter if needed
#   source .env && tools/oracle_boot.sh --stop       # kill Azahar + clean pidfile
#   source .env && tools/oracle_boot.sh --status     # print status (exit 0=alive, 1=dead)
#   source .env && tools/oracle_boot.sh --reenter    # force fast re-entry even if in-game
#   source .env && tools/oracle_boot.sh --display :95  # override Xvfb display (default :95)
#
# The oracle lock (scratch/.oraclelock) is NOT held inside this script — the caller holds it
# for the full oracle session. This script just ensures Azahar is alive and in-game.
#
# Outputs:
#   scratch/oracle.pid       — Azahar PID
#   scratch/oracle.display   — DISPLAY value (e.g. ":95")
#   RPC :45987 live on exit
#
# Fast re-entry recipe (from docs/oracle.md):
#   tap start → title screen
#   tap start → file select
#   tap a     → File 1 highlighted
#   tap a     → loads the save → spawns in Kokiri Forest (scene 85)
#
# Requires: source .env first (SOH3D_3DS_ROM); PIL (Pillow) for screenshots.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AZAHAR="$REPO/Azahar/build/bin/Release/azahar"
PIDFILE="$REPO/scratch/oracle.pid"
DISPFILE="$REPO/scratch/oracle.display"
SCRATCH="$REPO/scratch"
TOOLS="$REPO/tools"
REPL="$TOOLS/azahar_repl.py"

# ── argument parsing ──────────────────────────────────────────────────────────
CMD="boot"            # boot | stop | status | reenter
XVFB_DISPLAY=":95"   # preferred display for new Azahar launch

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stop)     CMD="stop"    ;;
        --status)   CMD="status"  ;;
        --reenter)  CMD="reenter" ;;
        --display)  XVFB_DISPLAY="$2"; shift ;;
        --help|-h)
            head -30 "${BASH_SOURCE[0]}" | grep '^#' | sed 's/^# \?//'
            exit 0 ;;
        *) echo "[oracle_boot] unknown arg: $1" >&2; exit 1 ;;
    esac
    shift
done

mkdir -p "$SCRATCH/logs"

# ── helpers ───────────────────────────────────────────────────────────────────

rpc_alive() {
    # Exit 0 if the RPC is up and OoT3D process is selected, else 1.
    python3 "$REPL" -c "procs" 2>/dev/null | grep -q "CtrApp"
}

azahar_pid() {
    # Return the PID of the running Azahar process, or "" if none.
    pgrep -f "azahar.*\.3ds" 2>/dev/null | head -1 || true
}

save_pid() {
    local pid="$1" disp="$2"
    echo "$pid" > "$PIDFILE"
    echo "$disp" > "$DISPFILE"
}

ensure_xvfb() {
    # Make sure XVFB_DISPLAY is running. If not, start a new one.
    if xdpyinfo -display "$XVFB_DISPLAY" >/dev/null 2>&1; then
        echo "[oracle_boot] Xvfb $XVFB_DISPLAY already running."
    else
        echo "[oracle_boot] Starting Xvfb $XVFB_DISPLAY..."
        Xvfb "$XVFB_DISPLAY" -screen 0 1280x720x24 &
        sleep 1
        if ! xdpyinfo -display "$XVFB_DISPLAY" >/dev/null 2>&1; then
            echo "[oracle_boot] ERROR: Xvfb $XVFB_DISPLAY failed to start." >&2
            exit 1
        fi
    fi
}

fast_reenter() {
    # Drive OoT3D from the title screen through the file-select into Kokiri Forest (File 1).
    # Sequence: start → start → a → a with generous gaps for load screens.
    echo "[oracle_boot] Fast re-entry: title → file select → File 1 → in-game..."
    python3 "$REPL" -c "
sleep 1;
tap start;
sleep 2;
tap start;
sleep 1;
tap a;
sleep 1;
tap a;
sleep 8
" 2>&1 | sed 's/^/  /'
    echo "[oracle_boot] Re-entry done."
}

# ── commands ──────────────────────────────────────────────────────────────────

do_status() {
    local pid disp
    pid="$(azahar_pid)"
    disp="$(cat "$DISPFILE" 2>/dev/null || echo "unknown")"
    if [[ -z "$pid" ]]; then
        echo "[oracle_boot] Azahar: NOT running"
        return 1
    fi
    echo "[oracle_boot] Azahar: pid=$pid  display=$disp"
    if rpc_alive; then
        echo "[oracle_boot] RPC :45987: LIVE (OoT3D in-process)"
        return 0
    else
        echo "[oracle_boot] RPC :45987: NOT responding"
        return 1
    fi
}

do_stop() {
    local pid
    pid="$(azahar_pid)"
    if [[ -n "$pid" ]]; then
        echo "[oracle_boot] Stopping Azahar pid=$pid..."
        kill "$pid" 2>/dev/null || true
        sleep 1
        kill -9 "$pid" 2>/dev/null || true
    else
        echo "[oracle_boot] Azahar not running."
    fi
    rm -f "$PIDFILE" "$DISPFILE"
    echo "[oracle_boot] Done."
}

do_boot() {
    # Check if already running + RPC alive → nothing to do.
    local pid
    pid="$(azahar_pid)"
    if [[ -n "$pid" ]] && rpc_alive; then
        echo "[oracle_boot] Azahar already running (pid=$pid) and RPC live. Nothing to do."
        save_pid "$pid" "$(cat "$DISPFILE" 2>/dev/null || echo "$XVFB_DISPLAY")"
        return 0
    fi

    # Running but RPC dead → kill and restart.
    if [[ -n "$pid" ]]; then
        echo "[oracle_boot] Azahar running (pid=$pid) but RPC not responding — killing and restarting."
        kill "$pid" 2>/dev/null || true
        sleep 2
    fi

    # ROM check.
    if [[ -z "${SOH3D_3DS_ROM:-}" ]]; then
        echo "[oracle_boot] ERROR: SOH3D_3DS_ROM not set. Run: source .env" >&2
        exit 1
    fi
    if [[ ! -f "$SOH3D_3DS_ROM" ]]; then
        echo "[oracle_boot] ERROR: ROM not found: $SOH3D_3DS_ROM" >&2
        exit 1
    fi

    ensure_xvfb

    echo "[oracle_boot] Launching Azahar headless on $XVFB_DISPLAY..."
    DISPLAY="$XVFB_DISPLAY" QT_QPA_PLATFORM=xcb setsid nohup \
        "$AZAHAR" "$SOH3D_3DS_ROM" \
        >"$SCRATCH/logs/oracle.log" 2>&1 &
    local new_pid=$!
    save_pid "$new_pid" "$XVFB_DISPLAY"
    echo "[oracle_boot] Launched pid=$new_pid. Waiting for RPC (up to 60s)..."

    local deadline=$(( SECONDS + 60 ))
    while (( SECONDS < deadline )); do
        if rpc_alive; then
            echo "[oracle_boot] RPC live (pid=$new_pid)."
            break
        fi
        printf "."
        sleep 2
    done
    if ! rpc_alive; then
        echo ""
        echo "[oracle_boot] ERROR: RPC did not come up after 60s. Check $SCRATCH/logs/oracle.log" >&2
        exit 1
    fi
    echo ""

    # Wait a few more seconds for the title screen to be interactive.
    echo "[oracle_boot] Waiting for title screen (5s)..."
    sleep 5

    # Fast re-entry.
    fast_reenter
}

do_reenter() {
    if ! rpc_alive; then
        echo "[oracle_boot] ERROR: RPC not live. Run oracle_boot.sh first." >&2
        exit 1
    fi
    fast_reenter
}

# ── dispatch ──────────────────────────────────────────────────────────────────
case "$CMD" in
    boot)    do_boot    ;;
    stop)    do_stop    ;;
    status)  do_status  ;;
    reenter) do_reenter ;;
esac

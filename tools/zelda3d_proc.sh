# Shared process-identification helper for the single zelda3d launcher binary.
#
# One program binary now runs BOTH games ("zelda3d oot" / "zelda3d mm"), so every running instance
# -- OoT, MM, a parallel agent's instance of either -- shares the SAME /proc/<pid>/exe target. A
# name- or path-based `pkill -f`/readlink match can no longer tell them apart, and would silently
# kill a concurrently-running sibling game or another agent's run. Source this file and use
# zelda3d_pids() instead of hand-rolling that match.
#
# Not executable on its own -- `. tools/zelda3d_proc.sh` from a script that already has `set -u`.

# zelda3d_pids <bin_path> <game_id>
#   Lists the pids of every running process whose /proc/<pid>/exe resolves to <bin_path> (with OR
#   without a trailing " (deleted)", for a rebuilt-over binary) AND whose /proc/<pid>/cmdline names
#   <game_id> ("oot" | "mm") as argv[1] (NUL-separated cmdline, so this is argv[1] exactly, not a
#   substring match). Both conditions must hold -- exe match alone is exactly the ambiguity this
#   exists to close.
#   Prints one pid per line; prints nothing when no such process exists. Callers doing a status/log
#   report should say what was searched (bin_path + game_id) rather than just showing an empty list,
#   so "found nothing" doesn't read the same as "didn't look".
zelda3d_pids() {
    local bin="$1" game="$2" p t pid arg1
    for p in /proc/*/exe; do
        t="$(readlink "$p" 2>/dev/null)" || continue
        case "$t" in
            "$bin"|"$bin ("*) ;;
            *) continue ;;
        esac
        pid="${p#/proc/}"; pid="${pid%/exe}"
        arg1="$(tr '\0' '\n' < "/proc/$pid/cmdline" 2>/dev/null | sed -n '2p')"
        [ "$arg1" = "$game" ] && echo "$pid"
    done
}

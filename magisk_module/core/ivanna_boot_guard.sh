#!/system/bin/sh
# Consecutive IVANNA runtime-health guard.
# A boot is considered healthy only after service.sh observes both the daemon
# process and its abstract control socket. This is deliberately not a claim
# that the entire Android boot completed.

ivanna_boot_guard_update() {
    local count_file="$1"
    local timestamp_file="$2"
    local healthy_marker="$3"
    local now="$4"
    local window="${5:-600}"
    local count last_ts count_tmp ts_tmp

    case "$now:$window" in
        *[!0-9:]*|:*) return 2 ;;
    esac
    [ -n "$count_file" ] && [ -n "$timestamp_file" ] && [ -n "$healthy_marker" ] || return 2

    count=$(cat "$count_file" 2>/dev/null)
    last_ts=$(cat "$timestamp_file" 2>/dev/null)
    case "$count" in ''|*[!0-9]*) count=0 ;; esac
    case "$last_ts" in ''|*[!0-9]*) last_ts=0 ;; esac

    # A health marker from the previous boot clears the failure streak.
    # Consume it so a stale marker cannot clear subsequent failed boots.
    if [ -f "$healthy_marker" ]; then
        if ! rm -f "$healthy_marker"; then
            return 1
        fi
        count=0
    elif [ "$last_ts" -gt 0 ] 2>/dev/null &&
         [ "$now" -ge "$last_ts" ] 2>/dev/null &&
         [ $((now - last_ts)) -gt "$window" ] 2>/dev/null; then
        # A later manual reboot is not part of the same crash loop.
        count=0
    fi

    count=$((count + 1))
    count_tmp="${count_file}.tmp.$$"
    ts_tmp="${timestamp_file}.tmp.$$"
    if ! printf '%s\n' "$count" > "$count_tmp" ||
       ! printf '%s\n' "$now" > "$ts_tmp"; then
        rm -f "$count_tmp" "$ts_tmp"
        return 1
    fi
    if ! mv -f "$count_tmp" "$count_file"; then
        rm -f "$count_tmp" "$ts_tmp"
        return 1
    fi
    if ! mv -f "$ts_tmp" "$timestamp_file"; then
        rm -f "$ts_tmp"
        return 1
    fi

    IVANNA_BOOT_STREAK="$count"
    return 0
}

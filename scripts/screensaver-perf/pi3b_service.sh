#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON the Pi through perf_run_remote.
#   ACTION=stop     stops the service and waits for the app to exit
#   ACTION=start    starts it, waits for the control socket, prints what runs
#   ACTION=dropin   writes ENV_EXTRA ("KEY=VALUE ...") as a systemd drop-in; empty removes it
#   ACTION=collect  prints FILE, then deletes it
#   ACTION=crashes  prints how many app crashes the journal holds since SINCE (UTC)
#   ACTION=journal  prints the app's journal lines since SINCE (UTC) that match PATTERN (ERE)
#   ACTION=display_value  prints /display/KEY from the app's settings.json as JSON (null when absent)
#   ACTION=unset_display  stops the app, removes /display/KEY from settings.json, starts the app
#   ACTION=app_env  prints the running app's HELIX_* environment, one KEY=VALUE per line
#   ACTION=print_state  prints print_stats.state from the Moonraker in the app's settings:
#                   "none" when nothing listens there, "unknown" on any other failure
set -u
PW=${PW:-}
INSTALL=${INSTALL:?}
CTL_SOCK=${CTL_SOCK:?}
ACTION=${ACTION:?}
ENV_EXTRA=${ENV_EXTRA:-}
FILE=${FILE:-}
SINCE=${SINCE:-}
PATTERN=${PATTERN:-}
KEY=${KEY:-}
DROPIN_DIR=/etc/systemd/system/helixscreen.service.d
DROPIN=$DROPIN_DIR/helix-perf.conf

sudo_() {
    if [ -n "$PW" ]; then
        printf '%s\n' "$PW" | sudo -S -p '' "$@"
    else
        sudo -n "$@"
    fi
}

app_running() {
    pidof helix-screen helix-screen-egl >/dev/null
}

ctl_answers() {
    "$INSTALL/bin/helix-screen-egl" ctl -s "$CTL_SOCK" ping < /dev/null 2>/dev/null | grep -q pong
}

case $ACTION in
stop)
    sudo_ systemctl stop helixscreen
    for _ in $(seq 75); do
        app_running || break
        sleep 0.2
    done
    if app_running; then
        echo "DEPLOY_FAIL still running"
        exit 1
    fi
    ;;
start)
    sudo_ systemctl start helixscreen
    for _ in $(seq 90); do
        ctl_answers && break
        sleep 1
    done
    echo "DEVICE_VERSION $("$INSTALL/bin/helix-screen" --version 2>&1 | head -n 1)"
    echo "DEVICE_RUNG $(ps -eo args | grep -o "[/]${INSTALL#/}/bin/helix-screen[-a-z]*" | grep -v watchdog | sort -u | tr '\n' ' ')"
    echo "DEVICE_EGL_SHA $(sha256sum "$INSTALL/bin/helix-screen-egl" | cut -c1-16)"
    if ctl_answers; then
        echo DEPLOY_OK
    else
        echo "DEPLOY_FAIL ctl not answering"
    fi
    ;;
dropin)
    if [ -n "$ENV_EXTRA" ]; then
        {
            echo "[Service]"
            for kv in $ENV_EXTRA; do
                echo "Environment=$kv"
            done
        } > /tmp/helix-perf.conf
        sudo_ mkdir -p "$DROPIN_DIR"
        sudo_ cp /tmp/helix-perf.conf "$DROPIN"
        rm -f /tmp/helix-perf.conf
        sudo_ cat "$DROPIN"
    else
        sudo_ rm -f "$DROPIN"
    fi
    sudo_ systemctl daemon-reload
    ;;
collect)
    cat "${FILE:?}"
    sudo_ rm -f "$FILE"
    ;;
crashes)
    sudo_ journalctl -u helixscreen --since "${SINCE:?} UTC" --no-pager -o cat 2>/dev/null |
        grep -c 'Child exited with code 139'
    ;;
journal)
    sudo_ journalctl -u helixscreen --since "${SINCE:?} UTC" --no-pager -o cat 2>/dev/null |
        grep -E "${PATTERN:?}"
    ;;
display_value)
    python3 -c 'import json, sys; print(json.dumps(json.load(open(sys.argv[1])).get("display", {}).get(sys.argv[2]), sort_keys=True))' \
        "$INSTALL/config/settings.json" "${KEY:?}"
    ;;
unset_display)
    sudo_ systemctl stop helixscreen
    sudo_ python3 -c 'import json, sys; p = sys.argv[1]; c = json.load(open(p)); c.get("display", {}).pop(sys.argv[2], None); json.dump(c, open(p, "w"), indent=2)' \
        "$INSTALL/config/settings.json" "${KEY:?}"
    sudo_ systemctl start helixscreen
    ;;
app_env)
    pid=$(ps -eo pid,comm | awk '$2 ~ /^helix-screen/ {print $1; exit}')
    sudo_ cat "/proc/$pid/environ" 2>/dev/null | tr '\0' '\n' | grep -E '^HELIX_'
    ;;
print_state)
    python3 -c 'import json, sys, urllib.error, urllib.request
settings = json.load(open(sys.argv[1]))
printer = settings.get("printers", {}).get(settings.get("active_printer_id") or "default", {})
url = "http://%s:%s/printer/objects/query?print_stats=state" % (
    printer.get("moonraker_host") or "127.0.0.1", printer.get("moonraker_port") or 7125)
try:
    reply = json.load(urllib.request.urlopen(url, timeout=5))
    print(reply["result"]["status"]["print_stats"]["state"])
except urllib.error.URLError as error:
    print("none" if isinstance(error.reason, ConnectionRefusedError) else "unknown")
except Exception:
    print("unknown")' "$INSTALL/config/settings.json"
    ;;
*)
    echo "pi3b_service: unknown ACTION '$ACTION'" >&2
    exit 2
    ;;
esac

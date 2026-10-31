#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ensure-polkit-rule.sh - Install the polkit rules a non-root HelixScreen
# service user needs
#
# Called by ExecStartPre=+ in helixscreen.service (runs as root) on every
# start, so an install self-heals on its next restart, and by the installer's
# install_permission_rules().
#
# Usage: ensure-polkit-rule.sh <username>
#
# Two independent rules:
#   network  NetworkManager access, only where nmcli exists. Left alone when
#            any HelixScreen network rule is already present.
#   power    logind power-off and reboot for this host. A service has no seat
#            session, so logind's default answer for it is "authentication
#            required". Rewritten whenever its content differs, which repairs
#            a changed service user. A root user gets none, and any left by an
#            earlier non-root user is removed.
#
# Writes .rules for polkit >= 0.106 (Debian 12+) and .pkla for 0.105 (Debian
# 11), going by `pkaction --version`; when that is unavailable, by which
# directory exists. polkitd watches both directories and reloads on change.
# A user name that is not a plausible login name gets no rule at all.
# Always exits 0: a rule it cannot write must not stop HelixScreen starting.
#
# HELIX_POLKIT_DIR overrides /etc/polkit-1 for tests.

HELIX_USER="${1:-root}"
POLKIT_DIR="${HELIX_POLKIT_DIR:-/etc/polkit-1}"
RULES_DIR="$POLKIT_DIR/rules.d"
PKLA_DIR="$POLKIT_DIR/localauthority/50-local.d"

POWER_RULES_FILE="$RULES_DIR/49-helixscreen-power.rules"
POWER_PKLA_FILE="$PKLA_DIR/helixscreen-power.pkla"

# Root needs no rules, and must not inherit a previous user's power grant
if [ "$HELIX_USER" = "root" ]; then
    rm -f "$POWER_RULES_FILE" "$POWER_PKLA_FILE" 2>/dev/null
    exit 0
fi

# valid_user <name>: ^[a-z_][a-z0-9_-]*\$?$, the name is pasted into JS and .pkla
valid_user() {
    case "$1" in
        '' | [!a-z_]*) return 1 ;;
    esac
    case "${1%\$}" in
        *[!a-z0-9_-]*) return 1 ;;
    esac
    return 0
}

if ! valid_user "$HELIX_USER"; then
    echo "ensure-polkit-rule: refusing user name '$HELIX_USER'; no polkit rules written" >&2
    exit 0
fi

# rule_format: prints "rules", "pkla", or nothing when neither can be used.
rule_format() {
    v=$(pkaction --version 2>/dev/null | sed -n 's/.*version[[:space:]]*//p' | head -n 1)
    major=${v%%.*}
    minor=""
    case "$v" in
        *.*) minor=${v#*.}; minor=${minor%%.*} ;;
    esac
    case "$major" in
        '' | *[!0-9]*) major="" ;;
    esac
    case "$minor" in
        *[!0-9]*) minor="" ;;
    esac
    if [ -n "$major" ]; then
        if [ "$major" -gt 0 ] || [ "${minor:-0}" -ge 106 ]; then
            [ -d "$RULES_DIR" ] && echo rules
        else
            [ -d "$PKLA_DIR" ] && echo pkla
        fi
        return 0
    fi
    if [ -d "$RULES_DIR" ]; then
        echo rules
    elif [ -d "$PKLA_DIR" ]; then
        echo pkla
    fi
}
FORMAT=$(rule_format)

# write_if_changed <dest> <content>: rewrite <dest> only when it differs.
write_if_changed() {
    if [ -f "$1" ] && [ "$(cat "$1")" = "$2" ]; then
        return 0
    fi
    if ! printf '%s\n' "$2" 2>/dev/null > "$1"; then
        echo "ensure-polkit-rule: cannot write $1" >&2
        return 0
    fi
    chmod 644 "$1" 2>/dev/null || true
    echo "ensure-polkit-rule: installed $1"
}

ensure_network_rule() {
    command -v nmcli >/dev/null 2>&1 || return 0

    [ -f "$RULES_DIR/49-helixscreen-network.rules" ] && return 0
    [ -f "$RULES_DIR/50-helixscreen-network.rules" ] && return 0
    [ -f "$PKLA_DIR/helixscreen-network.pkla" ] && return 0

    if [ "$FORMAT" = rules ]; then
        write_if_changed "$RULES_DIR/49-helixscreen-network.rules" "\
// Installed by HelixScreen - allow service user to manage NetworkManager
polkit.addRule(function(action, subject) {
    if (action.id.indexOf(\"org.freedesktop.NetworkManager.\") === 0 &&
        subject.user === \"${HELIX_USER}\") {
        return polkit.Result.YES;
    }
});"
    elif [ "$FORMAT" = pkla ]; then
        write_if_changed "$PKLA_DIR/helixscreen-network.pkla" "\
# Installed by HelixScreen - allow service user to manage NetworkManager
# connections (Wi-Fi connect/disconnect/scan)
[HelixScreen NetworkManager access]
Identity=unix-user:${HELIX_USER}
Action=org.freedesktop.NetworkManager.*
ResultAny=yes
ResultInactive=yes
ResultActive=yes"
    fi
}

# The actions logind checks for PowerOff/Reboot: the plain one and the one for
# when other users are logged in. The *-ignore-inhibit actions are left out on
# purpose, so a block inhibitor (a package upgrade) still holds the host up.
POWER_ACTIONS="org.freedesktop.login1.power-off
org.freedesktop.login1.power-off-multiple-sessions
org.freedesktop.login1.reboot
org.freedesktop.login1.reboot-multiple-sessions"

ensure_power_rule() {
    if [ "$FORMAT" = rules ]; then
        ids=$(printf '%s\n' "$POWER_ACTIONS" | sed 's/.*/        "&",/; $ s/,$//')
        write_if_changed "$POWER_RULES_FILE" "\
// Installed by HelixScreen - allow service user to power off and reboot this host
polkit.addRule(function(action, subject) {
    var actions = [
${ids}
    ];
    if (actions.indexOf(action.id) >= 0 && subject.user === \"${HELIX_USER}\") {
        return polkit.Result.YES;
    }
});"
        rm -f "$POWER_PKLA_FILE"
    elif [ "$FORMAT" = pkla ]; then
        write_if_changed "$POWER_PKLA_FILE" "\
# Installed by HelixScreen - allow service user to power off and reboot this host
[HelixScreen power off and reboot]
Identity=unix-user:${HELIX_USER}
Action=$(printf '%s' "$POWER_ACTIONS" | tr '\n' ';')
ResultAny=yes
ResultInactive=yes
ResultActive=yes"
    fi
}

ensure_network_rule
ensure_power_rule
exit 0

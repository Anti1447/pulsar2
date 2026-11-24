#!/usr/bin/env bash
# Re-install every packaged bot release (rlbot-run/releases = Pulsar2, rlbot-run/releases_gco = PulsarGCO) into the Windows RLBot v5 bots folder,
# without re-packaging. Needed after an RLBot v5 botpack update, which REPLACES %LOCALAPPDATA%\RLBot5\bots and removes our folders.
# Same file set the package scripts install: bot.toml, release.json, pulsar-assets (loadout, logo) and a generated run_wsl.bat.
#   tools/reinstall_bots.sh            # all versions of both lineages
#   tools/reinstall_bots.sh --latest   # only the newest version of each
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$REPO/rlbot-run/pulsar-assets"
DISTRO="${WSL_DISTRO_NAME:-Ubuntu-24.04}"; WSL_USER="$(id -un)"
LAD="$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')"
WIN_BOTS="$(wslpath "$LAD")/RLBot5/bots"
[ -d "$WIN_BOTS" ] || { echo "RLBot v5 bots folder not found: $WIN_BOTS" >&2; exit 1; }
install_one() { # <bot name> <release dir>
	local name="$1" rel="$2" tag; tag="$(basename "$rel")"; local wd="$WIN_BOTS/$name/$tag"
	[ -f "$rel/bot.toml" ] && [ -f "$rel/run.sh" ] || { echo "skip $rel (incomplete)"; return; }
	mkdir -p "$wd"; cp "$rel/bot.toml" "$rel/release.json" "$wd/"
	for f in "$ASSETS"/*; do case "$f" in *bot.toml.template|*Zone.Identifier) ;; *) cp "$f" "$wd/" ;; esac; done
	printf '@echo off\r\nrem Starts %s %s inside WSL. RLBot passes RLBOT_* variables; WSLENV forwards them.\r\nset "WSLENV=RLBOT_SERVER_IP:RLBOT_SERVER_PORT:RLBOT_AGENT_ID:%%WSLENV%%"\r\nwsl.exe -d %s -u %s -- %s\r\n' \
		"$name" "$tag" "$DISTRO" "$WSL_USER" "$rel/run.sh" > "$wd/run_wsl.bat"
}
for pair in "Pulsar2:$REPO/rlbot-run/releases" "PulsarGCO:$REPO/rlbot-run/releases_gco"; do
	name="${pair%%:*}"; root="${pair#*:}"
	vers=$(ls "$root" | sort -V)
	[ "${1:-}" = "--latest" ] && vers=$(echo "$vers" | tail -1)
	for v in $vers; do install_one "$name" "$root/$v"; done
	echo "$name: $(echo "$vers" | wc -l) release(s) installed under $WIN_BOTS/$name"
done
echo "Refresh or restart the RLBot v5 app."

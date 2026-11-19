#!/usr/bin/env bash
# Package a GCO fresh-run checkpoint as the RLBot v5 bot "PulsarGCO vX.Y.Z" and install it into the Windows RLBot v5 bots folder.
# Separate lineage from tools/package_bot.sh (Pulsar2 vX.Y.Z): own release root, own Windows folder, own version counter.
#
#   tools/package_gco.sh                         # newest checkpoint of build/checkpoints_gco_fresh, next version (first release = 1.0.0)
#   tools/package_gco.sh --checkpoint 1500000000 # a specific step (or a folder path)
#   tools/package_gco.sh --version 1.4.0         # explicit version (must not already exist)
#   tools/package_gco.sh --no-install            # build the release in WSL only
#   tools/package_gco.sh --dry-run               # show what would happen
#   tools/package_gco.sh --force                 # release even if identical weights+code already exist
#   tools/package_gco.sh --list                  # list releases
#   CKPT_DIR=/path tools/package_gco.sh          # another checkpoint root
#
# VERSIONING (carry counter, every digit 0-9): 1.0.0 -> 1.0.1 ... 1.0.9 -> 1.1.0 ... 1.9.9 -> 2.0.0.
#
# Layout (WSL):     rlbot-run/releases_gco/vX.Y.Z/{bot.toml,run.sh,bin/,checkpoint/,release.json}
# Layout (Windows): %LOCALAPPDATA%\RLBot5\bots\PulsarGCO\vX.Y.Z\{bot.toml,loadout.toml,pulsar.jpg,run_wsl.bat,release.json}
# Duplicate guard: the same checkpoint content + bot code is never released twice (exit 3) unless --force.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"
CKPT_DIR="${CKPT_DIR:-$BUILD/checkpoints_gco_fresh}"
REL_ROOT="$REPO/rlbot-run/releases_gco"
ASSETS="$REPO/rlbot-run/pulsar-assets"
BOT_NAME="PulsarGCO"
DISTRO="${WSL_DISTRO_NAME:-Ubuntu-24.04}"
WSL_USER="$(id -un)"

EXPLICIT=""; CKPT_ARG=""; INSTALL=1; DRY=0; FORCE=0; LIST=0
while [ $# -gt 0 ]; do
	case "$1" in
		--version) EXPLICIT="${2:?--version needs X.Y.Z}"; shift ;;
		--checkpoint) CKPT_ARG="${2:?--checkpoint needs a step or folder}"; shift ;;
		--no-install) INSTALL=0 ;;
		--dry-run) DRY=1 ;;
		--force) FORCE=1 ;;
		--list) LIST=1 ;;
		-h|--help) sed -n '2,19p' "$0"; exit 0 ;;
		*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
	esac
	shift
done

die() { echo "error: $*" >&2; exit 1; }
versions() { [ -d "$REL_ROOT" ] && ls -1 "$REL_ROOT" | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sed 's/^v//' | sort -V || true; }
# Carry counter: patch and minor roll over at 10 (1.0.9 -> 1.1.0, 1.9.9 -> 2.0.0).
next_version() {
	local a b c; IFS=. read -r a b c <<<"$1"
	c=$((c+1))
	if [ "$c" -ge 10 ]; then c=0; b=$((b+1)); fi
	if [ "$b" -ge 10 ]; then b=0; a=$((a+1)); fi
	echo "$a.$b.$c"
}
json_get() { { grep -o "\"$2\": *\"[^\"]*\"" "$1" 2>/dev/null || true; } | head -n1 | sed 's/.*: *"\(.*\)"/\1/'; }
ckpt_sha() { cat "$1/SHARED_HEAD.lt" "$1/POLICY.lt" "$1/RUNNING_STATS.json" 2>/dev/null | sha256sum | cut -c1-16; }

if [ "$LIST" = 1 ]; then
	versions | while read -r v; do
		j="$REL_ROOT/v$v/release.json"
		printf 'v%-8s step %-14s %s\n' "$v" "$(json_get "$j" checkpoint_step)" "$(json_get "$j" created)"
	done
	exit 0
fi

# ---- pick the checkpoint -------------------------------------------------------------
if [ -z "$CKPT_ARG" ]; then
	STEP="$(ls -1 "$CKPT_DIR" 2>/dev/null | grep -E '^[0-9]+$' | sort -n | tail -n1)"
	[ -n "$STEP" ] || die "no numbered checkpoints in $CKPT_DIR"
	CKPT="$CKPT_DIR/$STEP"
elif [ -d "$CKPT_ARG" ]; then
	CKPT="$(cd "$CKPT_ARG" && pwd)"; STEP="$(basename "$CKPT")"
else
	CKPT="$CKPT_DIR/$CKPT_ARG"; STEP="$CKPT_ARG"
fi
for f in SHARED_HEAD.lt POLICY.lt RUNNING_STATS.json; do
	[ -f "$CKPT/$f" ] || die "checkpoint $CKPT is missing $f"
done
[ -f "$ASSETS/bot.toml.template" ] || die "$ASSETS/bot.toml.template not found"
for f in GigaLearnRLBot libGigaLearnCPP.so; do
	[ -f "$BUILD/$f" ] || die "$BUILD/$f not found; build first: cmake --build build -j\$(nproc)"
done

# ---- duplicate check ------------------------------------------------------------------
SRC_HASH="$(cat "$REPO/src/RLBotMain.cpp" "$REPO/src/RLBotClient.cpp" "$REPO/src/ExampleMain.cpp" | sha256sum | cut -c1-12)"
CK_SHA="$(ckpt_sha "$CKPT")"
LATEST="$(versions | tail -n1)"
DUPS=""
for v in $(versions); do
	j="$REL_ROOT/v$v/release.json"
	rsha="$(json_get "$j" checkpoint_sha)"
	if [ "$rsha" = "$CK_SHA" ] && [ "$(json_get "$j" source_hash)" = "$SRC_HASH" ]; then DUPS="$DUPS v$v"; fi
done
if [ -n "$DUPS" ] && [ "$FORCE" = 0 ]; then
	echo "Nothing to release: this exact bot already exists as${DUPS} (weights hash $CK_SHA, source hash $SRC_HASH). --force overrides." >&2
	exit 3
fi

# ---- version -------------------------------------------------------------------------
if [ -n "$EXPLICIT" ]; then
	[[ "$EXPLICIT" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must look like 1.2.3"
	[ ! -e "$REL_ROOT/v$EXPLICIT" ] || die "v$EXPLICIT already exists; releases are never overwritten"
	VER="$EXPLICIT"
elif [ -z "$LATEST" ]; then
	VER="1.0.0"
else
	VER="$(next_version "$LATEST")"
fi
TAG="v$VER"
REL="$REL_ROOT/$TAG"

WIN_BOTS=""
if [ "$INSTALL" = 1 ]; then
	LAD="$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r' || true)"
	[ -n "$LAD" ] || die "cannot read %LOCALAPPDATA% (run inside WSL, or pass --no-install)"
	WIN_BOTS="$(wslpath "$LAD")/RLBot5/bots"
	[ -d "$WIN_BOTS" ] || die "RLBot v5 bots folder not found: $WIN_BOTS (install RLBot v5, or pass --no-install)"
fi

echo "release    : $BOT_NAME $TAG   (previous: ${LATEST:+v}${LATEST:-none})"
echo "checkpoint : $STEP   ($(basename "$CKPT"), content hash $CK_SHA)"
echo "source hash: $SRC_HASH   git: $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet -- src || echo '+local-edits')"
echo "WSL folder : $REL"
[ "$INSTALL" = 1 ] && echo "Windows    : $WIN_BOTS/$BOT_NAME/$TAG" || echo "Windows    : (skipped, --no-install)"
[ "$DRY" = 1 ] && { echo "dry run: nothing written"; exit 0; }

# ---- build the WSL release -----------------------------------------------------------
mkdir -p "$REL/bin" "$REL/checkpoint"
cp "$BUILD/GigaLearnRLBot" "$BUILD/libGigaLearnCPP.so" "$REL/bin/"
cp -a "$BUILD"/libRLBotCPP.so* "$REL/bin/" 2>/dev/null || true
cp "$CKPT/SHARED_HEAD.lt" "$CKPT/POLICY.lt" "$CKPT/RUNNING_STATS.json" "$REL/checkpoint/"

# The GCO nets read the 278-wide ball-prediction obs (GGL_BALLPRED=1; the client needs the collision meshes to predict the ball).
cat > "$REL/run.sh" <<EOF
#!/bin/bash
# Launched by RLBot v5 (through the Windows run_wsl.bat). RLBOT_SERVER_IP/PORT/AGENT_ID come from the server.
HERE="\$(cd "\$(dirname "\$0")" && pwd)"
LIBTORCH="$REPO/GigaLearnCPP/libtorch/lib"
export LD_LIBRARY_PATH="\$HERE/bin:\$LIBTORCH:\${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
export GGL_BALLPRED=1
export GGL_COLLISION_MESHES="$BUILD/collision_meshes"
# WSL2 NAT: 127.0.0.1 is WSL itself, not the Windows RLBot server. Point at the Windows host instead.
if [ "\$(wslinfo --networking-mode 2>/dev/null)" != "mirrored" ]; then
	case "\${RLBOT_SERVER_IP:-127.0.0.1}" in
		127.0.0.1|localhost|"")
			GW="\$(ip route | awk '/^default/{print \$3; exit}')"
			[ -n "\$GW" ] && export RLBOT_SERVER_IP="\$GW" ;;
	esac
fi
LOG="\$HERE/bot.\$\$.log"
{ echo "=== \$(date) server=\${RLBOT_SERVER_IP:-?}:\${RLBOT_SERVER_PORT:-?} agent=\${RLBOT_AGENT_ID:-?} ==="; } > "\$LOG"
"\$HERE/bin/GigaLearnRLBot" "\$HERE/checkpoint" >> "\$LOG" 2>&1
echo "=== GigaLearnRLBot exited with code \$? ===" >> "\$LOG"
EOF
chmod +x "$REL/run.sh"

sed -e "s|@TAG@|$TAG|g" -e "s|@STEP@|$STEP|g" "$ASSETS/bot.toml.template" > "$REL/bot.toml"
sed -i "s|^name = .*|name = \"$BOT_NAME $TAG\"|; s|^agent_id = .*|agent_id = \"pulsar2/gigalearn/pulsargco/$TAG\"|" "$REL/bot.toml"
sed -i "s|^description = .*|description = \"PulsarGCO $TAG: goal-only (GCO) fresh-start 1v1 PPO bot, checkpoint $STEP. Runs inside WSL2.\"|" "$REL/bot.toml"
for f in "$ASSETS"/*; do
	case "$f" in *bot.toml.template|*Zone.Identifier) ;; *) cp "$f" "$REL/" ;; esac
done

cat > "$REL/release.json" <<EOF
{
  "version": "$VER",
  "bot": "$BOT_NAME",
  "checkpoint_step": "$STEP",
  "checkpoint_name": "$(basename "$CKPT")",
  "checkpoint_sha": "$CK_SHA",
  "source_hash": "$SRC_HASH",
  "git_commit": "$(git -C "$REPO" rev-parse --short HEAD)",
  "created": "$(date -Iseconds)",
  "wsl_distro": "$DISTRO",
  "wsl_user": "$WSL_USER"
}
EOF
echo "built $REL"

# ---- install on Windows --------------------------------------------------------------
if [ "$INSTALL" = 1 ]; then
	WD="$WIN_BOTS/$BOT_NAME/$TAG"
	mkdir -p "$WD"
	cp "$REL/bot.toml" "$REL/release.json" "$WD/"
	for f in "$ASSETS"/*; do case "$f" in *bot.toml.template|*Zone.Identifier) ;; *) cp "$f" "$WD/" ;; esac; done
	printf '@echo off\r\nrem Starts %s %s inside WSL. RLBot passes RLBOT_* variables; WSLENV forwards them.\r\nset "WSLENV=RLBOT_SERVER_IP:RLBOT_SERVER_PORT:RLBOT_AGENT_ID:%%WSLENV%%"\r\nwsl.exe -d %s -u %s -- %s\r\n' \
		"$BOT_NAME" "$TAG" "$DISTRO" "$WSL_USER" "$REL/run.sh" > "$WD/run_wsl.bat"
	echo "installed $WD"
	echo "In the RLBot v5 app, refresh or restart it; pick \"$BOT_NAME $TAG\"."
fi

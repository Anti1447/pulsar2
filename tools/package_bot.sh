#!/usr/bin/env bash
# Package the newest (or a chosen) checkpoint as a versioned RLBot v5 bot and install it
# into the Windows RLBot v5 bots folder.
#
#   tools/package_bot.sh                      # newest checkpoint, bump PATCH (first release = 0.1.0)
#   tools/package_bot.sh --minor              # bump minor  (0.1.3 -> 0.2.0)
#   tools/package_bot.sh --major              # bump major  (0.1.3 -> 1.0.0)
#   tools/package_bot.sh --version 1.2.3      # explicit version (must not already exist)
#   tools/package_bot.sh --checkpoint 2200132352     # a specific step in checkpoints_5.3
#   tools/package_bot.sh --checkpoint /path/to/ckpt  # or a folder
#   tools/package_bot.sh --no-install         # build the release in WSL only
#   tools/package_bot.sh --dry-run            # show what would happen
#   tools/package_bot.sh --force              # release even if nothing changed since the last one
#   tools/package_bot.sh --best               # strongest-rated checkpoint (best_r<rating>_<steps>) instead of the newest
#   tools/package_best_bot.sh                 # same as --best
#   tools/package_bot.sh --list               # list releases
#
# Duplicate guard: before anything is written, the checkpoint CONTENT (weights + stats) and the bot code are
# compared with EVERY existing release. The same exact bot is never released twice (exit code 3), even if it
# arrives under a different folder name (e.g. best_r1178_N and N are identical). --force overrides.
#
# Layout (WSL):    rlbot-run/releases/vX.Y.Z/{bot.toml,run.sh,bin/,checkpoint/,release.json}
# Layout (Windows): %LOCALAPPDATA%\RLBot5\bots\Pulsar2\vX.Y.Z\{bot.toml,loadout.toml,pulsar.jpg,run_wsl.bat,release.json}
# bot.toml/loadout/logo come from rlbot-run/pulsar-assets/ (edit there, not in the release folders).
# The Windows folder is a thin launcher: RLBot runs run_wsl.bat, which starts the Linux bot in WSL.
# Run from anywhere. Needs a finished Release build in build/ (GigaLearnRLBot).

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"
CKPT_DIR="${CKPT_DIR:-$BUILD/checkpoints_5.3}"
REL_ROOT="$REPO/rlbot-run/releases"
ASSETS="$REPO/rlbot-run/pulsar-assets"   # bot.toml.template + loadout.toml + logo; edit these to change every future release
DISTRO="${WSL_DISTRO_NAME:-Ubuntu-24.04}"
WSL_USER="$(id -un)"

BUMP=patch; EXPLICIT=""; CKPT_ARG=""; INSTALL=1; DRY=0; FORCE=0; LIST=0; BEST=0
while [ $# -gt 0 ]; do
	case "$1" in
		--patch) BUMP=patch ;;
		--minor) BUMP=minor ;;
		--major) BUMP=major ;;
		--version) EXPLICIT="${2:?--version needs X.Y.Z}"; shift ;;
		--checkpoint) CKPT_ARG="${2:?--checkpoint needs a step or folder}"; shift ;;
		--best) BEST=1 ;;
		--no-install) INSTALL=0 ;;
		--dry-run) DRY=1 ;;
		--force) FORCE=1 ;;
		--list) LIST=1 ;;
		-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
		*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
	esac
	shift
done

die() { echo "error: $*" >&2; exit 1; }
versions() { [ -d "$REL_ROOT" ] && ls -1 "$REL_ROOT" | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sed 's/^v//' | sort -V || true; }
bump() { local a b c; IFS=. read -r a b c <<<"$1"
	case "$2" in major) echo "$((a+1)).0.0";; minor) echo "$a.$((b+1)).0";; patch) echo "$a.$b.$((c+1))";; esac; }
json_get() { { grep -o "\"$2\": *\"[^\"]*\"" "$1" 2>/dev/null || true; } | head -n1 | sed 's/.*: *"\(.*\)"/\1/'; }
# Identity of a checkpoint = its content, not its folder name (best_r1178_N and N can be the same bytes).
ckpt_sha() { cat "$1/SHARED_HEAD.lt" "$1/POLICY.lt" "$1/RUNNING_STATS.json" 2>/dev/null | sha256sum | cut -c1-16; }
ckpt_complete() { [ -f "$1/SHARED_HEAD.lt" ] && [ -f "$1/POLICY.lt" ] && [ -f "$1/RUNNING_STATS.json" ]; }

if [ "$LIST" = 1 ]; then
	versions | while read -r v; do
		j="$REL_ROOT/v$v/release.json"; r="$(json_get "$j" rating)"
		printf 'v%-8s step %-12s %-9s %s\n' "$v" "$(json_get "$j" checkpoint_step)" "${r:+rating $r}" "$(json_get "$j" created)"
	done
	exit 0
fi

# ---- pick the checkpoint -------------------------------------------------------------
RATING=""
if [ "$BEST" = 1 ]; then
	[ -z "$CKPT_ARG" ] || die "--best chooses the checkpoint itself; do not combine it with --checkpoint"
	# best_r<rating>_<steps> folders are the trainer's own archive of its best-rated snapshots.
	CAND="$(ls -1 "$CKPT_DIR" 2>/dev/null | sed -nE 's/^(best_r(-?[0-9]+)_([0-9]+))$/\2 \3 \1/p' | sort -k1,1nr -k2,2nr)"
	[ -n "$CAND" ] || die "no best_r<rating>_<steps> folders in $CKPT_DIR (the trainer has not archived a best-rated checkpoint yet)"
	CKPT=""
	while read -r r s name; do
		if ckpt_complete "$CKPT_DIR/$name"; then CKPT="$CKPT_DIR/$name"; RATING="$r"; STEP="$s"; break; fi
		echo "note: skipping $name (incomplete)" >&2
	done <<<"$CAND"
	[ -n "$CKPT" ] || die "no complete best_r* checkpoint found"
elif [ -z "$CKPT_ARG" ]; then
	STEP="$(ls -1 "$CKPT_DIR" 2>/dev/null | grep -E '^[0-9]+$' | sort -n | tail -n1)"
	[ -n "$STEP" ] || die "no numbered checkpoints in $CKPT_DIR"
	CKPT="$CKPT_DIR/$STEP"
elif [ -d "$CKPT_ARG" ]; then
	CKPT="$(cd "$CKPT_ARG" && pwd)"; STEP="$(basename "$CKPT")"
else
	CKPT="$CKPT_DIR/$CKPT_ARG"; STEP="$CKPT_ARG"
fi
CK_NAME="$(basename "$CKPT")"
if [[ "$CK_NAME" =~ ^best_r(-?[0-9]+)_([0-9]+)$ ]]; then RATING="${BASH_REMATCH[1]}"; STEP="${BASH_REMATCH[2]}"; fi
for f in SHARED_HEAD.lt POLICY.lt RUNNING_STATS.json; do
	[ -f "$CKPT/$f" ] || die "checkpoint $CKPT is missing $f"
done
[ -f "$ASSETS/bot.toml.template" ] || die "$ASSETS/bot.toml.template not found"
for f in GigaLearnRLBot libGigaLearnCPP.so; do
	[ -f "$BUILD/$f" ] || die "$BUILD/$f not found; build first: cmake --build build -j\$(nproc)"
done
STEP_LABEL="$STEP${RATING:+ (rating $RATING)}"
MODE="latest"; [ "$BEST" = 1 ] && MODE="best"

# ---- duplicate check: same weights AND same bot code as ANY existing release ---------------
SRC_HASH="$(cat "$REPO/src/RLBotMain.cpp" "$REPO/src/RLBotClient.cpp" "$REPO/src/ExampleMain.cpp" | sha256sum | cut -c1-12)"
CK_SHA="$(ckpt_sha "$CKPT")"
LATEST="$(versions | tail -n1)"
DUPS=""; SAME_WEIGHTS=""
for v in $(versions); do
	j="$REL_ROOT/v$v/release.json"
	rsha="$(json_get "$j" checkpoint_sha)"; [ -n "$rsha" ] || rsha="$(ckpt_sha "$REL_ROOT/v$v/checkpoint")"
	if [ "$rsha" = "$CK_SHA" ]; then
		if [ "$(json_get "$j" source_hash)" = "$SRC_HASH" ]; then DUPS="$DUPS v$v"; else SAME_WEIGHTS="$SAME_WEIGHTS v$v"; fi
	fi
done
if [ -n "$DUPS" ] && [ "$FORCE" = 0 ]; then
	echo "Nothing to release: this exact bot already exists as${DUPS}" >&2
	echo "  weights : ${CK_NAME} (step $STEP${RATING:+, rating $RATING}), content hash $CK_SHA" >&2
	echo "  code    : source hash $SRC_HASH (RLBotMain/RLBotClient/ExampleMain)" >&2
	echo "  Use --force to release it again anyway." >&2
	exit 3
fi
[ -n "$SAME_WEIGHTS" ] && echo "note: same weights as${SAME_WEIGHTS}, but the bot code has changed since, so this is a new release"

# ---- version -------------------------------------------------------------------------
if [ -n "$EXPLICIT" ]; then
	[[ "$EXPLICIT" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must look like 1.2.3"
	[ ! -e "$REL_ROOT/v$EXPLICIT" ] || die "v$EXPLICIT already exists; releases are never overwritten"
	VER="$EXPLICIT"
elif [ -z "$LATEST" ]; then
	VER="0.1.0"
else
	VER="$(bump "$LATEST" "$BUMP")"
fi
TAG="v$VER"
REL="$REL_ROOT/$TAG"
# ---- Windows install dir -------------------------------------------------------------
WIN_BOTS=""
if [ "$INSTALL" = 1 ]; then
	LAD="$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r' || true)"
	[ -n "$LAD" ] || die "cannot read %LOCALAPPDATA% (run inside WSL, or pass --no-install)"
	WIN_BOTS="$(wslpath "$LAD")/RLBot5/bots"
	[ -d "$WIN_BOTS" ] || die "RLBot v5 bots folder not found: $WIN_BOTS (install RLBot v5, or pass --no-install)"
fi

echo "release    : $TAG   (previous: ${LATEST:+v}${LATEST:-none})"
echo "checkpoint : $STEP_LABEL   ($CK_NAME, content hash $CK_SHA, mode: $MODE)"
echo "source hash: $SRC_HASH   git: $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet -- src || echo '+local-edits')"
echo "WSL folder : $REL"
[ "$INSTALL" = 1 ] && echo "Windows    : $WIN_BOTS/Pulsar2/$TAG" || echo "Windows    : (skipped, --no-install)"
[ "$DRY" = 1 ] && { echo "dry run: nothing written"; exit 0; }

# ---- build the WSL release -----------------------------------------------------------
mkdir -p "$REL/bin" "$REL/checkpoint"
cp "$BUILD/GigaLearnRLBot" "$BUILD/libGigaLearnCPP.so" "$REL/bin/"
cp -a "$BUILD"/libRLBotCPP.so* "$REL/bin/" 2>/dev/null || true
cp "$CKPT/SHARED_HEAD.lt" "$CKPT/POLICY.lt" "$CKPT/RUNNING_STATS.json" "$REL/checkpoint/"

cat > "$REL/run.sh" <<EOF
#!/bin/bash
# Launched by RLBot v5 (through the Windows run_wsl.bat). RLBOT_SERVER_IP/PORT/AGENT_ID come from the server.
HERE="\$(cd "\$(dirname "\$0")" && pwd)"
LIBTORCH="$REPO/GigaLearnCPP/libtorch/lib"
export LD_LIBRARY_PATH="\$HERE/bin:\$LIBTORCH:\${LD_LIBRARY_PATH:-}"
# One math thread per bot: two bots x 20 threads next to Rocket League starve the CPU and make the bot miss ticks.
export OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
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

# bot.toml, loadout and logo come from rlbot-run/pulsar-assets (placeholders: @TAG@ version tag, @STEP@ checkpoint)
sed -e "s|@TAG@|$TAG|g" -e "s|@STEP@|$STEP_LABEL|g" "$ASSETS/bot.toml.template" > "$REL/bot.toml"
for f in "$ASSETS"/*; do
	case "$f" in *bot.toml.template|*Zone.Identifier) ;; *) cp "$f" "$REL/" ;; esac
done

cat > "$REL/release.json" <<EOF
{
  "version": "$VER",
  "checkpoint_step": "$STEP",
  "checkpoint_name": "$CK_NAME",
  "checkpoint_sha": "$CK_SHA",
  "rating": "$RATING",
  "mode": "$MODE",
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
	WD="$WIN_BOTS/Pulsar2/$TAG"
	mkdir -p "$WD"
	cp "$REL/bot.toml" "$REL/release.json" "$WD/"
	for f in "$ASSETS"/*; do case "$f" in *bot.toml.template|*Zone.Identifier) ;; *) cp "$f" "$WD/" ;; esac; done
	WSLRUN="$REL/run.sh"
	printf '@echo off\r\nrem Starts the Pulsar2 %s bot inside WSL. RLBot passes RLBOT_* variables; WSLENV forwards them.\r\nset "WSLENV=RLBOT_SERVER_IP:RLBOT_SERVER_PORT:RLBOT_AGENT_ID:%%WSLENV%%"\r\nwsl.exe -d %s -u %s -- %s\r\n' \
		"$TAG" "$DISTRO" "$WSL_USER" "$WSLRUN" > "$WD/run_wsl.bat"
	echo "installed $WD"
	echo "In the RLBot v5 app, refresh or restart it; pick \"Pulsar2 $TAG\"."
fi
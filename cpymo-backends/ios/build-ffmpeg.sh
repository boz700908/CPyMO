#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upstream_script="$script_dir/FFmpeg-iOS-build-script/build-ffmpeg.sh"
patched_script="$(mktemp)"
trap 'rm -f "$patched_script"' EXIT

# New Xcode SDKs diagnose this legacy FFmpeg availability check as an error.
# Keep the iOS 8.0 FFmpeg deployment target instead of raising compatibility.
sed 's/CFLAGS="-arch \$ARCH"/CFLAGS="-arch \$ARCH -Wno-error=unguarded-availability-new"/' \
    "$upstream_script" > "$patched_script"

cd "$script_dir/FFmpeg-iOS-build-script"
bash "$patched_script" "$@"

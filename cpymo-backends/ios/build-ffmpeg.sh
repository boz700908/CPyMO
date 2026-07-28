#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upstream_dir="$script_dir/FFmpeg-iOS-build-script"
upstream_script="$upstream_dir/build-ffmpeg.sh"
patched_script="$(mktemp "$upstream_dir/.build-ffmpeg.XXXXXX")"
trap 'rm -f "$patched_script"' EXIT

# CPyMO does not use network media. Disable legacy SecureTransport so modern
# Xcode does not require an iOS 11.2 API while retaining the iOS 8.0 target.
sed 's/--disable-doc --enable-pic/--disable-doc --enable-pic --disable-securetransport/' \
    "$upstream_script" > "$patched_script"

cd "$upstream_dir"
bash "$patched_script" "$@"

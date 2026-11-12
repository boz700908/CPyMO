#!/usr/bin/env bash
# 把（资源已内嵌的）cpymo 二进制打成 .app 再封装成 .dmg。
# 未签名、未公证：首次打开需要右键→打开，或 xattr -d com.apple.quarantine。
#
# 用法: package-macos.sh <二进制> <输出目录> [标签]
set -euo pipefail

BIN=${1:?用法: package-macos.sh <二进制> <输出目录> [标签]}
OUT=${2:?用法: package-macos.sh <二进制> <输出目录> [标签]}
LABEL=${3:-macOS}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${VERSION:-$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)}
VERSION=${VERSION:-1.2.0}
SLUG=$(printf '%s' "$LABEL" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/CPyMO.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$OUT"
install -m 755 "$BIN" "$APP/Contents/MacOS/CPyMO"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>CPyMO</string>
  <key>CFBundleDisplayName</key><string>CPyMO</string>
  <key>CFBundleIdentifier</key><string>xyz.xydm.cpymo</string>
  <key>CFBundleExecutable</key><string>CPyMO</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>10.13</string>
</dict>
</plist>
EOF

for icon in "$ROOT/cpymo.icns" "$ROOT/icon.icns"; do
  if [ -f "$icon" ]; then cp "$icon" "$APP/Contents/Resources/cpymo.icns"; break; fi
done

# ad-hoc 签名：不需要开发者证书，至少让系统认为 app 完整。
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 \
  || echo "ad-hoc 签名跳过（不影响产物）"

hdiutil create -volname "CPyMO" -srcfolder "$APP" -ov -format UDZO \
  "$OUT/CPyMO-$VERSION-$SLUG.dmg"

echo "=== 产物（$LABEL）==="
ls -l "$OUT"

#!/usr/bin/env bash
# 把（资源已内嵌的）cpymo 二进制打成各 Linux 发行版的原生包，外加 AppImage。
#
# 用法: package-linux.sh <二进制> <输出目录> [标签]
# 产物: cpymo_<版本>_<架构>.deb / .rpm / .pkg.tar.zst(pacman) / .apk，以及 CPyMO-<版本>-x86_64.AppImage
set -euo pipefail

BIN=${1:?用法: package-linux.sh <二进制> <输出目录> [标签]}
OUT=${2:?用法: package-linux.sh <二进制> <输出目录> [标签]}
LABEL=${3:-Linux}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${VERSION:-$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)}
VERSION=${VERSION:-1.2.0}

DESC="CPyMO - visual novel engine with accessibility support"
MAINTAINER="boz700908"
URL="https://github.com/Strrationalism/CPyMO"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications" "$OUT"
install -m 755 "$BIN" "$STAGE/usr/bin/cpymo"

cat > "$STAGE/usr/share/applications/cpymo.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=CPyMO
Comment=$DESC
Exec=cpymo
Terminal=false
Categories=Game;
EOF

if ! command -v fpm >/dev/null 2>&1; then
  gem install --no-document fpm || sudo gem install --no-document fpm
  # gem 有时把可执行文件装到用户目录，那里默认不在 PATH 上。
  if command -v ruby >/dev/null 2>&1; then
    PATH="$(ruby -e 'print Gem.user_dir' 2>/dev/null)/bin:$PATH"
    export PATH
  fi
fi
command -v fpm >/dev/null 2>&1 || {
  echo "!! fpm 不可用，跳过 deb/rpm/pacman/apk"
  FPM_MISSING=1
}

# fpm 一次覆盖 deb / rpm / pacman / apk，包内就是单个 cpymo。
for fmt in deb rpm pacman apk; do
  [ -n "${FPM_MISSING:-}" ] && break
  echo "--- $fmt ---"
  fpm -s dir -t "$fmt" -n cpymo -v "$VERSION" \
      --maintainer "$MAINTAINER" --description "$DESC" --url "$URL" \
      --prefix / -C "$STAGE" . -p "$OUT/" \
    || echo "!! $fmt 打包失败，继续其它格式"
done

# AppImage 本身就是一个可执行文件，最贴合「单程序分发」。
if [ "$(uname -m)" = "x86_64" ]; then
  TOOL=/tmp/appimagetool-x86_64.AppImage
  if [ ! -x "$TOOL" ]; then
    curl -fsSL -o "$TOOL" \
      https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$TOOL"
  fi

  APPDIR="$STAGE/AppDir"
  mkdir -p "$APPDIR/usr/bin"
  install -m 755 "$BIN" "$APPDIR/usr/bin/cpymo"
  cp "$STAGE/usr/share/applications/cpymo.desktop" "$APPDIR/cpymo.desktop"
  cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/bin/cpymo" "$@"
EOF
  chmod +x "$APPDIR/AppRun"
  for icon in "$ROOT/cpymo.png" "$ROOT/icon.png" "$ROOT/cpymo.jpg"; do
    if [ -f "$icon" ]; then cp "$icon" "$APPDIR/cpymo.png"; break; fi
  done

  echo "--- AppImage ---"
  ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" \
      "$OUT/CPyMO-$VERSION-x86_64.AppImage" \
    || ARCH=x86_64 "$TOOL" "$APPDIR" "$OUT/CPyMO-$VERSION-x86_64.AppImage" \
    || echo "!! AppImage 打包失败，继续"
fi

echo "=== 产物（$LABEL）==="
ls -l "$OUT"

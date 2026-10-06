#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run in an isolated Ubuntu 20.04 amd64 environment with the documented dependencies.
set -euo pipefail
cd "$(dirname "$0")/.."
source_root=$PWD
source /etc/os-release
if [ "$ID" != ubuntu ] || [ "$VERSION_ID" != 20.04 ] || [ "$(dpkg --print-architecture)" != amd64 ]; then
    echo 'This script requires Ubuntu 20.04 amd64; use an isolated container.' >&2
    exit 2
fi
build_dir=$(realpath -m "${NOVA_BUILD_DIR:-build-focal}")
package_dir=$(realpath -m "${NOVA_PACKAGE_DIR:-dist/ubuntu20.04}")
if [ "$build_dir" = / ] || [ "$build_dir" = "$source_root" ]; then
    echo "Use a dedicated build directory." >&2
    exit 2
fi
prefix=/usr/lib/novapinyin/focal
upstream_version=$(sed -n 's/^Version: //p' packaging/control.in)
version="${upstream_version}~ubuntu20.04.5"
mkdir -p "$build_dir/sources" "$package_dir"
export CC=gcc-10 CXX=g++-10
fetch() {
    local filename=$1 url=$2
    if [ ! -f "$build_dir/sources/$filename" ]; then
        curl --fail --location --retry 3 "$url" -o "$build_dir/sources/$filename.partial"
        mv "$build_dir/sources/$filename.partial" "$build_dir/sources/$filename"
    fi
}
fetch fcitx5-5.1.7.tar.xz https://download.fcitx-im.org/fcitx5/fcitx5/fcitx5-5.1.7.tar.xz
fetch fcitx5-qt-5.0.17.tar.xz https://download.fcitx-im.org/fcitx5/fcitx5-qt/fcitx5-qt-5.0.17.tar.xz
fetch fcitx5-configtool-5.0.17.tar.xz https://download.fcitx-im.org/fcitx5/fcitx5-configtool/fcitx5-configtool-5.0.17.tar.xz
fetch libime-1.1.5.tar.xz https://download.fcitx-im.org/fcitx5/libime/libime-1.1.5.tar.xz
fetch xcb-imdkit-1.0.8.tar.gz https://codeload.github.com/fcitx/xcb-imdkit/tar.gz/refs/tags/1.0.8
for filename in lm_sc.arpa-20230712.tar.xz dict-20230412.tar.xz en_dict-20121020.tar.gz; do
    fetch "$filename" "https://download.fcitx-im.org/data/$filename"
done
(cd "$build_dir/sources" && sha256sum --check "$source_root/packaging/focal/sources.sha256")
for filename in fcitx5-5.1.7.tar.xz libime-1.1.5.tar.xz xcb-imdkit-1.0.8.tar.gz fcitx5-qt-5.0.17.tar.xz fcitx5-configtool-5.0.17.tar.xz; do
    tar -xf "$build_dir/sources/$filename" -C "$build_dir/sources"
done
# Focal msgfmt 0.19.8 merges empty Image= with the next Color= line.
# Omitting the empty key keeps Fcitx's empty image default and preserves Color.
sed -i '/^Image=$/d' \
    "$build_dir/sources/fcitx5-5.1.7/src/ui/classic/themes/default/theme.conf.in" \
    "$build_dir/sources/fcitx5-5.1.7/src/ui/classic/themes/default-dark/theme-dark.conf.in"
common=(-G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$prefix"
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_RPATH="$prefix/lib" -DCMAKE_PREFIX_PATH="$prefix")
cmake -S "$build_dir/sources/xcb-imdkit-1.0.8" -B "$build_dir/xcb" "${common[@]}" -DENABLE_TEST=OFF
cmake --build "$build_dir/xcb" --parallel 2
cmake --install "$build_dir/xcb"
cmake -S "$build_dir/sources/fcitx5-5.1.7" -B "$build_dir/fcitx" "${common[@]}" \
    -DENABLE_TEST=OFF -DENABLE_WAYLAND=OFF -DENABLE_ENCHANT=OFF -DENABLE_EMOJI=OFF -DENABLE_XDGAUTOSTART=OFF
mkdir -p "$build_dir/fcitx/src/modules/spell"
cp "$build_dir/sources/en_dict-20121020.tar.gz" "$build_dir/fcitx/src/modules/spell/"
cmake --build "$build_dir/fcitx" --parallel 2
cmake --install "$build_dir/fcitx"
# Reject broken translated themes before packaging a white-on-white highlight.
python3 - "$prefix" <<'PY'
import configparser
from pathlib import Path
import sys
themes = Path(sys.argv[1]) / 'share/fcitx5/themes'
for name in ('default', 'default-dark'):
    config = configparser.ConfigParser(strict=False, interpolation=None)
    config.read(str(themes / name / 'theme.conf'))
    for section in ('InputPanel/Background', 'InputPanel/Highlight', 'Menu/Background', 'Menu/Highlight'):
        assert config[section].get('Image', '') == '', (name, section)
        assert config[section]['Color'].startswith('#'), (name, section)
PY
cmake -S "$build_dir/sources/fcitx5-qt-5.0.17" -B "$build_dir/qt" "${common[@]}" -DENABLE_QT4=OFF -DENABLE_QT5=ON -DENABLE_QT6=OFF -DCMAKE_INSTALL_QT5PLUGINDIR="$prefix/lib/qt5/plugins"
cmake --build "$build_dir/qt" --parallel 2
cmake --install "$build_dir/qt"
cmake -S "$build_dir/sources/fcitx5-configtool-5.0.17" -B "$build_dir/configtool" "${common[@]}" -DENABLE_KCM=OFF -DENABLE_TEST=OFF
cmake --build "$build_dir/configtool" --parallel 2
cmake --install "$build_dir/configtool"
cmake -S "$build_dir/sources/libime-1.1.5" -B "$build_dir/libime" "${common[@]}" -DENABLE_TEST=OFF -DENABLE_DATA=OFF
cmake --build "$build_dir/libime" --parallel 2
cmake --install "$build_dir/libime"
mkdir -p "$prefix/share/libime" "$prefix/lib/libime" "$build_dir/data"
if [ ! -f "$prefix/lib/libime/zh_CN.lm" ]; then
    tar -xf "$build_dir/sources/lm_sc.arpa-20230712.tar.xz" -C "$build_dir/data"
    "$prefix/bin/libime_slm_build_binary" -s -a 22 -q 8 trie "$build_dir/data/lm_sc.arpa" "$prefix/lib/libime/zh_CN.lm.partial"
    mv "$prefix/lib/libime/zh_CN.lm.partial" "$prefix/lib/libime/zh_CN.lm"
fi
if [ ! -f "$prefix/share/libime/sc.dict" ]; then
    tar -xf "$build_dir/sources/dict-20230412.tar.xz" -C "$build_dir/data"
    "$prefix/bin/libime_pinyindict" "$build_dir/data/dict_sc.txt" "$prefix/share/libime/sc.dict.partial"
    mv "$prefix/share/libime/sc.dict.partial" "$prefix/share/libime/sc.dict"
fi
cmake -S . -B "$build_dir/nova" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_INSTALL_RPATH="$prefix/lib" \
    -DNOVA_MODEL_FILE="$prefix/lib/libime/zh_CN.lm" -DNOVA_DICTIONARY_DIR="$prefix/share/libime"
cmake --build "$build_dir/nova" --parallel 2
ctest --test-dir "$build_dir/nova" --output-on-failure
stage="$build_dir/package"
# This directory is dedicated to this build and is recreated to exclude stale files.
if [ "$stage" = / ] || [ "$stage" = "$PWD" ]; then exit 2; fi
rm -rf "$stage"
mkdir -p "$stage$prefix" "$stage/DEBIAN"
cp -a "$prefix/." "$stage$prefix/"
rm -rf "$stage$prefix/include" "$stage$prefix/lib/cmake" "$stage$prefix/lib/pkgconfig" "$stage$prefix/share/applications"
DESTDIR="$stage" cmake --install "$build_dir/nova" --strip
cp -a "$stage/usr/share/fcitx5/themes/." "$stage$prefix/share/fcitx5/themes/"
rm -rf "$stage/usr/share/fcitx5/themes"
# Apply the new defaults only to this package's private themes.
cp -a "$stage$prefix/share/fcitx5/themes/novapinyin/." "$stage$prefix/share/fcitx5/themes/default/"
cp -a "$stage$prefix/share/fcitx5/themes/novapinyin-dark/." "$stage$prefix/share/fcitx5/themes/default-dark/"
mkdir -p "$stage$prefix/lib/fcitx5" "$stage$prefix/share/fcitx5/addon" "$stage$prefix/share/fcitx5/inputmethod"
mv "$stage/usr/lib/x86_64-linux-gnu/fcitx5/libnovapinyin.so" "$stage$prefix/lib/fcitx5/"
mv "$stage/usr/share/fcitx5/addon/novapinyin.conf" "$stage$prefix/share/fcitx5/addon/"
mv "$stage/usr/share/fcitx5/inputmethod/novapinyin.conf" "$stage$prefix/share/fcitx5/inputmethod/"
mv "$stage/usr/bin/novapinyin-manager" "$stage$prefix/bin/novapinyin-manager"
install -m644 packaging/focal/runtime.py "$stage$prefix/bin/novapinyin-runtime.py"
install -m755 packaging/focal/novapinyin-manager packaging/focal/novapinyin-fcitx5 packaging/focal/novapinyin-fcitx5-configtool "$stage/usr/bin/"
install -m644 packaging/focal/novapinyin-fcitx5-configtool.desktop "$stage/usr/share/applications/"
mkdir -p "$stage/usr/share/im-config/data"
install -m644 packaging/focal/77_novapinyin.conf packaging/focal/77_novapinyin.rc "$stage/usr/share/im-config/data/"
install -m755 packaging/focal/session-setup.py "$stage/usr/bin/novapinyin-session-setup"
mkdir -p "$stage/etc/X11/Xsession.d"
install -m644 packaging/focal/69novapinyin-setup "$stage/etc/X11/Xsession.d/"
printf '/etc/X11/Xsession.d/69novapinyin-setup\n' > "$stage/DEBIAN/conffiles"
install -m755 packaging/focal/postinst packaging/focal/prerm "$stage/DEBIAN/"
install -m644 LICENSE "$stage/usr/share/doc/novapinyin/copyright"
python3 packaging/focal/package.py "$stage" "$prefix" "$version" "$build_dir"
# Matching source archives accompany the binary, including all vendored dependencies.
source_dir="$build_dir/source-bundle/novapinyin-$version"
rm -rf "$source_dir"
mkdir -p "$source_dir/upstream"
if [ -d .git ] || [ -f .git ]; then
    git_dir="$source_root/.git"
    if [ -f .git ]; then
        git_dir=$(sed -n "s/^gitdir: //p" .git)
        git_dir=$(realpath "$git_dir")
    fi
    git --git-dir="$git_dir" --work-tree="$source_root" -c core.fsmonitor=false -c core.hooksPath=/dev/null ls-files --cached --others --exclude-standard > "$build_dir/source-files"
else
    cp SOURCE_FILES "$build_dir/source-files"
fi
tar -T "$build_dir/source-files" -cf - | tar -xf - -C "$source_dir"
cp "$build_dir/source-files" "$source_dir/SOURCE_FILES"
while read -r checksum filename; do
    cp "$build_dir/sources/$filename" "$source_dir/upstream/"
done < packaging/focal/sources.sha256
cp packaging/focal/sources.sha256 "$source_dir/upstream/"
tar -czf "$package_dir/novapinyin_${version}_sources.tar.gz" -C "$build_dir/source-bundle" "novapinyin-$version"
dpkg-deb --root-owner-group --build "$stage" "$package_dir/novapinyin_${version}_amd64.deb"
(cd "$package_dir" && sha256sum "novapinyin_${version}_amd64.deb" > SHA256SUMS
 sha256sum "novapinyin_${version}_sources.tar.gz" > SOURCE_SHA256SUMS)

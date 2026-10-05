#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=${NOVA_BUILD_DIR:-build}
package_dir=${NOVA_PACKAGE_DIR:-dist}
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DNOVA_BACKEND_PROBE=OFF -DNOVA_GUI_SMOKE=OFF
cmake --build "$build_dir" --parallel 2
ctest --test-dir "$build_dir" --output-on-failure
build_dir=$(realpath "$build_dir")
stage="$build_dir/package"
mkdir -p "$stage"
DESTDIR="$stage" cmake --install "$build_dir" --strip
mkdir -p "$stage/DEBIAN" "$package_dir"
# Resolve dependencies against the target Ubuntu libraries, never guess ABI package names.
mkdir -p "$build_dir/shlibs/debian"
printf 'Source: novapinyin\n\nPackage: novapinyin\nArchitecture: any\nDescription: NovaPinyin\n' > "$build_dir/shlibs/debian/control"
depends=$(cd "$build_dir/shlibs" && dpkg-shlibdeps -O -e "$build_dir/novapinyin-tool" -e "$build_dir/libnovapinyin.so" | sed 's/^shlibs:Depends=//')
python3 - "$stage" "$depends" <<'PY'
from pathlib import Path
import sys
stage=Path(sys.argv[1])
control=Path('packaging/control.in').read_text().replace('@DEPENDS@',sys.argv[2])
(stage/'DEBIAN/control').write_text(control)
PY
cp LICENSE "$stage/usr/share/doc/novapinyin/copyright"
package_version=$(sed -n 's/^Version: //p' packaging/control.in)
package_name="novapinyin_${package_version}_amd64.deb"
dpkg-deb --root-owner-group --build "$stage" "$package_dir/$package_name"
(cd "$package_dir" && sha256sum "$package_name" > SHA256SUMS)

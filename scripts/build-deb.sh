#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source /etc/os-release
if [ "$ID" != ubuntu ] || [ "$VERSION_ID" != 24.04 ] || [ "$(dpkg --print-architecture)" != amd64 ]; then
    echo 'This script requires Ubuntu 24.04 amd64; use an isolated container.' >&2
    exit 2
fi
package_version="$(sed -n 's/^Version: //p' packaging/control.in)~ubuntu24.04.1"
build_dir=$(realpath -m "${NOVA_BUILD_DIR:-build}")
if [ "$build_dir" = / ] || [ "$build_dir" = "$PWD" ]; then
    echo 'Use a dedicated build directory.' >&2
    exit 2
fi
package_dir=${NOVA_PACKAGE_DIR:-dist}
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DNOVA_BACKEND_PROBE=OFF -DNOVA_GUI_SMOKE=OFF
cmake --build "$build_dir" --parallel 2
ctest --test-dir "$build_dir" --output-on-failure
build_dir=$(realpath "$build_dir")
stage="$build_dir/package"
rm -rf "$stage"
mkdir -p "$stage"
DESTDIR="$stage" cmake --install "$build_dir" --strip
mkdir -p "$stage/DEBIAN" "$package_dir"
# Resolve dependencies against the target Ubuntu libraries, never guess ABI package names.
mkdir -p "$build_dir/shlibs/debian"
printf 'Source: novapinyin\n\nPackage: novapinyin\nArchitecture: any\nDescription: NovaPinyin\n' > "$build_dir/shlibs/debian/control"
depends=$(cd "$build_dir/shlibs" && dpkg-shlibdeps -O -e "$build_dir/novapinyin-tool" -e "$build_dir/libnovapinyin.so" | sed 's/^shlibs:Depends=//')
python3 - "$stage" "$depends" "$package_version" <<'PY'
from pathlib import Path
import sys
stage=Path(sys.argv[1])
control=Path('packaging/control.in').read_text().replace('@DEPENDS@',sys.argv[2])
import re
control=re.sub(r'^Version: .*$', 'Version: '+sys.argv[3], control, flags=re.M)
(stage/'DEBIAN/control').write_text(control)
PY
cp LICENSE "$stage/usr/share/doc/novapinyin/copyright"
package_name="novapinyin_${package_version}_amd64.deb"
dpkg-deb --root-owner-group --build "$stage" "$package_dir/$package_name"
(cd "$package_dir" && sha256sum "$package_name" > SHA256SUMS)

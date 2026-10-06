#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve native dependencies while excluding this package's private libraries."""
from pathlib import Path
import re
import subprocess
import sys

stage, prefix, version, build = sys.argv[1:]
stage, build = Path(stage), Path(build)
private = stage / prefix.lstrip('/')
work = build / 'shlibs'
(work / 'debian').mkdir(parents=True, exist_ok=True)
(work / 'debian/control').write_text('Source: novapinyin\n\nPackage: novapinyin\nArchitecture: amd64\nDescription: NovaPinyin\n')
elfs, shlibs = [], set()
for path in stage.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            continue
    subprocess.run(['strip', '--strip-unneeded', str(path)], check=True)
    dynamic = subprocess.run(['readelf', '-d', str(path)], check=True, text=True, capture_output=True).stdout
    if 'NEEDED' not in dynamic:
        continue
    elfs.append(path)
    match = re.search(r'\(SONAME\).*\[(.+)\.so\.([^.]+)\]', dynamic)
    if match:
        shlibs.add(f'{match[1]} {match[2]} novapinyin (= {version})')
local = work / 'local.shlibs'
local.write_text('\n'.join(sorted(shlibs)) + '\n')
command = ['dpkg-shlibdeps', '-O', '-xnovapinyin', '-L' + str(local), '-l' + str(private / 'lib')]
command.extend('-e' + str(path) for path in elfs)
result = subprocess.run(command, cwd=work, check=True, text=True, capture_output=True)
sys.stderr.write(result.stderr)
depends = result.stdout.strip().split('=', 1)[1]
assert depends and 'novapinyin' not in depends, depends
control = Path('packaging/focal/control.in').read_text().replace('@VERSION@', version).replace('@DEPENDS@', depends)
(stage / 'DEBIAN/control').write_text(control)
# Retain upstream licensing and KenLM's bundled third-party notices.
licenses = stage / 'usr/share/doc/novapinyin/upstream-licenses'
licenses.mkdir(parents=True, exist_ok=True)
for notice in Path('packaging/focal').glob('*.copyright'):
    (licenses / notice.name).write_bytes(notice.read_bytes())
for project in ('fcitx5-5.1.7', 'libime-1.1.5', 'xcb-imdkit-1.0.8', 'fcitx5-qt-5.0.17', 'fcitx5-configtool-5.0.17'):
    source = build / 'sources' / project
    for path in source.rglob('*'):
        if path.is_file() and ('LICENSE' in path.name.upper() or 'COPYING' in path.name.upper()):
            dest = licenses / project / path.relative_to(source)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(path.read_bytes())
service = private / 'share/dbus-1/services/org.fcitx.Fcitx5.service'
if service.exists():
    service.write_text(re.sub(r'^Exec=.*$', 'Exec=/usr/bin/novapinyin-fcitx5', service.read_text(), flags=re.M))

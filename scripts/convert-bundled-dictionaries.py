#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproduce the bundled TSV dictionaries from checksum-pinned Rime sources."""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / 'data/dictionaries'
sources = json.loads((root / 'sources.json').read_text())
for source in sources:
    assert hashlib.sha256((root / source['file']).read_bytes()).hexdigest() == source['sha256'], source['file']
catalog = []
for name, title in [('rime-common', 'Rime 常用词精选'), ('rime-ice', '雾凇社区补充词')]:
    source = next(item for item in sources if item['name'] == name and item['source'].endswith('.yaml'))
    entries = {}
    started, skip_corrections = False, False
    for line in (root / source['file']).read_text().splitlines():
        if line == '...':
            started = True
            continue
        if line.startswith('##### '):
            skip_corrections = '错音错字' in line
        if not started or skip_corrections or not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) < 2:
            continue
        text, reading = fields[:2]
        if not re.fullmatch(r'[\u4e00-\u9fff]{2,16}', text):
            continue
        reading = ' '.join({'lue': 'lve', 'nue': 'nve'}.get(syllable, syllable)
                           for syllable in reading.replace('ü', 'v').split())
        if not re.fullmatch(r'[a-z]+(?: [a-z]+)*', reading) or len(text) != len(reading.split()):
            continue
        weight = int(fields[2]) if len(fields) >= 3 and fields[2].isdigit() else 100
        entries[(text, reading)] = min(100000, weight)
    selected = sorted(entries, key=lambda item: (-entries[item], item))[:20000]
    filename = name + '.tsv'
    header = '# novapinyin-dict-v1\n# Converted and filtered by NovaPinyin; see SOURCES.md and upstream licenses.\n'
    header += '# License: ' + ('Apache-2.0' if name == 'rime-common' else 'GPL-3.0') + '\n'
    header += '# Source: https://github.com/' + source['repository'] + '/blob/' + source['commit'] + '/' + source['source'] + '\n'
    (root / filename).write_text(header + ''.join('%s\t%s\t%d\n' % (text, reading, entries[(text, reading)])
        for text, reading in sorted(selected)))
    catalog.append({'name': name, 'title': title, 'file': filename, 'count': len(selected),
                    'license': 'Apache-2.0' if name == 'rime-common' else 'GPL-3.0',
                    'source': 'https://github.com/' + source['repository']})
(root / 'manifest.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')
print('\n'.join('%s: %d entries' % (item['title'], item['count']) for item in catalog))

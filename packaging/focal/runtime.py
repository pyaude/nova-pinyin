# SPDX-License-Identifier: GPL-3.0-or-later
"""Private framework configuration; personal NovaPinyin data keeps its XDG path."""
import os
from pathlib import Path

PREFIX = Path('/usr/lib/novapinyin/focal')

def xdg_directory(name, fallback):
    value = os.environ.get(name, '')
    return Path(value) if value and Path(value).is_absolute() else Path.home() / fallback

def configure(initial_profile=False):
    base = xdg_directory('XDG_CONFIG_HOME', '.config')
    private = base / 'novapinyin/fcitx5'
    os.environ['PATH'] = str(PREFIX / 'bin') + ':' + (os.environ.get('PATH') or '/usr/bin:/bin')
    os.environ['QT_PLUGIN_PATH'] = str(PREFIX / 'lib/qt5/plugins') + ':' + os.environ.get('QT_PLUGIN_PATH', '')
    os.environ.setdefault('QT_IM_MODULE', 'fcitx5')
    os.environ['FCITX_CONFIG_HOME'] = str(private)
    os.environ['FCITX_DATA_HOME'] = str(xdg_directory('XDG_DATA_HOME', '.local/share') / 'novapinyin/fcitx5')
    os.environ['FCITX_ADDON_DIRS'] = str(PREFIX / 'lib/fcitx5')
    os.environ['FCITX_DATA_DIRS'] = str(PREFIX / 'share/fcitx5')
    os.environ['XDG_DATA_DIRS'] = str(PREFIX / 'share') + ':' + (os.environ.get('XDG_DATA_DIRS') or '/usr/local/share:/usr/share')
    (private / 'conf').mkdir(parents=True, exist_ok=True, mode=0o700)
    old = base / 'fcitx5/conf/novapinyin.conf'
    if old.is_file() and not (private / 'conf/novapinyin.conf').exists():
        content = old.read_bytes()
        try:
            descriptor = os.open(str(private / 'conf/novapinyin.conf'), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            pass
        else:
            with os.fdopen(descriptor, 'wb') as stream:
                stream.write(content)
    if initial_profile:
        try:
            with (private / 'profile').open('x') as stream:
                stream.write('[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=novapinyin\n'
                             '[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n'
                             '[Groups/0/Items/1]\nName=novapinyin\nLayout=\n'
                             '[GroupOrder]\n0=Default\n')
        except FileExistsError:
            pass

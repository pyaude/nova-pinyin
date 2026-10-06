#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the installed Focal im-config entry through both session phases."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

assert os.environ.get('NOVA_ISOLATED_GUI_TEST') == '1'
if os.environ.get('_NOVA_FOCAL_PRIVATE_DBUS') != '1':
    subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve())],
                   env=dict(os.environ, _NOVA_FOCAL_PRIVATE_DBUS='1'), check=True)
    sys.exit(0)

prefix = Path('/usr/lib/novapinyin/focal')
with tempfile.TemporaryDirectory(prefix='nova-focal-login-') as temporary:
    root = Path(temporary)
    environment = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
                       XDG_DATA_HOME=str(root / 'data'), XDG_RUNTIME_DIR=str(root / 'run'))
    for key in ('FCITX_CONFIG_HOME', 'FCITX_DATA_HOME', 'FCITX_ADDON_DIRS',
                'FCITX_DATA_DIRS', 'GTK_IM_MODULE', 'QT_IM_MODULE', 'XMODIFIERS'):
        environment.pop(key, None)
    (root / 'run').mkdir(mode=0o700)
    available = subprocess.run(['im-config', '-l'], env=environment, text=True,
                               capture_output=True, check=True, timeout=10).stdout.split()
    assert 'novapinyin' in available, 'NovaPinyin is missing from the im-config menu'
    script = '''
. /usr/share/im-config/xinputrc.common
IM_CONFIG_PHASE=1
run_im novapinyin
test "$GTK_IM_MODULE" = fcitx5
test "$QT_IM_MODULE" = fcitx5
test "$XMODIFIERS" = @im=fcitx
test "$FCITX_CONFIG_HOME" = "$XDG_CONFIG_HOME/novapinyin/fcitx5"
test "$FCITX_ADDON_DIRS" = /usr/lib/novapinyin/focal/lib/fcitx5
IM_CONFIG_PHASE=2
run_im novapinyin
'''
    try:
        subprocess.run(['sh', '-ec', script], env=environment, check=True, timeout=20)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            result = subprocess.run(['dbus-send', '--session', '--print-reply',
                                     '--dest=org.fcitx.Fcitx5', '/controller',
                                     'org.fcitx.Fcitx.Controller1.AvailableInputMethods'],
                                    env=environment, capture_output=True, timeout=5)
            if result.returncode == 0 and b'string "novapinyin"' in result.stdout:
                break
            time.sleep(0.1)
        else:
            raise AssertionError('im-config did not start a usable NovaPinyin daemon')
        assert 'Name=novapinyin' in (root / 'config/novapinyin/fcitx5/profile').read_text()
    finally:
        subprocess.run([str(prefix / 'bin/fcitx5-remote'), '-e'], env=environment,
                       capture_output=True, timeout=10)
print('focal im-config: menu, login environment and daemon startup passed')

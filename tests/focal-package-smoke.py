#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the installed Focal package in private D-Bus and Xvfb sessions."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

assert os.environ.get('NOVA_ISOLATED_GUI_TEST') == '1'
if os.environ.get('_NOVA_FOCAL_PRIVATE_DBUS') != '1':
    private_environment = dict(os.environ, _NOVA_FOCAL_PRIVATE_DBUS='1')
    subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve())],
                   env=private_environment, check=True)
    sys.exit(0)
prefix = Path('/usr/lib/novapinyin/focal')
assert prefix.is_dir()
with tempfile.TemporaryDirectory(prefix='nova-focal-installed-') as temporary:
    root = Path(temporary)
    environment = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
                       XDG_DATA_HOME=str(root / 'data'), XDG_RUNTIME_DIR=str(root / 'run'))
    for key in ('FCITX_CONFIG_HOME', 'FCITX_DATA_HOME', 'FCITX_DATA_DIRS', 'FCITX_ADDON_DIRS'):
        environment.pop(key, None)
    (root / 'run').mkdir(mode=0o700)
    legacy = root / 'config/fcitx5'
    (legacy / 'conf').mkdir(parents=True)
    original_profile = b'[Groups/0]\nName=Existing\nDefaultIM=existing-im\n[Future]\nKeep=True\n'
    original_config = b'Learning=True\nPrivacy=False\nFutureOption=preserved\n'
    (legacy / 'profile').write_bytes(original_profile)
    (legacy / 'conf/novapinyin.conf').write_bytes(original_config)
    private = root / 'config/novapinyin/fcitx5'
    def run(*arguments):
        return subprocess.run(arguments, env=environment, text=True, capture_output=True,
                              check=True, timeout=20).stdout
    assert '5.1.7' in run('novapinyin-fcitx5', '--version')
    assert not private.exists(), '--version unexpectedly created user configuration'
    def start_and_check():
        with (root / 'fcitx.log').open('w') as log:
            daemon = subprocess.Popen(['novapinyin-fcitx5', '--disable=wayland,ibus'],
                                      env=environment, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    assert daemon.poll() is None, (root / 'fcitx.log').read_text()
                    result = subprocess.run(['dbus-send', '--session', '--print-reply',
                                             '--dest=org.fcitx.Fcitx5', '/controller',
                                             'org.fcitx.Fcitx.Controller1.AvailableInputMethods'],
                                            env=environment, capture_output=True, timeout=5)
                    if result.returncode == 0 and b'string \"novapinyin\"' in result.stdout:
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError('Installed NovaPinyin was not available')
                assert 'Name=novapinyin' in (private / 'profile').read_text()
                assert (legacy / 'profile').read_bytes() == original_profile
                assert (legacy / 'conf/novapinyin.conf').read_bytes() == original_config
                tool = subprocess.Popen(['novapinyin-fcitx5-configtool'], env=environment,
                                        stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        assert tool.poll() is None, (root / 'fcitx.log').read_text()
                        windows = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(tool.pid)],
                                                 env=environment, capture_output=True, timeout=5)
                        if windows.returncode == 0 and windows.stdout.strip():
                            break
                        time.sleep(0.1)
                    else:
                        raise AssertionError('Configuration tool did not open a window')
                finally:
                    tool.terminate()
                    tool.wait(timeout=10)
            finally:
                daemon.terminate()
                daemon.wait(timeout=10)
    start_and_check()
    assert (private / 'conf/novapinyin.conf').read_bytes() == original_config
    updated = original_config + b'NewPrivateOption=True\n'
    (private / 'conf/novapinyin.conf').write_bytes(updated)
    start_and_check()
    assert (private / 'conf/novapinyin.conf').read_bytes() == updated, 'Restart overwrote private settings'
print('focal package: private profile, preserved settings, startup and Qt configuration passed')

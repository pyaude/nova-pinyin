#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed-package activation/removal tests using disposable users in a Focal container."""
import base64
import hashlib
import json
import os
from pathlib import Path
import pwd
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

SETUP = '/usr/bin/novapinyin-session-setup'
REMOTE = '/usr/lib/novapinyin/focal/bin/fcitx5-remote'
ROOT = Path(__file__).resolve().parents[1]
VERSION = next(line.split(': ', 1)[1] for line in (ROOT / 'packaging/control.in').read_text().splitlines()
               if line.startswith('Version: ')) + '~ubuntu20.04.1'
PACKAGE = str(Path(os.environ.get('NOVA_PACKAGE_DIR', str(ROOT / 'dist/ubuntu20.04')))
              / ('novapinyin_' + VERSION + '_amd64.deb'))
BUILD = Path(os.environ.get('NOVA_BUILD_DIR', str(ROOT / 'build-focal/nova'))).resolve()


def check_real_clients():
    assert os.getuid() != 0
    assert os.environ['GTK_IM_MODULE'] == 'fcitx5'
    assert os.environ['QT_IM_MODULE'] == 'fcitx5'
    assert os.environ['XMODIFIERS'] == '@im=fcitx'
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            result = subprocess.run(['dbus-send', '--session', '--print-reply',
                                     '--dest=org.fcitx.Fcitx5', '/controller',
                                     'org.fcitx.Fcitx.Controller1.AvailableInputMethods'],
                                    capture_output=True, timeout=5)
            if result.returncode == 0 and b'string "novapinyin"' in result.stdout:
                break
            time.sleep(0.1)
        else:
            raise AssertionError('Automatic login did not start NovaPinyin')
        with tempfile.TemporaryDirectory(prefix='nova-auto-clients-') as temporary:
            for toolkit, title in [('gtk', 'GTK'), ('qt', 'Qt')]:
                output = Path(temporary) / (toolkit + '.txt')
                client = subprocess.Popen([str(BUILD / ('nova-' + toolkit + '-smoke')), str(output)])
                try:
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        windows = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name',
                                                  'NovaPinyin ' + title + ' Smoke'],
                                                 capture_output=True, text=True, timeout=5)
                        if windows.returncode == 0 and windows.stdout.strip():
                            window = windows.stdout.splitlines()[-1]
                            break
                        time.sleep(0.1)
                    else:
                        raise AssertionError('Client did not open')
                    subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True, timeout=5)
                    subprocess.run(['xdotool', 'mousemove', '--window', window, '40', '30', 'click', '1'], check=True, timeout=5)
                    time.sleep(0.3)
                    subprocess.run([REMOTE, '-c'], check=True, timeout=5)
                    subprocess.run(['xdotool', 'key', 'ctrl+space'], check=True, timeout=5)
                    time.sleep(0.2)
                    assert subprocess.check_output([REMOTE, '-n'], text=True).strip() == 'novapinyin'
                    subprocess.run(['xdotool', 'type', '--clearmodifiers', '--delay', '100', 'nihao'], check=True, timeout=5)
                    subprocess.run(['xdotool', 'key', 'space'], check=True, timeout=5)
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        if output.exists() and output.read_text() == '你好':
                            break
                        time.sleep(0.1)
                    else:
                        raise AssertionError('Automatic ' + toolkit + ' input failed')
                    print(toolkit + ': automatic login, Ctrl+Space and Chinese input passed')
                finally:
                    client.terminate()
                    client.wait(timeout=5)
    finally:
        subprocess.run([REMOTE, '-e'], capture_output=True, timeout=5)


class SessionSetup(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nova-auto-user-')
        self.base = Path(self.temporary.name)
        self.base.chmod(0o755)
        self.home = self.base / 'home'
        self.home.mkdir(mode=0o700)
        self.username = 'nova-' + uuid.uuid4().hex[:8]
        subprocess.run(['useradd', '--user-group', '--no-create-home', '--home-dir',
                        str(self.home), '--shell', '/bin/sh', self.username], check=True)
        self.account = pwd.getpwnam(self.username)
        os.chown(self.home, self.account.pw_uid, self.account.pw_gid)
        for name in ('config space', 'data', 'run'):
            directory = self.home / name
            directory.mkdir(mode=0o700)
            os.chown(directory, self.account.pw_uid, self.account.pw_gid)
        self.environment = dict(os.environ, XDG_CONFIG_HOME=str(self.home / 'config space'),
                                XDG_DATA_HOME=str(self.home / 'data'), XDG_RUNTIME_DIR=str(self.home / 'run'),
                                XDG_SESSION_TYPE='x11')
        for key in ('DBUS_SESSION_BUS_ADDRESS', 'DISPLAY', 'WAYLAND_DISPLAY', 'FCITX_CONFIG_HOME',
                    'FCITX_DATA_HOME', 'FCITX_ADDON_DIRS', 'FCITX_DATA_DIRS',
                    'GTK_IM_MODULE', 'QT_IM_MODULE', 'XMODIFIERS'):
            self.environment.pop(key, None)
        self.record = self.home / 'config space/novapinyin/session-setup.json'

    def tearDown(self):
        subprocess.run(['userdel', self.username], check=True)
        self.temporary.cleanup()

    def run_user(self, *arguments, check=True):
        result = subprocess.run(['/usr/sbin/runuser', '-u', self.username, '--', *arguments],
                                env=self.environment, capture_output=True, text=True, timeout=45)
        if check:
            self.assertEqual(result.returncode, 0, result.stdout + '\n' + result.stderr)
        return result

    def selected(self):
        return self.run_user('im-config', '-m').stdout.splitlines()[1]

    def test_automatic_login_and_real_clients(self):
        self.environment.update(QT_IM_MODULE='ibus', XMODIFIERS='@im=ibus',
                                QT4_IM_MODULE='ibus', CLUTTER_IM_MODULE='ibus')
        script = '''
set -e
STARTUP=true
. /etc/X11/Xsession.d/69novapinyin-setup
. /etc/X11/Xsession.d/70im-config_launch
exec /usr/bin/im-launch /usr/bin/python3 %s --client
''' % shlex.quote(str(Path(__file__).resolve()))
        result = self.run_user('dbus-run-session', '--', 'xvfb-run', '-a', 'sh', '-ec', script)
        self.assertIn('gtk: automatic login', result.stdout)
        self.assertIn('qt: automatic login', result.stdout)
        self.assertEqual(self.selected(), 'novapinyin')
        self.assertTrue(self.record.is_file())
        self.assertEqual((self.home / '.xinputrc.novapinyin-setup').resolve(), self.record)
        print(result.stdout.strip())

    def test_existing_selection_backup_and_restore(self):
        self.run_user('im-config', '-n', 'ibus')
        original = (self.home / '.xinputrc').read_bytes()
        self.run_user(SETUP, '--auto')
        self.assertEqual(self.selected(), 'novapinyin')
        state = json.loads(self.record.read_text())
        self.assertEqual(base64.b64decode(state['original']), original)
        self.run_user(SETUP, '--restore')
        self.assertEqual((self.home / '.xinputrc').read_bytes(), original)
        self.run_user(SETUP, '--auto')
        self.assertEqual(self.selected(), 'ibus')

    def test_later_user_choice_is_not_overwritten(self):
        self.run_user(SETUP, '--auto')
        self.run_user('im-config', '-n', 'none')
        chosen = (self.home / '.xinputrc').read_bytes()
        self.run_user(SETUP, '--auto')
        self.run_user(SETUP, '--restore')
        self.assertEqual((self.home / '.xinputrc').read_bytes(), chosen)

    def test_custom_configuration_preserved(self):
        original = b'# User-managed configuration\nrun_im ibus\nCUSTOM_FUTURE_OPTION=kept\n'
        path = self.home / '.xinputrc'
        path.write_bytes(original)
        os.chown(path, self.account.pw_uid, self.account.pw_gid)
        self.run_user(SETUP, '--auto')
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(self.selected(), 'custom')

    def test_symlink_configuration_not_followed(self):
        target = self.home / 'custom-config'
        target.write_bytes(b'preserve\n')
        os.chown(target, self.account.pw_uid, self.account.pw_gid)
        self.run_user('ln', '-s', str(target), str(self.home / '.xinputrc'))
        result = self.run_user(SETUP, '--auto', check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), b'preserve\n')

    def test_interrupted_activation_restored(self):
        self.run_user('im-config', '-n', 'ibus')
        original = (self.home / '.xinputrc').read_bytes()
        self.run_user(SETUP, '--auto')
        state = json.loads(self.record.read_text())
        state.update(status='pending', managed_sha256=None)
        self.record.write_text(json.dumps(state))
        self.run_user(SETUP, '--restore')
        self.assertEqual((self.home / '.xinputrc').read_bytes(), original)

    def test_already_selected_upgrade_restores_system_default(self):
        self.run_user('im-config', '-n', 'novapinyin')
        self.run_user(SETUP, '--auto')
        self.run_user(SETUP, '--restore')
        self.assertFalse((self.home / '.xinputrc').exists())

    def test_root_setup_does_not_touch_system_configuration(self):
        path = Path('/etc/X11/xinit/xinputrc')
        original = path.read_bytes()
        subprocess.run([SETUP, '--auto'], check=True)
        self.assertEqual(path.read_bytes(), original)

    def test_z_actual_upgrade_removal_and_reinstall(self):
        self.run_user('im-config', '-n', 'ibus')
        original = (self.home / '.xinputrc').read_bytes()
        self.run_user(SETUP, '--auto')
        record = self.record.read_bytes()
        self.run_user('novapinyin-tool', 'import', 'retained', '/usr/share/novapinyin/programming.tsv')
        database = self.home / 'data/novapinyin/user.db'
        checksum = hashlib.sha256(database.read_bytes()).digest()
        subprocess.run(['apt-get', 'install', '-y', '--no-install-recommends', '--reinstall', PACKAGE],
                       check=True, stdout=subprocess.DEVNULL)
        self.assertEqual(self.record.read_bytes(), record)
        try:
            subprocess.run(['apt-get', 'remove', '-y', 'novapinyin'], check=True, stdout=subprocess.DEVNULL)
            self.assertEqual((self.home / '.xinputrc').read_bytes(), original)
            self.assertFalse(self.record.exists())
            self.assertFalse((self.home / '.xinputrc.novapinyin-setup').exists())
            self.assertEqual(hashlib.sha256(database.read_bytes()).digest(), checksum)
        finally:
            subprocess.run(['apt-get', 'install', '-y', '--no-install-recommends', PACKAGE],
                           check=True, stdout=subprocess.DEVNULL)
        self.run_user(SETUP, '--auto')
        self.assertEqual(self.selected(), 'novapinyin')


if __name__ == '__main__':
    assert os.environ.get('NOVA_ISOLATED_GUI_TEST') == '1'
    if sys.argv[1:] == ['--client']:
        check_real_clients()
    else:
        assert os.getuid() == 0 and Path('/.dockerenv').exists(), 'Use an isolated Focal container'
        unittest.main(verbosity=2)

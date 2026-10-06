#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One-time user-session activation; package removal restores managed selections."""
import base64
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys
import tempfile

POINTER = '.xinputrc.novapinyin-setup'
LIMIT = 256 * 1024


def read_owned(path):
    try:
        descriptor = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return None
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
            raise ValueError('配置不是当前用户拥有的独立普通文件，保留原配置')
        data = stream.read(LIMIT + 1)
        if len(data) > LIMIT:
            raise ValueError('配置过大，保留原配置')
        return data, stat.S_IMODE(info.st_mode)


def atomic_write(path, data, mode=0o600):
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', dir=str(path.parent))
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def state_location(home):
    pointer = home / POINTER
    if pointer.is_symlink():
        if pointer.lstat().st_uid != os.getuid():
            raise ValueError('恢复记录不属于当前用户')
        target = Path(os.readlink(pointer))
        if not target.is_absolute():
            raise ValueError('恢复记录路径不合法')
        return pointer, target
    if pointer.exists():
        raise ValueError('恢复记录入口已存在，保留原文件')
    configured = os.environ.get('XDG_CONFIG_HOME', '')
    config = Path(configured) if configured and Path(configured).is_absolute() else home / '.config'
    return pointer, config / 'novapinyin/session-setup.json'


def save_state(path, state):
    atomic_write(path, (json.dumps(state, ensure_ascii=False, indent=2) + '\n').encode())


def digest(data):
    return hashlib.sha256(data).hexdigest()


def activate(home, state_path):
    record = read_owned(state_path)
    state = json.loads(record[0]) if record else None
    current = read_owned(home / '.xinputrc')
    if state and state.get('status') != 'pending':
        return
    if not state:
        result = subprocess.run(['/usr/bin/im-config', '-m'], capture_output=True,
                                text=True, check=True, timeout=10)
        selected = result.stdout.splitlines()[1]
        original = current[0] if current and selected != 'novapinyin' else None
        state = {'version': 1, 'status': 'pending', 'original':
                 base64.b64encode(original).decode() if original is not None else None,
                 'mode': current[1] if current else 0o600, 'managed_sha256': None}
        if selected == 'custom':
            state['status'] = 'custom'
            save_state(state_path, state)
            print('NovaPinyin：手工维护的 .xinputrc 已保留，请在设置中手动启用', file=sys.stderr)
            return
        save_state(state_path, state)
        if selected == 'novapinyin':
            state['managed_sha256'] = digest(current[0]) if current else None
            state['status'] = 'active'
            save_state(state_path, state)
            return
    original = base64.b64decode(state['original']) if state['original'] is not None else None
    # Recover an interrupted first activation without overwriting a subsequent user edit.
    if current and current[0] != original:
        result = subprocess.run(['/usr/bin/im-config', '-m'], capture_output=True,
                                text=True, check=True, timeout=10)
        if result.stdout.splitlines()[1] != 'novapinyin':
            state['status'] = 'changed'
            save_state(state_path, state)
            return
    else:
        subprocess.run(['/usr/bin/im-config', '-n', 'novapinyin'],
                       capture_output=True, check=True, timeout=10)
    current = read_owned(home / '.xinputrc')
    if not current:
        raise ValueError('输入框架选择未保存')
    os.chmod(home / '.xinputrc', 0o600)
    state['managed_sha256'] = digest(current[0])
    state['status'] = 'active'
    save_state(state_path, state)


def restore(home, state_path, removing):
    record = read_owned(state_path)
    if not record:
        if removing:
            (home / POINTER).unlink(missing_ok=True)
        return
    state = json.loads(record[0])
    current = read_owned(home / '.xinputrc')
    if state.get('status') == 'pending' and current:
        result = subprocess.run(['/usr/bin/im-config', '-m'], capture_output=True,
                                text=True, check=True, timeout=10)
        if result.stdout.splitlines()[1] == 'novapinyin':
            state['managed_sha256'] = digest(current[0])
    if state.get('managed_sha256') and current and digest(current[0]) == state['managed_sha256']:
        if state['original'] is None:
            (home / '.xinputrc').unlink()
        else:
            atomic_write(home / '.xinputrc', base64.b64decode(state['original']), state['mode'])
    if removing:
        state_path.unlink()
        (home / POINTER).unlink()
    else:
        state['status'] = 'restored'
        save_state(state_path, state)


def main():
    if sys.argv[1:] == ['--restore-all']:
        if os.getuid() != 0:
            raise ValueError('仅卸载程序可以恢复所有已配置用户')
        for account in pwd.getpwall():
            if account.pw_uid == 0 or not (Path(account.pw_dir) / POINTER).is_symlink():
                continue
            try:
                result = subprocess.run(['/usr/sbin/runuser', '-u', account.pw_name, '--',
                                         '/usr/bin/novapinyin-session-setup', '--restore-on-remove'],
                                        capture_output=True, timeout=15)
                if result.returncode:
                    print('NovaPinyin：部分用户的手工配置已保留，可使用 im-config 恢复', file=sys.stderr)
            except subprocess.TimeoutExpired:
                print('NovaPinyin：恢复登录选择超时，保留用户配置', file=sys.stderr)
        return
    if sys.argv[1:] not in (['--auto'], ['--restore'], ['--restore-on-remove']):
        raise ValueError('用法：novapinyin-session-setup --auto | --restore')
    if os.getuid() == 0:
        return  # Never mistake the installer/root account for the desktop user.
    home = Path.home()
    pointer, state_path = state_location(home)
    removing = sys.argv[1] == '--restore-on-remove'
    if not pointer.is_symlink() and sys.argv[1] != '--auto':
        return
    state_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    lock_path = state_path.with_suffix('.lock')
    descriptor = os.open(str(lock_path), os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'r+') as lock:
        info = os.fstat(lock.fileno())
        if info.st_uid != os.getuid() or not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError('配置锁不属于当前用户')
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        if not pointer.is_symlink():
            pointer.symlink_to(state_path)
        if sys.argv[1] == '--auto':
            activate(home, state_path)
        else:
            restore(home, state_path, removing)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        print('NovaPinyin 自动设置未完成：' + str(error), file=sys.stderr)
        sys.exit(1)

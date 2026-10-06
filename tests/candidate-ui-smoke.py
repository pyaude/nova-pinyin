#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect actual classic UI pixels in a private Xvfb and D-Bus session."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

assert os.environ.get('NOVA_ISOLATED_GUI_TEST') == '1'
if os.environ.get('_NOVA_UI_PRIVATE_DBUS') != '1':
    subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve())],
                   env=dict(os.environ, _NOVA_UI_PRIVATE_DBUS='1'), check=True)
    sys.exit(0)
build = Path(os.environ.get('NOVA_BUILD_DIR', 'build')).resolve()
artifacts = Path(os.environ.get('NOVA_UI_ARTIFACT_DIR', '/tmp/nova-candidate-ui-artifacts')).resolve()
artifacts.mkdir(parents=True, exist_ok=True)
daemon_binary = os.environ.get('NOVA_GUI_FCITX_BIN', 'fcitx5')
module = os.environ.get('NOVA_GUI_IM_MODULE', 'fcitx')
remote = os.environ.get('NOVA_GUI_REMOTE_BIN', 'fcitx5-remote')

def snapshot(window):
    xlib = ctypes.CDLL('libX11.so.6')
    display_t, window_t = ctypes.c_void_p, ctypes.c_ulong
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    xlib.XOpenDisplay.restype = display_t
    xlib.XGetGeometry.argtypes = [display_t, window_t, ctypes.POINTER(window_t),
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
        *([ctypes.POINTER(ctypes.c_uint)] * 4)]
    xlib.XGetImage.argtypes = [display_t, window_t, ctypes.c_int, ctypes.c_int,
                             ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
    xlib.XGetImage.restype = ctypes.c_void_p
    xlib.XGetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    xlib.XGetPixel.restype = ctypes.c_ulong
    xlib.XDestroyImage.argtypes = [ctypes.c_void_p]
    xlib.XCloseDisplay.argtypes = [display_t]
    display = xlib.XOpenDisplay(None)
    assert display, 'Cannot open isolated display'
    image = None
    try:
        root, x, y = window_t(), ctypes.c_int(), ctypes.c_int()
        width, height, border, depth = [ctypes.c_uint() for _ in range(4)]
        assert xlib.XGetGeometry(display, window, ctypes.byref(root), ctypes.byref(x),
            ctypes.byref(y), ctypes.byref(width), ctypes.byref(height),
            ctypes.byref(border), ctypes.byref(depth))
        assert depth.value == 24, 'Use a 24-bit Xvfb display'
        image = xlib.XGetImage(display, window, 0, 0, width.value, height.value,
                              ctypes.c_ulong(-1).value, 2)
        assert image, 'Cannot capture candidate window'
        pixels = [[xlib.XGetPixel(image, col, row) & 0xffffff
                   for col in range(width.value)] for row in range(height.value)]
        return pixels
    finally:
        if image:
            xlib.XDestroyImage(image)
        xlib.XCloseDisplay(display)

def save_png(path, pixels):
    height, width = len(pixels), len(pixels[0])
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b''.join(b'\0' + bytes(value for pixel in row
                    for value in ((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
                    for row in pixels)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

with tempfile.TemporaryDirectory(prefix='nova-candidate-ui-') as temporary:
    root = Path(temporary)
    environment = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
        XDG_DATA_HOME=str(root / 'data'), XDG_RUNTIME_DIR=str(root / 'run'),
        XMODIFIERS='@im=fcitx', GTK_IM_MODULE=module, QT_IM_MODULE=module)
    for key in ('FCITX_CONFIG_HOME', 'FCITX_DATA_HOME', 'FCITX_ADDON_DIRS', 'FCITX_DATA_DIRS'):
        environment.pop(key, None)
    (root / 'run').mkdir(mode=0o700)
    config = root / 'config/novapinyin/fcitx5' if daemon_binary == 'novapinyin-fcitx5' else root / 'config/fcitx5'
    (config / 'conf').mkdir(parents=True)
    (config / 'profile').write_text('[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=novapinyin\n'
        '[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n'
        '[Groups/0/Items/1]\nName=novapinyin\nLayout=\n[GroupOrder]\n0=Default\n')
    def run(*args):
        return subprocess.run(args, env=environment, capture_output=True, text=True,
                              check=True, timeout=10).stdout.strip()
    def window_named(name):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', name],
                env=environment, capture_output=True, text=True, timeout=5)
            if result.returncode == 0 and result.stdout.strip():
                return int(result.stdout.splitlines()[-1])
            time.sleep(0.1)
        raise AssertionError('Window did not appear: ' + name)
    themes = ['novapinyin', 'novapinyin-dark']
    if daemon_binary == 'novapinyin-fcitx5':
        themes += ['default', 'default-dark']
    for theme in themes:
        background = 0x1d4ed8
        (config / 'conf/classicui.conf').write_text('Theme=' + theme + '\nUseDarkTheme=False\n')
        with (root / (theme + '.log')).open('w') as log:
            daemon = subprocess.Popen([daemon_binary, '--disable=wayland,ibus,kimpanel'],
                env=environment, stdout=log, stderr=log)
            try:
                time.sleep(1)
                for toolkit in ('gtk', 'qt'):
                    output = root / (toolkit + '-' + theme + '.txt')
                    client = subprocess.Popen([str(build / ('nova-' + toolkit + '-smoke')), str(output)],
                        env=environment, stdout=log, stderr=log)
                    try:
                        window = window_named('NovaPinyin ' + ('GTK' if toolkit == 'gtk' else 'Qt') + ' Smoke')
                        run('xdotool', 'windowfocus', '--sync', str(window))
                        run('xdotool', 'mousemove', '--window', str(window), '40', '30', 'click', '1')
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            assert daemon.poll() is None, 'Input daemon exited'
                            run(remote, '-s', 'novapinyin')
                            run(remote, '-o')
                            if run(remote, '-n') == 'novapinyin' and run(remote) == '2':
                                break
                            time.sleep(0.1)
                        else:
                            raise AssertionError('Client input context did not activate')
                        run('xdotool', 'type', '--clearmodifiers', '--delay', '100', 'hao')
                        candidate_window = window_named('Fcitx5 Input Window')
                        time.sleep(0.2)
                        pixels = snapshot(candidate_window)
                        save_png(artifacts / (toolkit + '-' + theme + '.png'), pixels)
                        colored = [(x, y) for y, row in enumerate(pixels)
                                   for x, color in enumerate(row) if color == background]
                        assert len(colored) > 100, 'Selected candidate background is missing: ' + theme
                        left, right = min(x for x, y in colored), max(x for x, y in colored)
                        top, bottom = min(y for x, y in colored), max(y for x, y in colored)
                        text_pixels = sum(all(((pixels[y][x] >> shift) & 255) >= 200 for shift in (0, 8, 16))
                            for y in range(top + 1, bottom) for x in range(left + 1, right))
                        assert text_pixels > 30, 'Selected candidate text is not visible'
                        height, width = len(pixels), len(pixels[0])
                        assert width > 2 * height, 'Pinyin candidates are not horizontal: %dx%d' % (width, height)
                        assert width >= 400, 'Candidate spacing did not increase'
                        run('xdotool', 'key', 'Down')
                        time.sleep(0.2)
                        paged_pixels = snapshot(candidate_window)
                        assert pixels != paged_pixels, 'Down did not change the candidate page'
                        assert not output.exists() or not output.read_text(), 'Paging committed text'
                        run('xdotool', 'key', 'Up')
                        time.sleep(0.2)
                        assert snapshot(candidate_window) == pixels, 'Up did not restore the first candidate page'
                        run('xdotool', 'key', 'space')
                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            if output.exists() and output.read_text() == '好':
                                break
                            time.sleep(0.1)
                        else:
                            raise AssertionError('Selected visible candidate did not commit 好')
                        print('%s %s: visible blue highlight, spaced horizontal %dx%d, Down/Up paging, committed 好' % (toolkit, theme, width, height))
                    finally:
                        client.terminate()
                        client.wait(timeout=10)
            finally:
                daemon.terminate()
                daemon.wait(timeout=10)
                (artifacts / (theme + '.log')).write_bytes((root / (theme + '.log')).read_bytes())

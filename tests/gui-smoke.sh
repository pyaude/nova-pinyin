#!/bin/bash
# Run inside a NEW dbus-run-session and a private Xvfb display, not the user's desktop.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${NOVA_ISOLATED_GUI_TEST:-}" != 1 ]; then
  echo 'Set NOVA_ISOLATED_GUI_TEST=1 inside a fresh dbus-run-session before running.' >&2
  exit 2
fi
test_dir=$(mktemp -d /tmp/nova-gui-smoke.XXXXXX)
export DISPLAY=:187
export XDG_CONFIG_HOME="$test_dir/config"
export XDG_DATA_HOME="$test_dir/data"
export XDG_RUNTIME_DIR="$test_dir/run"
mkdir -p "$XDG_CONFIG_HOME/fcitx5" "$XDG_DATA_HOME" "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf"
cat > "$XDG_CONFIG_HOME/fcitx5/conf/novapinyin.conf" <<'CONFIG'
Developer=True
Context=True
CONFIG
chmod 700 "$XDG_RUNTIME_DIR"
export XMODIFIERS=@im=fcitx GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx
cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=novapinyin
[Groups/0/Items/0]
Name=keyboard-us
Layout=
[Groups/0/Items/1]
Name=novapinyin
Layout=
[GroupOrder]
0=Default
PROFILE
Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp > "$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
fcitx_pid=''
client_pid=''
cleanup(){
  for pid in "$client_pid" "$fcitx_pid" "$xvfb_pid"; do [ -z "$pid" ] || kill "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT
sleep 1
fcitx5 --disable=wayland,ibus > "$test_dir/fcitx.log" 2>&1 &
fcitx_pid=$!
sleep 2
for toolkit in gtk qt; do
  build/nova-${toolkit}-smoke "$test_dir/$toolkit.txt" > "$test_dir/$toolkit.log" 2>&1 &
  client_pid=$!
  sleep 1
  window=$(xdotool search --name "NovaPinyin $( [ "$toolkit" = gtk ] && echo GTK || echo Qt ) Smoke" | head -1)
  xdotool windowfocus --sync "$window"
  xdotool mousemove --window "$window" 50 50 click 1
  sleep 1
  for attempt in $(seq 1 20); do
    fcitx5-remote -s novapinyin
    fcitx5-remote -o
    sleep 0.2
    [ "$(fcitx5-remote -n)" = novapinyin ] && [ "$(fcitx5-remote)" = 2 ] && break
  done
  [ "$(fcitx5-remote -n)" = novapinyin ]
  [ "$(fcitx5-remote)" = 2 ]
  xdotool type --clearmodifiers --delay 100 nihao
  xdotool key space
  sleep 0.8
  python3 - "$test_dir/$toolkit.txt" <<'PY'
from pathlib import Path
import sys
actual=Path(sys.argv[1]).read_text()
assert actual == '你好', repr(actual)
print(f'{Path(sys.argv[1]).stem}: real frontend committed 你好')
PY
  xdotool key ctrl+alt+space
  xdotool type --clearmodifiers --delay 100 'cancel123'
  xdotool key ctrl+alt+space
  sleep 0.2
  python3 - "$test_dir/$toolkit.txt" <<'PY'
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_text() == '你好', 'Exit chord committed the prefix'
PY
  xdotool key ctrl+alt+space
  xdotool type --clearmodifiers --delay 100 'git che'
  xdotool key Return
  sleep 0.8
  python3 - "$test_dir/$toolkit.txt" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
assert path.read_text() == '你好git checkout', repr(path.read_text())
assert Path(str(path) + '.activated').read_text() == '0', 'Selection Enter reached application'
print(f'{path.stem}: explicit completion committed text; Enter consumed')
PY
  xdotool key Return
  sleep 0.3
  test "$(cat "$test_dir/$toolkit.txt.activated")" = 1
  kill "$client_pid"
  wait "$client_pid" 2>/dev/null || true
  client_pid=''
done
printf 'Logs: %s\n' "$test_dir"

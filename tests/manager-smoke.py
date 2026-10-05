#!/usr/bin/python3
"""Run with xvfb-run and private XDG directories after installing the package."""
import os
import runpy
import subprocess
import sys
import tkinter as tk
import tempfile
import time
from pathlib import Path
from unittest.mock import patch

assert os.environ.get("NOVA_ISOLATED_GUI_TEST") == "1"
if os.environ.get("_NOVA_MANAGER_PRIVATE_DBUS") != "1":
    # Save requests a daemon reload. Give the test its own bus even when the caller
    # inherited a desktop bus; fcitx5-remote also aborts when no session bus exists.
    environment = os.environ.copy()
    environment["_NOVA_MANAGER_PRIVATE_DBUS"] = "1"
    environment.pop("WAYLAND_DISPLAY", None)
    with tempfile.TemporaryDirectory(prefix="nova-manager-run-") as runtime:
        environment["XDG_RUNTIME_DIR"] = runtime
        result = subprocess.run(
            ["dbus-run-session", "--", sys.executable, str(Path(__file__).resolve())],
            env=environment, check=False)
    sys.exit(result.returncode)
manager = runpy.run_path("/usr/bin/novapinyin-manager", run_name="smoke")
root = tk.Tk()
try:
    with patch("tkinter.messagebox.showinfo"), patch("tkinter.messagebox.showerror") as errors:
        app = manager["Manager"](root)
        root.update()
        app.values["Shuangpin"].set(True)
        app.profile.current(1)
        app.save()
        assert "Shuangpin=True" in manager["config_path"]().read_text()
        app.examples()
        assert len(app.tree.get_children()) == 2
        app.tree.selection_set(app.tree.get_children()[0])
        app.toggle()
        assert "0\t" in manager["run_tool"]("list")
        app.values["Shuangpin"].set(False)
        app.values["Developer"].set(True)
        app.values["Context"].set(True)
        app.save()
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "main.py"
            source.write_text("def ProjectDemoFunction(argument):\n    return argument\n")
            app.begin_index("smoke-project", temporary)
            deadline = time.monotonic() + 15
            while app.index_future is not None and time.monotonic() < deadline:
                root.update()
                time.sleep(0.02)
            assert app.index_future is None, "Background indexing timed out"
            assert len(app.project_tree.get_children()) == 1
            app.active_project.set("smoke-project")
            app.save()
            assert "ActiveProject=smoke-project" in manager["config_path"]().read_text()
        root.update()
        errors.assert_not_called()
    print("manager: settings, dictionaries, background project indexing and selection passed")
finally:
    root.destroy()

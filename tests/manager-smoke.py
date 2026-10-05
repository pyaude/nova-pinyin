#!/usr/bin/python3
"""Run with xvfb-run and private XDG directories after installing the package."""
import os
import runpy
import tkinter as tk
import tempfile
import time
from pathlib import Path
from unittest.mock import patch

assert os.environ.get("NOVA_ISOLATED_GUI_TEST") == "1"
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

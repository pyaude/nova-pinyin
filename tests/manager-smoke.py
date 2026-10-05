#!/usr/bin/python3
"""Run with xvfb-run and private XDG directories after installing the package."""
import os
import runpy
import tkinter as tk
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
        app.save()
        root.update()
        errors.assert_not_called()
    print("manager: settings save, example import and dictionary toggle passed")
finally:
    root.destroy()

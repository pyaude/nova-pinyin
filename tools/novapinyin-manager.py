#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Local dictionary management; never captures text or uses a network service."""
import os
from pathlib import Path
import subprocess
import concurrent.futures
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog


def run_tool(*args):
    result = subprocess.run(["novapinyin-tool", *args], text=True, capture_output=True,
                            timeout=30, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "词库操作失败")
    return result.stdout


def config_path():
    framework = os.environ.get("FCITX_CONFIG_HOME", "")
    if framework and Path(framework).is_absolute():
        return Path(framework) / "conf/novapinyin.conf"
    base = os.environ.get("XDG_CONFIG_HOME", "")
    if not base or not Path(base).is_absolute():
        base = str(Path.home() / ".config")
    return Path(base) / "fcitx5/conf/novapinyin.conf"


def int_setting(saved, key, default, minimum, maximum):
    try:
        return max(minimum, min(maximum, int(saved.get(key, default))))
    except (ValueError, TypeError):
        return default


class Manager:
    def __init__(self, root):
        self.root = root
        root.title("NovaPinyin 设置与词库管理")
        root.geometry("840x760")
        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True, padx=12, pady=12)
        settings, dictionaries, projects = ttk.Frame(notebook), ttk.Frame(notebook), ttk.Frame(notebook)
        notebook.add(settings, text="输入设置")
        notebook.add(dictionaries, text="本地词库")
        notebook.add(projects, text="项目补全")
        self.index_future = None
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.values = {}
        defaults = {
            "Shuangpin": False, "Typo": True, "Traditional": False, "Emoji": True,
            "Learning": True, "Privacy": False, "Punctuation": True, "ShiftSwitch": True,
            "Context": False, "Developer": False,
            "ApplicationHints": False,
            "FuzzyZ_ZH": False, "FuzzyC_CH": False, "FuzzyS_SH": False,
            "FuzzyN_L": False, "FuzzyAN_ANG": False, "FuzzyEN_ENG": False,
            "FuzzyIN_ING": False,
        }
        self.defaults = defaults
        labels = {
            "Shuangpin": "双拼输入", "Typo": "有限拼音纠错（不自动提交）",
            "Traditional": "繁体输出", "Emoji": "显示表情候选",
            "Learning": "学习选词", "Privacy": "隐私模式（停用个人词条与学习）",
            "Context": "内存上下文排序（默认关闭）", "Developer": "显式开发者补全（默认关闭）",
            "ApplicationHints": "可覆盖的应用类别提示（默认关闭）",
            "Punctuation": "中文标点（终端默认半角）", "ShiftSwitch": "单独 Shift 切换中英文",
            "FuzzyZ_ZH": "z / zh", "FuzzyC_CH": "c / ch", "FuzzyS_SH": "s / sh",
            "FuzzyN_L": "n / l", "FuzzyAN_ANG": "an / ang",
            "FuzzyEN_ENG": "en / eng", "FuzzyIN_ING": "in / ing",
        }
        saved = {}
        try:
            for line in config_path().read_text().splitlines():
                if "=" in line and not line.startswith("#"):
                    k, v = line.split("=", 1)
                    saved[k.strip()] = v.strip()
        except FileNotFoundError:
            pass
        self.extra_config = {key: value for key, value in saved.items()
                             if key not in defaults and key not in ("PageSize", "ShuangpinProfile", "DeveloperKey", "ApplicationOverrides", "ActiveProject")}
        self.developer_key = tk.StringVar(value=saved.get("DeveloperKey", "Control+Alt+space"))
        self.app_overrides = tk.StringVar(value=saved.get("ApplicationOverrides", ""))
        self.active_project = tk.StringVar(value=saved.get("ActiveProject", ""))
        for index, (key, default) in enumerate(defaults.items()):
            self.values[key] = tk.BooleanVar(value=saved.get(key, str(default)).lower() == "true")
            ttk.Checkbutton(settings, text=labels[key], variable=self.values[key]).grid(
                row=index // 2, column=index % 2, sticky="w", padx=14, pady=7)
        ttk.Label(settings, text="候选页大小（3–9）：").grid(row=9, column=0, sticky="w", padx=14)
        self.page = tk.IntVar(value=int_setting(saved, "PageSize", 9, 3, 9))
        ttk.Spinbox(settings, from_=3, to=9, textvariable=self.page, width=6).grid(row=9, column=1, sticky="w")
        ttk.Label(settings, text="双拼方案：").grid(row=10, column=0, sticky="w", padx=14, pady=12)
        self.profile = ttk.Combobox(settings, values=["自然码", "小鹤", "微软"], state="readonly", width=12)
        self.profile.current(int_setting(saved, "ShuangpinProfile", 0, 0, 2))
        self.profile.grid(row=10, column=1, sticky="w")
        ttk.Label(settings, text="补全快捷键（Fcitx5 格式）：").grid(row=11, column=0, sticky="w", padx=14)
        ttk.Entry(settings, textvariable=self.developer_key, width=34).grid(row=11, column=1, sticky="w")
        ttk.Label(settings, text="应用提示覆盖（如 kitty=terminal;code=editor）：").grid(row=12, column=0, sticky="w", padx=14, pady=10)
        ttk.Entry(settings, textvariable=self.app_overrides, width=34).grid(row=12, column=1, sticky="w")
        ttk.Button(settings, text="保存设置", command=lambda: self.guard(self.save)).grid(row=13, column=0, pady=18)
        ttk.Button(settings, text="恢复默认设置", command=self.reset).grid(row=13, column=1)
        ttk.Label(settings, text="补全模式：Space 输入空格，↑↓ 选择，Tab/Enter 提交文字，Esc 退出。\n隐私模式禁用上下文、补全与个人词条；不会保存完整输入历史。", wraplength=800).grid(row=14, column=0, columnspan=2, padx=14, sticky="w")
        self.tree = ttk.Treeview(dictionaries, columns=("enabled",), show="tree headings", height=12)
        self.tree.heading("#0", text="词库")
        self.tree.heading("enabled", text="状态")
        self.tree.column("enabled", width=100)
        self.tree.pack(fill="both", expand=True, padx=12, pady=12)
        buttons = ttk.Frame(dictionaries)
        buttons.pack(fill="x", padx=12)
        for label, callback in [("导入 TSV", self.import_dict), ("启用 / 禁用", self.toggle),
                                ("移除词库", self.remove), ("安装示例词库", self.examples),
                                ("刷新", self.refresh)]:
            ttk.Button(buttons, text=label, command=lambda c=callback: self.guard(c)).pack(side="left", padx=3)
        personal = ttk.Frame(dictionaries)
        personal.pack(pady=18)
        ttk.Button(personal, text="导出个人词条", command=lambda: self.guard(self.export)).pack(side="left", padx=8)
        ttk.Button(personal, text="清空个人学习数据…", command=lambda: self.guard(self.clear)).pack(side="left", padx=8)
        ttk.Label(dictionaries, text="词库为本机保存，不上传网络。导入失败保留已有数据。\n清空学习数据保留领域词库；可另行禁用或移除。", wraplength=620).pack(pady=12)
        self.guard(self.refresh)
        ttk.Label(projects, text="请选择项目并显式索引。仅提取源码标识符，不执行项目代码；索引不会自动监控文件变化。\n遵循 Git 忽略规则和 .novapinyinignore，排除隐藏文件、凭证、符号链接、二进制和超大文件。", wraplength=800).pack(padx=12, pady=12)
        self.project_tree = ttk.Treeview(projects, columns=("root", "count"), show="tree headings", height=12)
        self.project_tree.heading("#0", text="项目")
        self.project_tree.heading("root", text="用户指定目录")
        self.project_tree.heading("count", text="标识符数")
        self.project_tree.column("#0", width=130)
        self.project_tree.column("root", width=470)
        self.project_tree.column("count", width=90)
        self.project_tree.pack(fill="both", expand=True, padx=12, pady=12)
        controls = ttk.Frame(projects)
        controls.pack(fill="x", padx=12, pady=12)
        for label, callback in [("添加项目并索引…", self.add_project), ("重新索引", self.reindex_project),
                                ("移除索引", self.remove_project), ("刷新", self.refresh_projects)]:
            ttk.Button(controls, text=label, command=lambda c=callback: self.guard(c)).pack(side="left", padx=5)
        active = ttk.Frame(projects)
        active.pack(padx=12, pady=12)
        ttk.Label(active, text="补全使用的项目（空为仅内置命令）：").pack(side="left")
        self.project_choice = ttk.Combobox(active, textvariable=self.active_project, state="readonly", width=26)
        self.project_choice.pack(side="left", padx=8)
        ttk.Button(active, text="保存项目选择", command=lambda: self.guard(self.save)).pack(side="left")
        self.index_status = tk.StringVar(value="尚未开始索引")
        ttk.Label(projects, textvariable=self.index_status).pack(pady=12)
        self.guard(self.refresh_projects)

    def guard(self, fn):
        try:
            fn()
        except (Exception,) as exc:
            messagebox.showerror("NovaPinyin", str(exc), parent=self.root)

    def save(self):
        page = self.page.get()
        if not 3 <= page <= 9:
            raise ValueError("候选页大小应为 3–9")
        path = config_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        text = "\n".join(f"{k}={str(v.get())}" for k, v in self.values.items())
        text += f"\nPageSize={page}\nShuangpinProfile={self.profile.current()}\n"
        for key, value, maximum in (("DeveloperKey", self.developer_key.get(), 128),
                                    ("ApplicationOverrides", self.app_overrides.get(), 4096),
                                    ("ActiveProject", self.active_project.get(), 80)):
            if len(value) > maximum or any(c in value for c in "\r\n\t\0"):
                raise ValueError("快捷键、应用覆盖或项目名称不合法")
            text += f"{key}={value}\n"
        text += "".join(f"{key}={value}\n" for key, value in self.extra_config.items())
        temporary = path.with_suffix(".tmp")
        temporary.write_text(text)
        temporary.chmod(0o600)
        temporary.replace(path)
        # -r asks the existing daemon to reload configuration; never replace the desktop framework.
        try:
            subprocess.run(["fcitx5-remote", "-r"], timeout=5, capture_output=True, check=False)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
        messagebox.showinfo("NovaPinyin", "设置已保存。若当前会话未刷新，请通过 Fcitx5 菜单重新启动输入法。", parent=self.root)

    def reset(self):
        if messagebox.askyesno("恢复默认", "恢复输入设置？个人词条与词库将保留。", parent=self.root):
            for key, value in self.defaults.items():
                self.values[key].set(value)
            self.page.set(9)
            self.profile.current(0)
            self.extra_config = {}
            self.developer_key.set("Control+Alt+space")
            self.app_overrides.set("")
            self.active_project.set("")
            self.guard(self.save)

    def refresh(self):
        self.tree.delete(*self.tree.get_children())
        for line in run_tool("list").splitlines():
            enabled, name = line.split("\t", 1)
            self.tree.insert("", "end", text=name, values=("启用" if enabled == "1" else "禁用",))

    def selected(self):
        selected = self.tree.selection()
        if not selected:
            raise ValueError("请先选择一个词库")
        return self.tree.item(selected[0])

    def import_dict(self):
        path = filedialog.askopenfilename(title="选择 UTF-8 TSV 词库", filetypes=[("TSV 词库", "*.tsv"), ("所有文件", "*")])
        if path:
            name = simpledialog.askstring("词库名称", "同名导入将替换原词库：", initialvalue=Path(path).stem, parent=self.root)
            if name:
                output = run_tool("import", name, path)
                self.refresh()
                messagebox.showinfo("导入完成", output, parent=self.root)

    def toggle(self):
        item = self.selected()
        run_tool("disable" if item["values"][0] == "启用" else "enable", item["text"])
        self.refresh()

    def remove(self):
        item = self.selected()
        if messagebox.askyesno("移除词库", f"移除“{item['text']}”？个人学习词条不会删除。", parent=self.root):
            run_tool("remove", item["text"])
            self.refresh()

    def examples(self):
        for name in ("semiconductor", "programming"):
            run_tool("import", name, f"/usr/share/novapinyin/{name}.tsv")
        self.refresh()

    def export(self):
        path = filedialog.asksaveasfilename(title="导出个人词条", defaultextension=".tsv")
        if path:
            run_tool("export", path)
            messagebox.showinfo("NovaPinyin", "已导出个人词条。", parent=self.root)

    def clear(self):
        if self.index_future is not None:
            raise ValueError("请等待索引任务结束后再清空数据")
        if messagebox.askyesno("清空个人学习数据", "永久清空本机个人词条、选词统计与全部项目索引？\n建议先导出备份。安装的领域词库将保留。", parent=self.root):
            run_tool("clear", "--yes")
            self.active_project.set("")
            self.refresh_projects()
            messagebox.showinfo("NovaPinyin", "已清空。运行中的输入法将在后台刷新。", parent=self.root)

    def refresh_projects(self):
        self.project_tree.delete(*self.project_tree.get_children())
        names = [""]
        for line in run_tool("projects").splitlines():
            name, directory, count = line.split("\t")
            names.append(name)
            self.project_tree.insert("", "end", text=name, values=(directory, count))
        self.project_choice.configure(values=names)

    def selected_project(self):
        selected = self.project_tree.selection()
        if not selected:
            raise ValueError("请先选择项目")
        return self.project_tree.item(selected[0])

    def add_project(self):
        directory = filedialog.askdirectory(title="选择待索引的项目目录（不会执行项目代码）")
        if directory:
            name = simpledialog.askstring("项目名称", "同名索引成功后将替换原数据：", initialvalue=Path(directory).name, parent=self.root)
            if name:
                self.begin_index(name, directory)

    def reindex_project(self):
        item = self.selected_project()
        self.begin_index(item["text"], item["values"][0])

    def begin_index(self, name, directory):
        if self.index_future is not None:
            raise ValueError("已有索引任务，请等待完成")
        def work():
            result = subprocess.run(["novapinyin-project", name, directory], text=True,
                                    capture_output=True, timeout=45, check=False)
            if result.returncode:
                raise RuntimeError(result.stderr.strip() or "索引失败，原数据保留")
            return result.stdout.strip()
        self.index_status.set("正在后台索引…输入法和窗口仍可使用")
        self.index_future = self.executor.submit(work)
        self.root.after(100, self.poll_index)

    def poll_index(self):
        if not self.index_future.done():
            self.root.after(100, self.poll_index)
            return
        future, self.index_future = self.index_future, None
        try:
            self.index_status.set(future.result())
            self.refresh_projects()
        except Exception as exc:
            self.index_status.set("索引失败，原数据保留")
            messagebox.showerror("NovaPinyin", str(exc), parent=self.root)

    def remove_project(self):
        if self.index_future is not None:
            raise ValueError("请等待索引任务完成后再移除")
        item = self.selected_project()
        if messagebox.askyesno("移除项目索引", f"移除“{item['text']}”的索引？项目源文件保持不变。", parent=self.root):
            run_tool("project-remove", item["text"])
            if self.active_project.get() == item["text"]:
                self.active_project.set("")
                self.save()
            self.refresh_projects()


if __name__ == "__main__":
    window = tk.Tk()
    Manager(window)
    window.mainloop()

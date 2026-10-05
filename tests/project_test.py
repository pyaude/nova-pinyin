#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("project", Path(__file__).resolve().parents[1] / "tools/novapinyin-project.py")
project = importlib.util.module_from_spec(spec)
spec.loader.exec_module(project)


class ProjectIndex(unittest.TestCase):
    def test_gitignore_secrets_comments_and_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as outside:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / ".gitignore").write_text("ignored.py\nignored_dir/\n")
            (root / ".novapinyinignore").write_text("custom.py\n")
            (root / "main.py").write_text('def ProcessInput(argument_name):\n    return argument_name\n# CommentOnlySymbol\nvalue = "StringOnlySymbol"\n')
            (root / "source.cpp").write_text('void CppFunction() { const char *value = "LiteralSymbol"; } /* BlockCommentSymbol */')
            (root / "template.ts").write_text('function TemplateFunction() { return `TemplateLiteralSymbol`; }')
            for name in ("ignored.py", "custom.py", "credentials.py", ".hidden.py"):
                (root / name).write_text("ForbiddenSymbol = 1\n")
            (root / "ignored_dir").mkdir()
            (root / "ignored_dir/child.py").write_text("ForbiddenSymbol = 1\n")
            (Path(outside) / "external.py").write_text("OutsideSymbol = 1\n")
            (root / "link.py").symlink_to(Path(outside) / "external.py")
            (root / "external").symlink_to(outside, target_is_directory=True)
            (root / "binary.py").write_bytes(b"BinarySymbol\0\xff")
            (root / "large.py").write_text("BigSymbol\n" + " " * project.MAX_FILE)
            _, words, stats = project.scan(root)
            self.assertIn("ProcessInput", words)
            self.assertIn("CppFunction", words)
            self.assertIn("TemplateFunction", words)
            for word in ("ForbiddenSymbol", "OutsideSymbol", "LiteralSymbol", "BlockCommentSymbol",
                         "CommentOnlySymbol", "StringOnlySymbol", "TemplateLiteralSymbol", "BigSymbol"):
                self.assertNotIn(word, words)
            self.assertGreaterEqual(stats["skipped"], 3)

    def test_nested_ignore_and_tracked_ignored_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "sub").mkdir()
            (root / "sub/tracked.py").write_text("TrackedHiddenSymbol = 1\n")
            subprocess.run(["git", "-C", str(root), "add", "sub/tracked.py"], check=True)
            (root / "sub/.gitignore").write_text("tracked.py\n*.generated.py\n!keep.generated.py\n")
            (root / "sub/drop.generated.py").write_text("DropSymbol = 1\n")
            (root / "sub/keep.generated.py").write_text("KeepSymbol = 1\n")
            _, words, _ = project.scan(root)
            self.assertNotIn("TrackedHiddenSymbol", words)
            self.assertNotIn("DropSymbol", words)
            self.assertIn("KeepSymbol", words)

    def test_limits_and_invalid_ignore_abort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = SecondSymbol\n")
            with patch.object(project, "MAX_TERMS", 1):
                with self.assertRaises(ValueError):
                    project.scan(root)
            (root / ".novapinyinignore").write_text("!main.py\n")
            with self.assertRaises(ValueError):
                project.scan(root)

    def test_safe_open_rejects_parent_symlink(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as outside:
            root = Path(temporary)
            (Path(outside) / "data.py").write_text("ExternalSymbol = 1")
            (root / "link").symlink_to(outside, target_is_directory=True)
            fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            try:
                with self.assertRaises(OSError):
                    project.safe_read(fd, "link/data.py", project.MAX_FILE)
            finally:
                os.close(fd)

    def test_deadline_checked_inside_a_single_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = 1\n")
            now = [0]
            read = project.safe_read
            def slow_read(*args):
                data = read(*args)
                now[0] = 21
                return data
            with patch.object(project.time, "monotonic", side_effect=lambda: now[0]), \
                 patch.object(project, "safe_read", side_effect=slow_read):
                with self.assertRaises(ValueError):
                    project.scan(root)

    def test_deadline_after_last_file_tokenization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = 1\n")
            now = [0]
            def slow_identifiers(*args):
                now[0] = 21
                return ["FirstSymbol"]
            with patch.object(project.time, "monotonic", side_effect=lambda: now[0]), \
                 patch.object(project, "identifiers", side_effect=slow_identifiers):
                with self.assertRaises(ValueError):
                    project.scan(root)

    def test_deadline_after_failed_read(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = 1\n")
            now = [0]
            def slow_failure(*args):
                now[0] = 21
                raise OSError("unreadable")
            with patch.object(project.time, "monotonic", side_effect=lambda: now[0]), \
                 patch.object(project, "safe_read", side_effect=slow_failure):
                with self.assertRaises(ValueError):
                    project.scan(root)

    def test_enumeration_stops_at_entry_limit(self):
        class Entries:
            seen = 0
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def __iter__(self):
                return self
            def __next__(self):
                self.seen += 1
                if self.seen > 10:
                    raise AssertionError("directory enumeration was not bounded")
                class Entry:
                    name = "main.py"
                    def is_dir(self, follow_symlinks):
                        return False
                return Entry()
        with tempfile.TemporaryDirectory() as temporary:
            fd = os.open(temporary, os.O_RDONLY | os.O_DIRECTORY)
            entries = Entries()
            try:
                with patch.object(project, "MAX_ENTRIES", 2), \
                     patch.object(project.os, "scandir", return_value=entries):
                    with self.assertRaises(ValueError):
                        list(project.bounded_walk(fd, lambda: None))
                self.assertEqual(entries.seen, 3)
            finally:
                os.close(fd)

    def test_directory_replacement_during_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as outside:
            root = Path(temporary)
            (root / "sub").mkdir()
            (Path(outside) / "main.py").write_text("OutsideSymbol = 1\n")
            fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            walk = project.bounded_walk(fd, lambda: None)
            try:
                _, dirs, _ = next(walk)
                self.assertEqual(dirs, ["sub"])
                (root / "sub").rmdir()
                (root / "sub").symlink_to(outside, target_is_directory=True)
                with self.assertRaises(OSError):
                    next(walk)
            finally:
                walk.close()
                os.close(fd)

    def test_rejected_files_count_toward_scan_budgets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("first.py", "second.py"):
                (root / name).write_bytes(b"InvalidSymbol\0")
            with patch.object(project, "MAX_FILES", 1):
                with self.assertRaises(ValueError):
                    project.scan(root)
            with patch.object(project, "MAX_TOTAL", 1):
                with self.assertRaises(ValueError):
                    project.scan(root)

    def test_safe_read_rejects_absolute_and_parent_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = 1\n")
            fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            try:
                for relative in (root / "main.py", "../main.py", "."):
                    with self.assertRaises(ValueError):
                        project.safe_read(fd, relative, project.MAX_FILE)
            finally:
                os.close(fd)

    def test_git_timeout_uses_remaining_scan_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "main.py").write_text("FirstSymbol = 1\n")
            now = [0]
            timeouts = []
            def git(root, *args, timeout, **kwargs):
                timeouts.append(timeout)
                if args[0] == "rev-parse":
                    now[0] = 19
                    return subprocess.CompletedProcess(args, 0)
                raise subprocess.TimeoutExpired(args, timeout)
            with patch.object(project.time, "monotonic", side_effect=lambda: now[0]), \
                 patch.object(project, "git_command", side_effect=git):
                with self.assertRaisesRegex(ValueError, "超时"):
                    project.scan(root)
            self.assertEqual(timeouts, [10, 1])

    def test_directory_count_and_depth_limits(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "sub/deep").mkdir(parents=True)
            (root / "sub/main.py").write_text("IncludedSymbol = 1\n")
            (root / "sub/deep/main.py").write_text("TooDeepSymbol = 1\n")
            with patch.object(project, "MAX_DIRECTORIES", 1):
                with self.assertRaises(ValueError):
                    project.scan(root)
            with patch.object(project, "MAX_DEPTH", 1):
                _, words, _ = project.scan(root)
            self.assertIn("IncludedSymbol", words)
            self.assertNotIn("TooDeepSymbol", words)

    def test_failed_scan_never_imports_partial_index(self):
        generation = subprocess.CompletedProcess([], 0, stdout="7\n")
        with patch.object(project.subprocess, "run", return_value=generation) as run, \
             patch.object(project, "scan", side_effect=ValueError("scan budget exceeded")):
            with self.assertRaises(ValueError):
                project.index_project("demo", "/unused")
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0], ["novapinyin-tool", "project-generation"])


if __name__ == "__main__":
    unittest.main()

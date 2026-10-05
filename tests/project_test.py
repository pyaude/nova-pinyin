#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
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
            (root / "sub/tracked.py").write_text("TrackedSecret = 1\n")
            subprocess.run(["git", "-C", str(root), "add", "sub/tracked.py"], check=True)
            (root / "sub/.gitignore").write_text("tracked.py\n*.generated.py\n!keep.generated.py\n")
            (root / "sub/drop.generated.py").write_text("DropSymbol = 1\n")
            (root / "sub/keep.generated.py").write_text("KeepSymbol = 1\n")
            _, words, _ = project.scan(root)
            self.assertNotIn("TrackedSecret", words)
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
        import os
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


if __name__ == "__main__":
    unittest.main()

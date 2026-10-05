#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, bounded source identifier index. Never executes project code."""
import collections
import fnmatch
import io
import keyword
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import time
import tokenize

MAX_FILE = 256 * 1024
MAX_TOTAL = 32 * 1024 * 1024
MAX_FILES = 2000
MAX_TERMS = 20000
MAX_DIRECTORIES = 2000
MAX_ENTRIES = 10000
MAX_DEPTH = 32
MAX_SECONDS = 20
EXTENSIONS = {".c", ".h", ".cc", ".cpp", ".hpp", ".cxx", ".py", ".rs", ".go",
              ".js", ".jsx", ".ts", ".tsx", ".java", ".kt", ".sh", ".cmake"}
EXCLUDED_DIRS = {"node_modules", "vendor", "build", "dist", "target", "venv", "__pycache__"}
SENSITIVE = re.compile(r"secret|credential|password|token|private.?key|api.?key|\.pem$|\.key$", re.I)
IDENTIFIER = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]{2,63}(?![A-Za-z0-9_])")
LITERALS = re.compile(r'/\*[\s\S]*?(?:\*/|$)|//[^\n]*|\#[^\n]*|"(?:\\[\s\S]|[^"\\])*(?:"|$)|\'(?:\\[\s\S]|[^\'\\])*(?:\'|$)|`(?:\\[\s\S]|[^`\\])*(?:`|$)')
STOP = set(keyword.kwlist) | {"return", "const", "static", "void", "int", "char", "bool", "true", "false",
    "null", "nullptr", "class", "public", "private", "protected", "import", "from", "let", "var",
    "function", "export", "default", "this", "self", "struct", "enum", "else", "while", "for",
    "include", "define", "unsigned", "sizeof", "using", "namespace", "new", "delete", "auto"}


def open_directory(root_fd, relative):
    """Open every directory component relative to the pinned project root."""
    relative = Path(relative)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("非法相对路径")
    current = os.dup(root_fd)
    try:
        for part in relative.parts:
            following = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=current)
            os.close(current)
            current = following
        return current
    except BaseException:
        os.close(current)
        raise


def safe_read(root_fd, relative, maximum):
    """Open every component without following links, including concurrent replacements."""
    relative = Path(relative)
    if relative.is_absolute() or not relative.parts or ".." in relative.parts:
        raise ValueError("非法相对路径")
    current = open_directory(root_fd, relative.parent)
    try:
        descriptor = os.open(relative.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=current)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > maximum:
                raise ValueError("非普通文件或文件过大")
            data = stream.read(maximum + 1)
            if len(data) > maximum:
                raise ValueError("文件增长超出限制")
            return data
    finally:
        os.close(current)


def bounded_walk(root_fd, check_budget):
    """Bound enumeration itself and never traverse a substituted directory symlink."""
    pending = [Path()]
    directories = 0
    while pending:
        check_budget()
        directories += 1
        if directories > MAX_DIRECTORIES:
            raise ValueError("扫描目录数超过限制；请缩小项目或添加忽略规则")
        relative = pending.pop()
        current = open_directory(root_fd, relative)
        try:
            dirs, names = [], []
            with os.scandir(current) as entries:
                for entry in entries:
                    check_budget()
                    if len(dirs) + len(names) >= MAX_ENTRIES:
                        raise ValueError("单目录文件数超过 10000")
                    if entry.is_dir(follow_symlinks=False):
                        dirs.append(entry.name)
                    else:
                        names.append(entry.name)
            check_budget()
            if len(relative.parts) >= MAX_DEPTH:
                dirs.clear()
            # The caller prunes dirs using its privacy and ignore rules before we resume.
            yield relative, dirs, names
            pending.extend(relative / name for name in reversed(dirs))
        finally:
            os.close(current)


def identifiers(text, suffix):
    if suffix == ".py":
        try:
            words = [t.string for t in tokenize.generate_tokens(io.StringIO(text).readline)
                     if t.type == tokenize.NAME and IDENTIFIER.fullmatch(t.string)]
        except (tokenize.TokenError, IndentationError, SyntaxError):
            return []
    else:
        words = IDENTIFIER.findall(LITERALS.sub(" ", text))
    return [w for w in words if w not in STOP and not SENSITIVE.search(w)
            and not (len(w) >= 32 and re.fullmatch(r"[a-fA-F0-9]+", w))]


def git_command(root, *args, timeout=10, **kwargs):
    return subprocess.run(["git", "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null",
                           "-C", str(root), *args], capture_output=True, timeout=timeout, check=False, **kwargs)


def scan(root):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("请选择项目目录")
    if any(c in str(root) for c in "\r\n\t"):
        raise ValueError("项目目录不能含制表符或换行")
    started = time.monotonic()
    def check_budget():
        if time.monotonic() - started >= MAX_SECONDS:
            raise ValueError("扫描耗时超过 20 秒，原索引保留")

    def run_git(*args, **kwargs):
        check_budget()
        remaining = MAX_SECONDS - (time.monotonic() - started)
        if remaining <= 0:
            check_budget()
        try:
            result = git_command(root, *args, timeout=min(10, remaining), **kwargs)
        except subprocess.TimeoutExpired as exc:
            raise ValueError("Git 忽略规则校验超时，原索引保留") from exc
        check_budget()
        return result

    is_git = run_git("rev-parse", "--is-inside-work-tree").returncode == 0
    root_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        patterns = []
        if (root / ".novapinyinignore").exists():
            content = safe_read(root_fd, ".novapinyinignore", 65536).decode("utf-8")
            for line in content.splitlines():
                check_budget()
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if line.startswith("!"):
                    raise ValueError(".novapinyinignore 仅支持排除规则，不支持 ! 反选")
                patterns.append(line.lstrip("/").rstrip("/"))
        counts = collections.Counter()
        files = attempted = total = skipped = 0
        for relative, dirs, names in bounded_walk(root_fd, check_budget):
            def excluded(name, directory=False):
                check_budget()
                p = (relative / name).as_posix()
                if name.startswith(".") or SENSITIVE.search(name):
                    return True
                if directory and (name in EXCLUDED_DIRS or name.startswith(("build-", "cmake-build-"))):
                    return True
                for pattern in patterns:
                    check_budget()
                    if (fnmatch.fnmatch(p, pattern) or fnmatch.fnmatch(name, pattern) or
                            fnmatch.fnmatch(p, pattern + "/**")):
                        return True
                return False
            dirs[:] = sorted(d for d in dirs if not excluded(d, True))
            has_gitignore = ".gitignore" in names
            names = sorted(n for n in names if not excluded(n) and
                           (Path(n).suffix.lower() in EXTENSIONS or n == "CMakeLists.txt"))
            if is_git and (dirs or names):
                if has_gitignore:
                    ignore = safe_read(root_fd, relative / ".gitignore", 65536).decode("utf-8")
                    if any(len(line) > 4096 for line in ignore.splitlines()):
                        raise ValueError("Git 忽略规则行过长")
                paths = [(relative / n).as_posix() + "/" for n in dirs]
                paths += [(relative / n).as_posix() for n in names]
                payload = b"\0".join(os.fsencode(p) for p in paths) + b"\0"
                if len(payload) > 1024 * 1024:
                    raise ValueError("单目录文件列表过大")
                result = run_git("check-ignore", "--no-index", "--stdin", "-z", "-v", input=payload)
                if result.returncode not in (0, 1):
                    raise ValueError("Git 忽略规则校验失败，索引未修改")
                fields = result.stdout.split(b"\0")
                if fields and fields[-1] == b"": fields.pop()
                if len(fields) % 4:
                    raise ValueError("Git 忽略规则输出格式错误")
                ignored = {os.fsdecode(fields[i + 3]).rstrip("/") for i in range(0, len(fields), 4)
                           if not fields[i + 2].startswith(b"!")}
                dirs[:] = [d for d in dirs if (relative / d).as_posix() not in ignored]
                names = [n for n in names if (relative / n).as_posix() not in ignored]
            for name in names:
                check_budget()
                if attempted >= MAX_FILES:
                    raise ValueError("源码文件超过 2000；请添加忽略规则")
                attempted += 1
                try:
                    data = safe_read(root_fd, relative / name, MAX_FILE)
                except (OSError, ValueError):
                    check_budget()
                    skipped += 1
                    continue
                check_budget()
                total += len(data)
                if total > MAX_TOTAL:
                    raise ValueError("源码总大小超过 32 MiB")
                try:
                    if b"\0" in data:
                        raise ValueError("二进制文件")
                    text = data.decode("utf-8")
                except (OSError, UnicodeError, ValueError):
                    skipped += 1
                    continue
                files += 1
                counts.update(identifiers(text, Path(name).suffix.lower()))
                check_budget()
                if len(counts) > MAX_TERMS:
                    raise ValueError("标识符超过 20000；请缩小项目")
        check_budget()
        return root, counts, {"files": files, "skipped": skipped}
    finally:
        os.close(root_fd)


def index_project(name, directory):
    generation_result = subprocess.run(["novapinyin-tool", "project-generation"], text=True,
                                       capture_output=True, timeout=10, check=False)
    if generation_result.returncode:
        raise RuntimeError("无法读取项目数据版本")
    generation = str(int(generation_result.stdout.strip()))
    root, words, stats = scan(directory)
    with tempfile.TemporaryDirectory(prefix="novapinyin-index-") as folder:
        output = Path(folder) / "terms.tsv"
        output.write_text("".join(f"{word}\t{min(count, 1000000)}\n" for word, count in sorted(words.items())), encoding="utf-8")
        output.chmod(0o600)
        result = subprocess.run(["novapinyin-tool", "project-import", name, str(root), str(output), generation],
                                text=True, capture_output=True, timeout=10, check=False)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or "索引写入失败")
    return f"已扫描 {stats['files']} 个源码文件，索引 {len(words)} 个标识符；跳过 {stats['skipped']} 个不可索引文件。"


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("用法：novapinyin-project 项目名称 项目目录")
        print(index_project(sys.argv[1], sys.argv[2]))
    except Exception as exc:
        print(f"NovaPinyin：{exc}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""凭据自检：扫描将被提交的文件，发现疑似真实密钥时以非零码退出。

用法：python tools/check_secrets.py
说明：只报告文件名与行号，不打印匹配到的明文。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", "build", ".build", ".codex-build", "__pycache__", ".vscode", ".idea"}
SKIP_SUFFIXES = {".jpg", ".png", ".gif", ".svg", ".pdf", ".zip", ".elf", ".bin", ".o", ".a"}
# 本地真值文件允许存在，但不应被提交（由 .gitignore 保证）
SKIP_NAMES = {"secrets.local.h"}

RULES = [
    ("OpenAI-style long token", re.compile(r"sk-[A-Za-z0-9]{24,}")),
    ("Google API key", re.compile(r"AIza[0-9A-Za-z_-]{24,}")),
    ("WeCom webhook with real key", re.compile(r"qyapi\.weixin\.qq\.com[^\s\"']*key=[A-Za-z0-9-]{20,}")),
    ("Private key block", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")),
    ("GitHub PAT", re.compile(r"gh[pousr]_[A-Za-z0-9]{30,}")),
    ("Non-empty WIFI_PASSWORD macro", re.compile(r'#define\s+WIFI_PASSWORD\s+"(?!")[^"]{4,}"')),
    ("Non-empty ARK_API_KEY macro", re.compile(r'#define\s+ARK_API_KEY\s+"(?!")[^"]{4,}"')),
]

ALLOWED_PLACEHOLDER = re.compile(r"YOUR_|PLACEHOLDER|EXAMPLE|xxxx|<", re.I)


def iter_files():
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS for part in rel.parts):
            continue
        if path.name in SKIP_NAMES or path.suffix.lower() in SKIP_SUFFIXES:
            continue
        # 模板文件本身允许出现占位说明
        if path.name.endswith(".example"):
            continue
        yield path, rel


def main() -> int:
    hits: list[tuple[str, int, str]] = []
    scanned = 0
    for path, rel in iter_files():
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        scanned += 1
        for lineno, line in enumerate(text.splitlines(), 1):
            if ALLOWED_PLACEHOLDER.search(line) and "sk-" not in line:
                continue
            for label, pattern in RULES:
                if pattern.search(line):
                    hits.append((str(rel), lineno, label))

    if hits:
        print(f"[FAIL] {len(hits)} suspicious line(s) in {scanned} scanned files:")
        for rel, lineno, label in hits:
            print(f"  {rel}:{lineno}  {label}")
        print("Do not commit real credentials. Fill secrets.local.h locally instead.")
        return 1
    print(f"[OK] {scanned} files scanned, no credential-like content found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

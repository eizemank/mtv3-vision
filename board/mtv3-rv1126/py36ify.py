#!/usr/bin/env python3
"""py36ify.py — подготовка дерева see_sharp к python из buildroot.

Запускается на ХОСТЕ (python >= 3.8), делает конвертированную КОПИЮ дерева:

    python3 py36ify.py SRC_DIR DST_DIR [--target 36|37]

--target 36 (default, buildroot 2018.02 / python 3.6):
    * все аннотации (аргументы, return, переменные) закавычиваются —
      `x: list[int] | None` становится `x: "list[int] | None"`; строки-аннотации
      не исполняются, поэтому 3.10-синтаксис внутри них безопасен;
    * строки `from __future__ import annotations` удаляются (3.6 их не знает);
    * dataclasses в 3.6 нет — нужен backport: на хосте
      `pip3 download dataclasses==0.8 --no-deps -d .`, распаковать dataclasses.py
      в корень DST_DIR (рядом с пакетом see_sharp).

--target 37 (если python на борту >= 3.7):
    * только добавляется `from __future__ import annotations` в каждый файл;
      dataclasses уже в stdlib.

Отчёт: список файлов, число правок, предупреждения о конструкциях,
которые скрипт не чинит (walrus :=, match, и т.п.).
"""

import ast
import re
import shutil
import sys
from pathlib import Path

FUTURE_RE = re.compile(rb"^from\s+__future__\s+import\s+annotations\s*(#.*)?$")


def annotation_spans(src_bytes):
    """(start, end) байтовые интервалы всех не-строковых аннотаций."""
    tree = ast.parse(src_bytes)
    # абсолютный байтовый офсет начала каждой строки
    line_start = [0]
    for line in src_bytes.splitlines(keepends=True):
        line_start.append(line_start[-1] + len(line))

    def off(lineno, col):
        return line_start[lineno - 1] + col

    spans = []
    for node in ast.walk(tree):
        anns = []
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            a = node.args
            for arg in (list(getattr(a, "posonlyargs", [])) + list(a.args)
                        + list(a.kwonlyargs) + [a.vararg, a.kwarg]):
                if arg is not None and arg.annotation is not None:
                    anns.append(arg.annotation)
            if node.returns is not None:
                anns.append(node.returns)
        elif isinstance(node, ast.AnnAssign):
            anns.append(node.annotation)
        for an in anns:
            if isinstance(an, ast.Constant) and isinstance(an.value, str):
                continue  # уже строка
            spans.append((off(an.lineno, an.col_offset),
                          off(an.end_lineno, an.end_col_offset)))
    return sorted(spans, reverse=True)


def quote_annotations(src_bytes):
    spans = annotation_spans(src_bytes)
    out = src_bytes
    for s, e in spans:
        txt = " ".join(out[s:e].decode("utf-8").split())  # многострочные -> в строку
        out = out[:s] + repr(txt).encode("utf-8") + out[e:]
    return out, len(spans)


def _is_future(line_bytes):
    return FUTURE_RE.match(line_bytes.strip()) is not None


def strip_future(src_bytes):
    kept, removed = [], 0
    for line in src_bytes.splitlines(keepends=True):
        if _is_future(line):
            removed += 1
            continue
        kept.append(line)
    return b"".join(kept), removed


def add_future(src_bytes):
    if any(_is_future(l) for l in src_bytes.splitlines()):
        return src_bytes, 0
    tree = ast.parse(src_bytes)
    ins_line = 1
    if (tree.body and isinstance(tree.body[0], ast.Expr)
            and isinstance(tree.body[0].value, ast.Constant)
            and isinstance(tree.body[0].value.value, str)):
        ins_line = tree.body[0].end_lineno + 1
    lines = src_bytes.splitlines(keepends=True)
    lines.insert(ins_line - 1, b"from __future__ import annotations\n")
    return b"".join(lines), 1


def warn_scan(src_bytes, path):
    warns = []
    tree = ast.parse(src_bytes)
    for node in ast.walk(tree):
        name = type(node).__name__
        if name == "NamedExpr":
            warns.append("walrus := (3.8) line %d" % node.lineno)
        elif name in ("Match", "MatchValue"):
            warns.append("match statement (3.10) line %d" % node.lineno)
    if b"dataclass" in src_bytes:
        warns.append("uses dataclasses — на 3.6 нужен backport (см. шапку скрипта)")
    return ["%s: %s" % (path, w) for w in warns]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    target = "37" if "--target" in sys.argv and "37" in sys.argv[sys.argv.index("--target") + 1] else "36"
    if len(args) != 2:
        print(__doc__)
        sys.exit(1)
    src_root, dst_root = Path(args[0]), Path(args[1])
    if not src_root.is_dir():
        sys.exit("no such dir: %s" % src_root)

    n_files = n_edits = 0
    warns = []
    dc_seen = False
    for p in src_root.rglob("*"):
        rel = p.relative_to(src_root)
        if "__pycache__" in rel.parts or p.suffix == ".pyc":
            continue
        d = dst_root / rel
        if p.is_dir():
            d.mkdir(parents=True, exist_ok=True)
            continue
        d.parent.mkdir(parents=True, exist_ok=True)
        if p.suffix != ".py":
            shutil.copy2(p, d)
            continue
        src = p.read_bytes()
        try:
            if target == "36":
                out, k1 = quote_annotations(src)
                out, k2 = strip_future(out)
            else:
                out, k1 = add_future(src)
                k2 = 0
            file_warns = warn_scan(src, rel)
        except SyntaxError as ex:
            sys.exit("parse error %s: %s" % (rel, ex))
        if any("dataclass" in w for w in file_warns):
            dc_seen = True
            file_warns = [w for w in file_warns if "dataclass" not in w]
        warns += file_warns
        d.write_bytes(out)
        n_files += 1
        n_edits += k1 + k2
        if k1 + k2:
            print("  %-60s %d правок" % (rel, k1 + k2))

    print("\nfiles: %d, edits: %d, target: py3.%s" % (n_files, n_edits, target))
    if target == "36" and dc_seen:
        print("ВНИМАНИЕ: dataclasses используются — положите backport dataclasses.py")
        print("  в %s (pip3 download dataclasses==0.8 --no-deps)" % dst_root)
    for w in warns:
        print("WARN:", w)
    if not warns:
        print("несовместимых конструкций сверх аннотаций не найдено")


if __name__ == "__main__":
    main()

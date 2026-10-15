#!/usr/bin/env python3
"""Emit host-link stubs for decompiled methods that do not have bodies yet."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MISSING = Path("/tmp/missing-syms.txt")
OUT = ROOT / "port" / "generated" / "link_stubs.cpp"

SKIP_EXACT = {
    "SCmdEqualityOp::Exec(ScriptContext&)",
    "ScriptCommand::ConvertTypes(ScriptContext&)",
}

SKIP_CLASSES = {
    "NuMemory",
    "NuMemoryManager",
    "NuMemoryPool",
    "NuConstStringManager",
    "NuCore",
    "OPTIONSSAVE",
    "cAttackManager",
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def strip_nested(body):
    prev = None
    while prev != body:
        prev = body
        body = re.sub(r"\{[^{}]*\}", " ", body)
    return body


def clean_decl(decl):
    decl = re.sub(r"\b(public|private|protected)\s*:", "", decl)
    decl = re.sub(r"\b(virtual|static|inline|explicit|friend)\b", "", decl)
    decl = decl.replace("override", "").replace("final", "")
    return " ".join(decl.split())


def method_name(decl):
    decl = clean_decl(decl)
    head = decl.split("(", 1)[0]
    ids = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", head)
    skip = {
        "const", "virtual", "static", "inline", "unsigned", "signed", "int", "void", "bool",
        "float", "double", "char", "short", "long", "ActionState", "uint32_t", "int32_t",
        "uint8_t", "int8_t", "uint16_t", "int16_t", "size_t", "volatile",
    }
    ids = [i for i in ids if i not in skip]
    return ids[-1] if ids else None


def return_expr(decl):
    decl = clean_decl(decl)
    head = decl.split("(", 1)[0]
    name = method_name(decl)
    if name:
        head = head[: head.rfind(name)]
    ret = " ".join(head.split())
    if ret in ("void",):
        return None
    if ret in ("ActionState",):
        return "ACTION_FINISHED"
    if ret in ("bool",):
        return "false"
    if ret in ("float", "double"):
        return "0"
    if "*" in ret:
        return "nullptr"
    if ret.startswith("const char"):
        return '""'
    return "0"


def main():
    classes = {}
    for header in (ROOT / "src").rglob("*.h"):
        text = strip_comments(header.read_text(errors="ignore"))
        for match in re.finditer(r"class\s+(\w+)([^;{]*)\{", text):
            name = match.group(1)
            start = match.end()
            depth = 1
            i = start
            while i < len(text) and depth:
                if text[i] == "{":
                    depth += 1
                elif text[i] == "}":
                    depth -= 1
                i += 1
            body = text[start : i - 1]
            ns = None
            prefix = text[max(0, match.start() - 80) : match.start()]
            ns_match = re.findall(r"namespace\s+(\w+)\s*\{", prefix)
            if ns_match:
                ns = ns_match[-1]
            classes.setdefault(name, []).append((header.relative_to(ROOT / "src").as_posix(), ns, body))

    missing = [line.strip() for line in MISSING.read_text().splitlines() if line.strip()]
    chunks = []
    includes = set()
    skipped = []
    seen = set()

    for sym in missing:
        head = sym.split("(", 1)[0]
        if "<" in head or sym in SKIP_EXACT or "::" not in sym:
            continue
        if not (sym.endswith(")") or sym.rstrip().endswith("const")):
            continue
        if sym.startswith("typeinfo ") or sym.startswith("vtable "):
            continue
        parts = head.split("::")
        if len(parts) < 2:
            continue
        method = parts[-1]
        cls = parts[-2]
        if cls in SKIP_CLASSES:
            continue
        qual_ns = parts[:-2]
        infos = classes.get(cls, [])
        found = None
        for header, ns, body in infos:
            cleaned = strip_nested(body)
            for piece in cleaned.split(";"):
                piece = " ".join(piece.split())
                if "(" not in piece or "= 0" in piece:
                    continue
                if method_name(piece) != method:
                    continue
                found = (header, ns, piece)
                break
            if found:
                break
        if not found:
            skipped.append(sym)
            continue
        header, ns, decl = found
        if qual_ns and ns and qual_ns[-1] != ns:
            skipped.append(sym + " (namespace mismatch)")
            continue
        key = (ns, cls, method, decl)
        if key in seen:
            continue
        seen.add(key)
        includes.add(header)
        sig = clean_decl(decl)
        name = method_name(sig)
        sig = sig.replace(name + "(", f"{cls}::{name}(", 1)
        if ns:
            sig = sig.replace(f"{cls}::{name}(", f"{ns}::{cls}::{name}(", 1)
        expr = return_expr(decl)
        if expr is None:
            body_txt = "{}"
        else:
            body_txt = "{ return " + expr + "; }"
        chunks.append(sig + " " + body_txt)

    lines = [
        "// Generated by tools/gen_host_stubs.py. Host link only.",
        "#include \"script/common/ScriptVarTypeEnum.h\"",
    ]
    for inc in sorted(includes):
        lines.append(f'#include "{inc}"')
    lines.append("")
    lines.extend(chunks)
    lines.append("")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines))
    print(f"wrote {len(chunks)} stubs, {len(includes)} headers, skipped {len(skipped)}")
    for item in skipped:
        print("SKIP", item)


if __name__ == "__main__":
    main()

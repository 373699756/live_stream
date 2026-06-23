#!/usr/bin/env python3
"""Check that repository C++ style policy files agree on 4-space indentation."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read_clang_format() -> dict[str, str]:
    config_path = ROOT / ".clang-format"
    values: dict[str, str] = {}
    for line in config_path.read_text(encoding="utf-8").splitlines():
        if ":" not in line or line.lstrip().startswith("#"):
            continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip()
    return values


def main() -> int:
    errors: list[str] = []
    clang_format = read_clang_format()
    expected_values = {
        "IndentWidth": "4",
        "ContinuationIndentWidth": "4",
        "ConstructorInitializerIndentWidth": "4",
        "TabWidth": "4",
        "UseTab": "Never",
    }
    for key, expected in expected_values.items():
        actual = clang_format.get(key)
        if actual != expected:
            errors.append(f".clang-format {key} is {actual!r}, expected {expected!r}")

    agents_text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    if "缩进使用 4 个空格" not in agents_text:
        errors.append("AGENTS.md does not state 4-space indentation")
    if "缩进使用 2 个空格" in agents_text:
        errors.append("AGENTS.md still contains the old 2-space indentation rule")

    if errors:
        print("[cpp-style-contract] failed")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("[cpp-style-contract] 4-space policy files are consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())

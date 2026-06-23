#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from datetime import datetime
from pathlib import Path


CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
CPP_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
WEB_SUFFIXES = {".ts", ".tsx", ".js", ".jsx", ".css"}
REPORTABLE_ROOTS = {"app", "libs", "configs", "www", "scripts"}
SEVERITY_ORDER = {"note": 0, "warning": 1, "error": 2}

CPPCHECK_XML_FILE = "cppcheck.xml"
SEMGREP_JSON_FILE = "semgrep.json"

CLANG_TIDY_CHECKS = ",".join(
    [
        "clang-analyzer-*",
        "bugprone-*",
        "performance-*",
        "portability-*",
        "readability-duplicate-include",
        "readability-redundant-control-flow",
        "readability-simplify-boolean-expr",
        "modernize-use-nullptr",
        "-bugprone-easily-swappable-parameters",
        "-bugprone-reserved-identifier",
    ]
)

CPPCHECK_DIAGNOSTIC_RE = re.compile(
    r"(^|.*/)(app|libs)/.*:[0-9]+:[0-9]+: "
    r"(error|warning|style|performance|portability):"
)
CPPCHECK_ERROR_RE = re.compile(
    r"^(?!.*\[cppcheckError\])(^|.*/)(app|libs)/.*:[0-9]+:[0-9]+: error:"
)
CLANG_TIDY_DIAGNOSTIC_RE = re.compile(
    r"(^|.*/)(app|libs)/.*:[0-9]+:[0-9]+: (error|warning):"
)
IWYU_FINDING_RE = re.compile(r" should add these lines:| should remove these lines:")
SCAN_BUILD_DIAGNOSTIC_RE = re.compile(r"scan-build: [1-9][0-9]* bugs? found")
BUILD_FAILURE_RE = re.compile(
    r"(^|\s)(error:|Error [0-9]+|错误|Bad system call|core dumped|核心已转储|"
    r"undefined reference|No such file)"
)
TRANSIENT_BUILD_RE = re.compile(
    r"Bad system call|错误的系统调用|core dumped|核心已转储"
)
TS_DIAGNOSTIC_RE = re.compile(r"error TS[0-9]+:")
CLANG_FORMAT_DIAGNOSTIC_RE = re.compile(r"error:|warning:")
CLANG_FORMAT_MESSAGE_RE = re.compile(
    r"^(?P<path>(?:.*/)?(?:app|libs)/[^:]+):"
    r"(?P<line>[0-9]+):(?P<column>[0-9]+): "
    r"(?P<level>error|warning): (?P<message>.*)$"
)
LIZARD_DIAGNOSTIC_RE = re.compile(r"!!!!|warning:|error:")
LIZARD_WARNING_RE = re.compile(
    r"^\s*(?P<nloc>[0-9]+)\s+(?P<ccn>[0-9]+)\s+"
    r"(?P<token>[0-9]+)\s+(?P<param>[0-9]+)\s+"
    r"(?P<length>[0-9]+)\s+(?P<symbol>.+)@"
    r"(?P<start>[0-9]+)-(?P<end>[0-9]+)@"
    r"(?P<path>(?:app|libs|www/src)/.+)$"
)
FLAWFINDER_DIAGNOSTIC_RE = re.compile(
    r"(^|.*/)(app|libs)/.*:[0-9]+:[0-9]+:"
)
FLAWFINDER_MESSAGE_RE = re.compile(
    r"^(?P<path>(?:.*/)?(?:app|libs)/[^:]+):"
    r"(?P<line>[0-9]+):(?P<column>[0-9]+):\s*(?P<message>.*)$"
)
SEMGREP_DIAGNOSTIC_RE = re.compile(r"(^|.*/)(app|libs|www/src)/.*:[0-9]+")
CLANG_TIDY_MESSAGE_RE = re.compile(
    r"^(?P<path>[^:\n]+):(?P<line>[0-9]+):(?P<column>[0-9]+): "
    r"(?P<level>error|warning): (?P<message>.*?)(?: \[(?P<rule>[^\]]+)\])?$"
)
KEYWORD_RE = re.compile(r"^(app|libs|configs|www)/.*:")
HOT_PATH_RE = re.compile(r"^(app|libs)/.*:")
BUILTIN_ERROR_RE = re.compile(r"^(app|libs|configs|www|scripts)/.*:[0-9]+: ERROR: ")
BUILTIN_REVIEW_RE = re.compile(
    r"^(app|libs|configs|www|scripts)/.*:[0-9]+: (ERROR|WARN|REVIEW): "
)

CPP_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
HEADER_IFNDEF_RE = re.compile(r"^\s*#\s*ifndef\s+([A-Za-z0-9_]+)")
HEADER_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z0-9_]+)")
CPP_FORBIDDEN_LANGUAGE_RE = re.compile(
    r"\b(throw|try|catch|dynamic_cast|typeid)\b"
)
DEPENDENCIES_MEMBER_RE = re.compile(
    r"\b[A-Za-z][A-Za-z0-9]*Dependencies\s+(dependencies_|[A-Za-z_][A-Za-z0-9_]*_)\b"
)
USING_NAMESPACE_RE = re.compile(r"\busing\s+namespace\s+[A-Za-z_:]+")
FRONTEND_FETCH_RE = re.compile(r"\bfetch\s*\(")
FRONTEND_STORAGE_RE = re.compile(r"\b(window\.)?(localStorage|sessionStorage)\b")
FRONTEND_COOKIE_RE = re.compile(r"\bdocument\.cookie\b")
NAMING_TEXT_SUFFIXES = CPP_SUFFIXES | WEB_SUFFIXES | {
    ".css",
    ".json",
    ".md",
    ".mk",
    ".py",
    ".sh",
    ".yml",
    ".yaml",
}
NAMING_FORBIDDEN_LEGACY_RE = re.compile(
    r"\b(MetaRtc|metaRTC|Yang|BackendName|AiBackendName|"
    r"MediaFlvStartData|GetFlvStartData|MediaFlvStartDataUnref|"
    r"PreviewModeState|buildPreviewModeState|previewPlaybackProtocols|"
    r"aiAlertStatus)\b"
)
NAMING_BROAD_WORD_RE = re.compile(
    r"\b[A-Za-z0-9_]*(Runtime|Manager|Store|Topology)[A-Za-z0-9_]*\b"
)
NAMING_DELETED_DOC_RE = re.compile(
    r"docs/refactor/(重构|重构_2|协议|Media重构|AI|net|命名扫描)\.md"
)
CPP_FILE_NAME_RE = re.compile(r"^[a-z0-9_]+\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$")
CSS_FILE_NAME_RE = re.compile(r"^[a-z0-9-]+\.css$")


def finding_identity_value(finding: dict[str, object]) -> str:
    return "\t".join(
        [
            str(finding.get("tool", "")),
            str(finding.get("rule_id", "")),
            str(finding.get("path", "")),
            str(finding.get("line", "")),
            str(finding.get("message", "")),
        ]
    )


class QualityScan:
    def __init__(
        self,
        root_dir: Path,
        mode: str,
        scope: str,
        base_ref: str,
        baseline_path: Path | None = None,
        write_baseline_path: Path | None = None,
        fail_on_new: str = "warning",
    ) -> None:
        self.root_dir = root_dir
        self.mode = mode
        self.scope = scope
        self.base_ref = base_ref
        self.baseline_path = baseline_path
        self.write_baseline_path = write_baseline_path
        self.fail_on_new = self.normalize_finding_level(fail_on_new)
        self.timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        self.report_root = self.root_dir / "scripts" / "scan" / "reports" / "quality"
        self.report_dir = self.report_root / self.timestamp
        self.quality_report_file = self.report_root / "quality_report.md"
        self.findings_file = self.report_dir / "findings.json"
        self.latest_findings_file = self.report_root / "quality_findings.json"
        self.sarif_file = self.report_dir / "findings.sarif"
        self.latest_sarif_file = self.report_root / "quality_findings.sarif"
        self.baseline_diff_file = self.report_dir / "baseline-diff.json"
        self.clang_analysis_db_dir = self.report_dir / "clang-analysis-db"
        self.clang_analysis_db_ready = False
        self.changed_paths_resolved = False
        self.changed_paths: set[Path] | None = None

        self.cross_prefix = os.environ.get("CROSS_PREFIX", "arm-himix200-linux-")
        self.cross_cc = os.environ.get("CROSS_CC", f"{self.cross_prefix}gcc")
        self.cross_cxx = os.environ.get("CROSS_CXX", f"{self.cross_prefix}g++")
        self.cross_ar = os.environ.get("CROSS_AR", f"{self.cross_prefix}ar")
        self.cross_size = os.environ.get("CROSS_SIZE", f"{self.cross_prefix}size")
        self.cppcheck_platform = os.environ.get("CPPCHECK_PLATFORM", "unix32")
        self.cross_cc_from_env = "CROSS_CC" in os.environ
        self.cross_cxx_from_env = "CROSS_CXX" in os.environ
        self.cross_ar_from_env = "CROSS_AR" in os.environ
        self.cross_size_from_env = "CROSS_SIZE" in os.environ
        self.cross_toolchain_resolved = False

        self.failed_steps: list[str] = []
        self.skipped_steps: list[str] = []
        self.warning_steps: list[str] = []
        self.findings: list[dict[str, object]] = []
        self.finding_keys: set[tuple[str, str, str, int, str]] = set()
        self.baseline_checked = False
        self.baseline_error = ""
        self.baseline_total = 0
        self.baseline_new_findings: list[dict[str, object]] = []
        self.baseline_blocking_findings: list[dict[str, object]] = []

    def log(self, message: str) -> None:
        print(f"[quality-scan] {message}")

    def record_failure(self, step: str) -> None:
        if step not in self.failed_steps:
            self.failed_steps.append(step)

    def record_skipped(self, step: str) -> None:
        if step not in self.skipped_steps:
            self.skipped_steps.append(step)

    def record_warning(self, step: str) -> None:
        if step not in self.warning_steps:
            self.warning_steps.append(step)

    def have_tool(self, tool: str) -> bool:
        return shutil.which(tool) is not None

    def find_first_tool(self, tools: list[str]) -> str | None:
        for tool in tools:
            if self.have_tool(tool):
                return tool
        return None

    def resolve_tool_path(self, configured_tool: str) -> str | None:
        resolved = shutil.which(configured_tool)
        if resolved is None:
            return None
        return str(Path(resolved).resolve())

    def resolve_cross_toolchain(self) -> bool:
        if self.cross_toolchain_resolved:
            return True

        resolved_cc = self.resolve_tool_path(self.cross_cc)
        if resolved_cc is None:
            self.log(f"missing cross C compiler: {self.cross_cc}")
            return False
        self.cross_cc = resolved_cc
        if not self.cross_cc.endswith("gcc"):
            self.log(f"cannot derive CROSS_COMPILE prefix from {self.cross_cc}")
            return False
        self.cross_prefix = self.cross_cc[: -len("gcc")]

        if not self.cross_cxx_from_env:
            self.cross_cxx = f"{self.cross_prefix}g++"
        if not self.cross_ar_from_env:
            self.cross_ar = f"{self.cross_prefix}ar"
        if not self.cross_size_from_env:
            self.cross_size = f"{self.cross_prefix}size"

        resolved_cxx = self.resolve_tool_path(self.cross_cxx)
        resolved_ar = self.resolve_tool_path(self.cross_ar)
        if resolved_cxx is None:
            self.log(f"missing cross C++ compiler: {self.cross_cxx}")
            return False
        if resolved_ar is None:
            self.log(f"missing cross archiver: {self.cross_ar}")
            return False
        self.cross_cxx = resolved_cxx
        self.cross_ar = resolved_ar

        resolved_size = self.resolve_tool_path(self.cross_size)
        self.cross_size = resolved_size or ""
        self.cross_toolchain_resolved = True
        return True

    def command_text(self, cmd: list[str]) -> str:
        return shlex.join(str(part) for part in cmd)

    def log_path(self, log_file: str) -> Path:
        return self.report_dir / log_file

    def normalize_rel_path(self, path: Path | str) -> Path:
        rel_path = Path(path)
        if rel_path.is_absolute():
            rel_path = rel_path.relative_to(self.root_dir)
        return rel_path

    def tool_rel_path(self, path_text: str | None) -> Path | None:
        if not path_text:
            return None
        path = Path(path_text)
        try:
            if path.is_absolute():
                return path.resolve(strict=False).relative_to(
                    self.root_dir.resolve(strict=False)
                )
            candidate = (self.root_dir / path).resolve(strict=False)
            return candidate.relative_to(self.root_dir.resolve(strict=False))
        except (OSError, ValueError):
            if path.parts and path.parts[0] in REPORTABLE_ROOTS:
                return path
        return None

    def is_reportable_rel_path(self, rel_path: Path) -> bool:
        return (
            bool(rel_path.parts)
            and rel_path.parts[0] in REPORTABLE_ROOTS
            and not self.is_ignored_rel_path(rel_path)
        )

    def is_production_source(self, path_text: str | None) -> bool:
        rel_path = self.tool_rel_path(path_text)
        if rel_path is None:
            return False
        return (
            bool(rel_path.parts)
            and rel_path.parts[0] in {"app", "libs"}
            and rel_path.suffix in CPP_SOURCE_SUFFIXES
            and not self.is_ignored_rel_path(rel_path)
            and (self.root_dir / rel_path).is_file()
        )

    def normalize_finding_level(self, level: str) -> str:
        normalized = level.lower()
        if normalized in {"error", "fatal"}:
            return "error"
        if normalized in {"warning", "warn", "style", "performance", "portability"}:
            return "warning"
        return "note"

    def add_tool_finding(
        self,
        tool: str,
        rule_id: str,
        level: str,
        rel_path: Path | str,
        line_number: int,
        message: str,
        column: int | None = None,
    ) -> bool:
        rel = self.normalize_rel_path(rel_path)
        if not self.is_reportable_rel_path(rel):
            return False
        line = max(1, int(line_number or 1))
        normalized_level = self.normalize_finding_level(level)
        normalized_rule = rule_id or "unknown"
        normalized_message = message.strip() or normalized_rule
        key = (tool, normalized_rule, str(rel), line, normalized_message)
        if key in self.finding_keys:
            return False
        self.finding_keys.add(key)
        finding: dict[str, object] = {
            "tool": tool,
            "rule_id": normalized_rule,
            "level": normalized_level,
            "path": str(rel),
            "line": line,
            "message": normalized_message,
        }
        if column:
            finding["column"] = int(column)
        self.findings.append(finding)
        return True

    def path_in_scope(self, rel_path: Path | str) -> bool:
        if self.scope == "all":
            return True
        changed_paths = self.get_changed_paths()
        if changed_paths is None:
            return True
        rel = self.normalize_rel_path(rel_path)
        return rel in changed_paths

    def git_changed_paths(self, diff_filter: str) -> set[Path] | None:
        if not self.have_tool("git"):
            return None
        cmd = [
            "git",
            "-C",
            str(self.root_dir),
            "diff",
            "--name-only",
            f"--diff-filter={diff_filter}",
            self.base_ref,
        ]
        completed = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            return None
        paths: set[Path] = set()
        for line in completed.stdout.splitlines():
            if line:
                paths.add(Path(line))
        return paths

    def git_untracked_paths(self) -> set[Path]:
        if not self.have_tool("git"):
            return set()
        completed = subprocess.run(
            ["git", "-C", str(self.root_dir), "ls-files", "--others", "--exclude-standard"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            return set()
        return {Path(line) for line in completed.stdout.splitlines() if line}

    def get_changed_paths(self) -> set[Path] | None:
        if self.changed_paths_resolved:
            return self.changed_paths
        self.changed_paths_resolved = True
        changed = self.git_changed_paths("ACMR")
        if changed is None:
            self.record_warning(
                f"changed scope: cannot resolve git diff against {self.base_ref}; scanning all files"
            )
            self.changed_paths = None
            return None
        changed.update(self.git_untracked_paths())
        filtered: set[Path] = set()
        for rel_path in changed:
            if self.is_ignored_rel_path(rel_path):
                continue
            if (self.root_dir / rel_path).is_file():
                filtered.add(rel_path)
        self.changed_paths = filtered
        return self.changed_paths

    def run_command(
        self,
        name: str,
        log_file: str,
        cmd: list[str],
        cwd: Path | None = None,
        on_failure: str = "failure",
        append: bool = False,
        env: dict[str, str | None] | None = None,
        timeout_seconds: int | None = None,
    ) -> int:
        cwd = cwd or self.root_dir
        self.log(f"running {name}")
        log_path = self.log_path(log_file)
        command_env = None
        if env is not None:
            command_env = os.environ.copy()
            for key, value in env.items():
                if value is None:
                    command_env.pop(key, None)
                else:
                    command_env[key] = value
        mode = "a" if append else "w"
        with log_path.open(mode, encoding="utf-8", errors="replace") as handle:
            handle.write(f"$ {self.command_text(cmd)}\n\n")
            try:
                completed = subprocess.run(
                    cmd,
                    cwd=str(cwd),
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    env=command_env,
                    timeout=timeout_seconds,
                    check=False,
                )
                status = completed.returncode
            except FileNotFoundError:
                status = 127
                handle.write(f"\ncommand not found: {cmd[0]}\n")
            except subprocess.TimeoutExpired:
                status = 124
                handle.write(
                    f"\ncommand timed out after {timeout_seconds} seconds\n"
                )

        if status != 0:
            self.log(f"{name} failed with exit code {status}")
            if on_failure == "failure":
                self.record_failure(name)
            elif on_failure == "warning":
                self.record_warning(f"{name}: exit code {status}")
        return status

    def run_optional_step(
        self, tool: str, name: str, log_file: str, cmd: list[str]
    ) -> int | None:
        if not self.have_tool(tool):
            self.log(f"skipping {name}: missing {tool}")
            self.record_skipped(f"{name}: missing {tool}")
            return None
        return self.run_command(name, log_file, cmd)

    def run_warning_step(
        self, tool: str, name: str, log_file: str, cmd: list[str]
    ) -> int | None:
        if not self.have_tool(tool):
            self.log(f"skipping {name}: missing {tool}")
            self.record_skipped(f"{name}: missing {tool}")
            return None
        return self.run_command(name, log_file, cmd, on_failure="warning")

    def run_sequence(
        self,
        name: str,
        log_file: str,
        commands: list[list[str]],
        on_failure: str = "failure",
    ) -> int:
        status = 0
        for index, cmd in enumerate(commands):
            status = self.run_command(
                name,
                log_file,
                cmd,
                on_failure="ignore",
                append=index > 0,
            )
            if status != 0:
                self.log(f"{name} failed with exit code {status}")
                if on_failure == "failure":
                    self.record_failure(name)
                elif on_failure == "warning":
                    self.record_warning(f"{name}: exit code {status}")
                return status
        return status

    def log_contains(self, log_file: str, pattern: re.Pattern[str]) -> bool:
        path = self.log_path(log_file)
        if not path.exists():
            return False
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            return any(pattern.search(line) for line in handle)

    def count_matches(self, log_file: str, pattern: re.Pattern[str]) -> int:
        path = self.log_path(log_file)
        if not path.exists():
            return 0
        count = 0
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if pattern.search(line):
                    count += 1
        return count

    def fail_if_log_matches(
        self, name: str, log_file: str, pattern: re.Pattern[str], description: str
    ) -> None:
        count = self.count_matches(log_file, pattern)
        if count > 0:
            self.log(f"{name} has {count} {description}")
            self.record_failure(name)

    def is_ignored_rel_path(self, rel: Path) -> bool:
        parts = rel.parts
        ignored_parts = {
            ".git",
            "__pycache__",
            "build",
            "dist",
            "node_modules",
            "out",
            "reports",
            "tests",
        }
        return any(part in ignored_parts for part in parts)

    def iter_existing_bases(self, base_names: tuple[str, ...]) -> list[Path]:
        bases: list[Path] = []
        for base_name in base_names:
            base = self.root_dir / base_name
            if base.exists():
                bases.append(base)
        return bases

    def project_files(
        self,
        base_names: tuple[str, ...],
        suffixes: set[str],
        include_tests: bool = False,
    ) -> list[Path]:
        files: list[Path] = []
        for base in self.iter_existing_bases(base_names):
            for path in base.rglob("*"):
                if not path.is_file() or path.suffix not in suffixes:
                    continue
                rel = path.relative_to(self.root_dir)
                if self.is_ignored_rel_path(rel):
                    if include_tests and "tests" in rel.parts:
                        pass
                    else:
                        continue
                files.append(rel)
        return sorted(files)

    def production_cpp_files(self, scoped: bool = False) -> list[str]:
        paths = self.project_files(("app", "libs"), CPP_SUFFIXES)
        if scoped:
            paths = [path for path in paths if self.path_in_scope(path)]
        return [str(path) for path in paths]

    def production_cpp_paths(self, scoped: bool = False) -> list[Path]:
        paths = self.project_files(("app", "libs"), CPP_SUFFIXES)
        if scoped:
            paths = [path for path in paths if self.path_in_scope(path)]
        return paths

    def production_header_paths(self, scoped: bool = False) -> list[Path]:
        paths = self.project_files(("app", "libs"), HEADER_SUFFIXES)
        if scoped:
            paths = [path for path in paths if self.path_in_scope(path)]
        return paths

    def frontend_paths(self, scoped: bool = False) -> list[Path]:
        paths = self.project_files(("www/src",), WEB_SUFFIXES)
        if scoped:
            paths = [path for path in paths if self.path_in_scope(path)]
        return paths

    def naming_scan_paths(self, scoped: bool = False) -> list[Path]:
        paths: list[Path] = []
        for base in self.iter_existing_bases(
            ("app", "libs", "configs", "www/src", "docs", "scripts")
        ):
            for path in base.rglob("*"):
                if not path.is_file():
                    continue
                rel = path.relative_to(self.root_dir)
                if self.is_ignored_rel_path(rel):
                    continue
                if path.suffix and path.suffix not in NAMING_TEXT_SUFFIXES:
                    continue
                if scoped and not self.path_in_scope(rel):
                    continue
                paths.append(rel)
        return sorted(paths)

    def config_json_paths(self, scoped: bool = False) -> list[Path]:
        paths = self.project_files(("configs",), {".json"})
        if scoped:
            paths = [path for path in paths if self.path_in_scope(path)]
        return paths

    def read_text(self, rel_path: Path) -> str:
        return (self.root_dir / rel_path).read_text(
            encoding="utf-8", errors="replace"
        )

    def read_lines(self, rel_path: Path) -> list[str]:
        return self.read_text(rel_path).splitlines()

    def read_colon_config(self, rel_path: Path) -> dict[str, str]:
        values: dict[str, str] = {}
        path = self.root_dir / rel_path
        if not path.exists():
            return values
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                stripped = line.strip()
                if not stripped or stripped.startswith("#") or ":" not in stripped:
                    continue
                key, value = stripped.split(":", 1)
                values[key.strip()] = value.strip().strip("\"'")
        return values

    def run_format_config_step(self) -> None:
        issues: list[str] = []
        lines = [
            "Format indentation policy:",
            "- C/C++: clang-format IndentWidth=4, TabWidth=4, UseTab=Never",
            "- Web: prettier tabWidth=4, useTabs=false",
            "",
        ]

        clang_format = self.root_dir / ".clang-format"
        if not clang_format.exists():
            issues.append(".clang-format is missing")
        else:
            config = self.read_colon_config(Path(".clang-format"))
            expected = {
                "IndentWidth": "4",
                "ContinuationIndentWidth": "4",
                "TabWidth": "4",
                "UseTab": "Never",
            }
            for key, expected_value in expected.items():
                actual_value = config.get(key)
                if actual_value != expected_value:
                    issues.append(
                        f".clang-format {key} must be {expected_value}, "
                        f"got {actual_value or '<missing>'}"
                    )

        if (self.root_dir / "www").is_dir():
            prettier_config = self.root_dir / "www" / ".prettierrc.json"
            if not prettier_config.exists():
                issues.append("www/.prettierrc.json is missing")
            else:
                try:
                    with prettier_config.open("r", encoding="utf-8") as handle:
                        config = json.load(handle)
                except (OSError, json.JSONDecodeError) as exc:
                    issues.append(f"www/.prettierrc.json cannot be parsed: {exc}")
                else:
                    if config.get("tabWidth") != 4:
                        issues.append(
                            "www/.prettierrc.json tabWidth must be 4, "
                            f"got {config.get('tabWidth', '<missing>')}"
                        )
                    if config.get("useTabs") is not False:
                        issues.append(
                            "www/.prettierrc.json useTabs must be false, "
                            f"got {config.get('useTabs', '<missing>')}"
                        )

        if issues:
            lines.append("Result: failed")
            lines.extend(f"- {issue}" for issue in issues)
            self.record_failure("format config")
        else:
            lines.append("Result: passed")
        self.log_path("format-config.log").write_text(
            "\n".join(lines) + "\n", encoding="utf-8"
        )

    def cppcheck_paths(self) -> list[str]:
        paths: list[str] = []
        if (self.root_dir / "app").exists():
            paths.append("app")
        libs_dir = self.root_dir / "libs"
        if libs_dir.exists():
            for module_dir in sorted(libs_dir.iterdir()):
                if not module_dir.is_dir():
                    continue
                for child in ("include", "src"):
                    candidate = module_dir / child
                    if candidate.exists():
                        paths.append(str(candidate.relative_to(self.root_dir)))
        return paths

    def run_build_step(self) -> None:
        status = self.run_command(
            "make", "make.log", ["make", "-j2"], on_failure="ignore"
        )
        if status == 0:
            return

        self.log(f"make -j2 failed with exit code {status}")
        if not self.log_contains("make.log", TRANSIENT_BUILD_RE):
            self.record_failure("make")
            return

        self.log("parallel build hit transient runtime fault; rerunning make -j1")
        self.record_warning(
            "make: parallel build hit transient runtime fault; serial fallback used"
        )
        status = self.run_command(
            "make serial fallback",
            "make-serial.log",
            ["make", "-j1"],
            on_failure="ignore",
        )
        if status != 0:
            self.log(f"make -j1 failed with exit code {status}")
            self.record_failure("make")

    def run_clang_format_step(self) -> None:
        if not self.have_tool("clang-format"):
            self.log("skipping clang-format: missing clang-format")
            self.record_skipped("clang-format: missing clang-format")
            return
        source_files = self.production_cpp_files(scoped=self.scope == "changed")
        if not source_files:
            self.record_skipped(
                f"clang-format: no production C/C++ files found for scope {self.scope}"
            )
            return
        status = self.run_command(
            "clang-format",
            "clang-format.log",
            ["clang-format", "--dry-run", "--Werror", *source_files],
            on_failure="ignore",
        )
        finding_count = self.parse_clang_format_log()
        if status != 0:
            if self.baseline_path is None:
                self.record_failure("clang-format")
            elif finding_count == 0:
                self.record_failure("clang-format")

    def package_has_script(self, script_name: str) -> bool:
        package_file = self.root_dir / "www" / "package.json"
        if not package_file.exists():
            return False
        try:
            with package_file.open("r", encoding="utf-8") as handle:
                package = json.load(handle)
        except (OSError, json.JSONDecodeError):
            return False
        scripts = package.get("scripts")
        return isinstance(scripts, dict) and bool(scripts.get(script_name))

    def run_npm_script_if_present(
        self, script_name: str, name: str, log_file: str
    ) -> None:
        package_file = self.root_dir / "www" / "package.json"
        if not package_file.exists():
            self.record_skipped(f"{name}: missing www/package.json")
            return
        if not self.have_tool("npm"):
            self.log(f"skipping {name}: missing npm")
            self.record_skipped(f"{name}: missing npm")
            return
        if not self.package_has_script(script_name):
            self.log(f"skipping {name}: missing npm script {script_name}")
            self.record_skipped(f"{name}: missing npm script {script_name}")
            return
        self.run_command(
            name,
            log_file,
            ["npm", "run", script_name],
            cwd=self.root_dir / "www",
        )

    def run_frontend_build_step(self) -> None:
        if not (self.root_dir / "www").is_dir():
            self.record_skipped("www build: missing www directory")
            return
        if not self.have_tool("npm"):
            self.log("skipping www build: missing npm")
            self.record_skipped("www build: missing npm")
            return
        self.run_command(
            "www build",
            "www-build.log",
            ["npm", "run", "build"],
            cwd=self.root_dir / "www",
        )

    def run_frontend_typecheck_step(self) -> None:
        package_file = self.root_dir / "www" / "package.json"
        if not package_file.exists():
            self.record_skipped("www typecheck: missing www/package.json")
            return
        if self.package_has_script("typecheck"):
            self.run_npm_script_if_present(
                "typecheck", "www typecheck", "www-typecheck.log"
            )
            return
        local_tsc = self.root_dir / "www" / "node_modules" / ".bin" / "tsc"
        if not local_tsc.exists():
            self.log("skipping www typecheck: missing npm script typecheck and local tsc")
            self.record_skipped(
                "www typecheck: missing npm script typecheck and local tsc"
            )
            return
        self.run_command(
            "www typecheck",
            "www-typecheck.log",
            [str(local_tsc), "--noEmit"],
            cwd=self.root_dir / "www",
        )

    def run_keyword_scan(self) -> None:
        if not self.have_tool("rg"):
            self.log("skipping keyword scan: missing rg")
            self.record_skipped("keyword scan: missing rg")
            return
        self.run_command(
            "keyword scan",
            "keyword-scan.log",
            [
                "rg",
                "-n",
                "--glob",
                "!**/tests/**",
                "--glob",
                "!www/package-lock.json",
                "--glob",
                "!www/public/vendor/**",
                "TODO|FIXME|XXX|HACK|sleep_for|usleep|malloc|free|new |delete |"
                "memcpy|strcpy|sprintf|pthread|recursive_mutex|detach",
                "app",
                "libs",
                "configs",
                "www/src",
            ],
            on_failure="ignore",
        )
        if not self.log_path("keyword-scan.log").exists():
            return
        if not self.log_contains("keyword-scan.log", KEYWORD_RE):
            with self.log_path("keyword-scan.log").open("a", encoding="utf-8") as handle:
                handle.write("\n(no matches)\n")

    def run_hot_path_log_scan(self) -> None:
        if not self.have_tool("rg"):
            self.log("skipping hot path log scan: missing rg")
            self.record_skipped("hot path log scan: missing rg")
            return
        self.run_command(
            "hot path log scan",
            "hot-path-log-scan.log",
            [
                "rg",
                "-n",
                "--glob",
                "!**/tests/**",
                "PublishFrame|OnFrame|EncodedFrame|Encode|WriteFrame|SendFrame|"
                "Send\\(|Push|memcpy|malloc|free|usleep|sleep_for",
                "app",
                "libs",
            ],
            on_failure="ignore",
        )
        if not self.log_contains("hot-path-log-scan.log", HOT_PATH_RE):
            with self.log_path("hot-path-log-scan.log").open(
                "a", encoding="utf-8"
            ) as handle:
                handle.write("\n(no matches)\n")

    def run_cppcheck_step(self) -> None:
        paths = self.cppcheck_paths()
        if not paths:
            self.record_skipped("cppcheck: no production C/C++ paths found")
            return
        xml_path = self.log_path(CPPCHECK_XML_FILE)
        build_dir = self.report_dir / "cppcheck-build"
        build_dir.mkdir(parents=True, exist_ok=True)
        cmd = [
            "cppcheck",
            "--enable=warning,performance,portability,style",
            "--std=c++17",
            "--inline-suppr",
            "--library=posix",
            f"--platform={self.cppcheck_platform}",
            "--suppress=missingIncludeSystem",
            "--xml",
            "--xml-version=2",
            f"--output-file={xml_path}",
            f"--cppcheck-build-dir={build_dir}",
        ]
        if self.mode == "full":
            cmd.append("--inconclusive")
        compile_db = self.root_dir / "compile_commands.json"
        project_db = None
        if compile_db.exists():
            if self.prepare_clang_analysis_database():
                project_db = self.clang_analysis_db_dir / "compile_commands.json"
            else:
                self.record_warning(
                    "cppcheck: failed to prepare filtered compile database; falling back to paths"
                )
        if project_db is not None and project_db.exists():
            cmd.append(f"--project={project_db}")
        else:
            cmd.extend(paths)
        status = self.run_optional_step(
            "cppcheck",
            "cppcheck",
            "cppcheck.log",
            cmd,
        )
        if status is not None:
            self.parse_cppcheck_xml()

    def run_lizard_step(self) -> None:
        if not self.have_tool("lizard"):
            self.log("skipping lizard: missing lizard")
            self.record_skipped("lizard: missing lizard")
            return
        status = self.run_command(
            "lizard",
            "lizard.log",
            ["lizard", "-C", "20", "-L", "120", "-a", "8", "app", "libs", "www/src"],
            on_failure="ignore",
        )
        finding_count = self.parse_lizard_log()
        if status != 0:
            if self.baseline_path is None:
                self.record_failure("lizard")
            elif finding_count == 0:
                self.record_failure("lizard")

    def run_flawfinder_step(self) -> None:
        if not self.have_tool("flawfinder"):
            self.log("skipping flawfinder: missing flawfinder")
            self.record_skipped("flawfinder: missing flawfinder")
            return
        status = self.run_command(
            "flawfinder",
            "flawfinder.log",
            [
                "flawfinder",
                "--quiet",
                "--columns",
                "--minlevel=2",
                "--error-level=2",
                "app",
                "libs",
            ],
            on_failure="ignore",
        )
        finding_count = self.parse_flawfinder_log()
        if status != 0:
            if self.baseline_path is None:
                self.record_failure("flawfinder")
            elif finding_count == 0:
                self.record_failure("flawfinder")

    def find_semgrep_config(self) -> Path | None:
        for rel_path in (
            ".semgrep.yml",
            ".semgrep.yaml",
            "semgrep.yml",
            "semgrep.yaml",
            ".semgrep/rules",
            "scripts/scan/quality_semgrep.yml",
        ):
            path = self.root_dir / rel_path
            if path.exists():
                return path
        return None

    def run_semgrep_step(self) -> None:
        if not self.have_tool("semgrep"):
            self.log("skipping semgrep: missing semgrep")
            self.record_skipped("semgrep: missing semgrep")
            return
        config_path = self.find_semgrep_config()
        if config_path is None:
            self.log("skipping semgrep: missing local semgrep config")
            self.record_skipped("semgrep: missing local semgrep config")
            return
        semgrep_state_dir = self.report_dir / "semgrep-state"
        semgrep_state_dir.mkdir(parents=True, exist_ok=True)
        status = self.run_command(
            "semgrep",
            "semgrep.log",
            [
                "semgrep",
                "--config",
                str(config_path),
                "--json",
                "--output",
                str(self.log_path(SEMGREP_JSON_FILE)),
                "--metrics=off",
                "--error",
                "app",
                "libs",
                "www/src",
            ],
            on_failure="ignore",
            env={
                "SEMGREP_SETTINGS_FILE": str(semgrep_state_dir / "settings.yml"),
                "SEMGREP_LOG_FILE": str(semgrep_state_dir / "semgrep.log"),
                "SEMGREP_SEND_METRICS": "off",
                "XDG_CACHE_HOME": str(semgrep_state_dir / "cache"),
                "XDG_CONFIG_HOME": str(semgrep_state_dir / "config"),
                "HTTP_PROXY": None,
                "HTTPS_PROXY": None,
                "ALL_PROXY": None,
                "http_proxy": None,
                "https_proxy": None,
                "all_proxy": None,
            },
            timeout_seconds=300,
        )
        if status not in (0, 1):
            self.record_failure("semgrep")
        self.parse_semgrep_json()

    def write_builtin_findings(
        self,
        name: str,
        log_file: str,
        findings: list[str],
        fail_on_error: bool = True,
    ) -> None:
        path = self.log_path(log_file)
        with path.open("w", encoding="utf-8") as handle:
            if findings:
                handle.write("\n".join(findings))
                handle.write("\n")
            else:
                handle.write("(no findings)\n")
        error_count = sum(1 for finding in findings if ": ERROR:" in finding)
        if error_count > 0:
            self.log(f"{name} has {error_count} errors")
            if fail_on_error:
                self.record_failure(name)

    def append_normalized_findings(
        self, log_file: str, title: str, lines: list[str]
    ) -> None:
        with self.log_path(log_file).open("a", encoding="utf-8") as handle:
            handle.write(f"\n## {title}\n")
            if lines:
                handle.write("\n".join(lines))
                handle.write("\n")
            else:
                handle.write("(no normalized findings)\n")

    def parse_clang_format_log(self) -> int:
        path = self.log_path("clang-format.log")
        if not path.exists():
            return 0
        count = 0
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = CLANG_FORMAT_MESSAGE_RE.match(line)
            if not match:
                continue
            rel_path = self.tool_rel_path(match.group("path"))
            if rel_path is None or not self.is_reportable_rel_path(rel_path):
                continue
            if self.add_tool_finding(
                "clang-format",
                "clang-format-violations",
                match.group("level"),
                rel_path,
                int(match.group("line")),
                match.group("message"),
                int(match.group("column")),
            ):
                count += 1
        return count

    def parse_cppcheck_xml(self) -> None:
        xml_path = self.log_path(CPPCHECK_XML_FILE)
        normalized_lines: list[str] = []
        if not xml_path.exists():
            self.record_warning("cppcheck: missing XML output")
            return
        try:
            root = ET.parse(xml_path).getroot()
        except ET.ParseError as error:
            self.record_warning(f"cppcheck: invalid XML output: {error}")
            return

        error_count = 0
        for error_node in root.findall(".//error"):
            severity = error_node.get("severity", "warning")
            rule_id = error_node.get("id", "cppcheck")
            message = error_node.get("msg") or error_node.get("verbose") or rule_id
            if rule_id == "cppcheckError":
                self.record_warning(f"cppcheck internal analysis error: {message}")
                continue
            locations = error_node.findall("location")
            location = None
            for candidate in locations:
                rel = self.tool_rel_path(candidate.get("file"))
                if rel is not None and self.is_reportable_rel_path(rel):
                    location = candidate
                    break
            if location is None:
                continue
            rel_path = self.tool_rel_path(location.get("file"))
            if rel_path is None:
                continue
            line_number = int(location.get("line") or 1)
            column = int(location.get("column") or 1)
            self.add_tool_finding(
                "cppcheck", rule_id, severity, rel_path, line_number, message, column
            )
            normalized_lines.append(
                f"{rel_path}:{line_number}:{column}: {severity}: "
                f"{message} [{rule_id}]"
            )
            if severity == "error":
                error_count += 1

        self.append_normalized_findings(
            "cppcheck.log", "Normalized cppcheck findings", normalized_lines
        )
        if error_count:
            self.log(f"cppcheck has {error_count} error findings")
            if self.baseline_path is None:
                self.record_failure("cppcheck")

    def parse_clang_tidy_log(self) -> None:
        path = self.log_path("clang-tidy.log")
        if not path.exists():
            return
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = CLANG_TIDY_MESSAGE_RE.match(line)
            if not match:
                continue
            rel_path = self.tool_rel_path(match.group("path"))
            if rel_path is None or not self.is_reportable_rel_path(rel_path):
                continue
            rule_id = match.group("rule") or "clang-tidy"
            self.add_tool_finding(
                "clang-tidy",
                rule_id,
                match.group("level"),
                rel_path,
                int(match.group("line")),
                match.group("message"),
                int(match.group("column")),
            )

    def parse_flawfinder_log(self) -> int:
        path = self.log_path("flawfinder.log")
        if not path.exists():
            return 0
        count = 0
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = FLAWFINDER_MESSAGE_RE.match(line)
            if not match:
                continue
            rel_path = self.tool_rel_path(match.group("path"))
            if rel_path is None or not self.is_reportable_rel_path(rel_path):
                continue
            message = match.group("message").strip()
            rule_match = re.search(r"\(([^)]+)\)", message)
            rule_id = rule_match.group(1) if rule_match else "flawfinder"
            if self.add_tool_finding(
                "flawfinder",
                rule_id,
                "warning",
                rel_path,
                int(match.group("line")),
                message,
                int(match.group("column")),
            ):
                count += 1
        return count

    def parse_lizard_log(self) -> int:
        path = self.log_path("lizard.log")
        if not path.exists():
            return 0
        count = 0
        in_warning_section = False
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("!!!! Warnings "):
                in_warning_section = True
                continue
            if not in_warning_section:
                continue
            match = LIZARD_WARNING_RE.match(line)
            if not match:
                continue
            rel_path = self.tool_rel_path(match.group("path"))
            if rel_path is None or not self.is_reportable_rel_path(rel_path):
                continue
            message = (
                "lizard threshold exceeded: "
                f"ccn={match.group('ccn')}, "
                f"nloc={match.group('nloc')}, "
                f"length={match.group('length')}, "
                f"params={match.group('param')}, "
                f"symbol={match.group('symbol')}"
            )
            if self.add_tool_finding(
                "lizard",
                "complexity-threshold",
                "warning",
                rel_path,
                int(match.group("start")),
                message,
            ):
                count += 1
        return count

    def parse_semgrep_json(self) -> None:
        json_path = self.log_path(SEMGREP_JSON_FILE)
        normalized_lines: list[str] = []
        if not json_path.exists():
            self.record_warning("semgrep: missing JSON output")
            return
        try:
            payload = json.loads(json_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            self.record_warning(f"semgrep: invalid JSON output: {error}")
            return

        error_count = 0
        for result in payload.get("results", []):
            rel_path = self.tool_rel_path(result.get("path"))
            if rel_path is None or not self.is_reportable_rel_path(rel_path):
                continue
            extra = result.get("extra") if isinstance(result.get("extra"), dict) else {}
            start = result.get("start") if isinstance(result.get("start"), dict) else {}
            rule_id = str(result.get("check_id") or "semgrep")
            severity = str(extra.get("severity") or "WARNING")
            message = str(extra.get("message") or rule_id)
            line_number = int(start.get("line") or 1)
            column = int(start.get("col") or 1)
            self.add_tool_finding(
                "semgrep", rule_id, severity, rel_path, line_number, message, column
            )
            normalized_lines.append(
                f"{rel_path}:{line_number}:{column}: {severity.lower()}: "
                f"{message} [{rule_id}]"
            )
            if self.normalize_finding_level(severity) == "error":
                error_count += 1

        self.append_normalized_findings(
            "semgrep.log", "Normalized semgrep findings", normalized_lines
        )
        if error_count:
            self.log(f"semgrep has {error_count} error findings")
            self.record_failure("semgrep")

    def add_finding(
        self,
        findings: list[str],
        rel_path: Path | str,
        line_number: int,
        level: str,
        message: str,
        rule_id: str = "builtin",
        scoped: bool = True,
    ) -> None:
        rel = self.normalize_rel_path(rel_path)
        if scoped and not self.path_in_scope(rel):
            return
        if self.add_tool_finding(
            "quality_scan", rule_id, level, rel, line_number, message
        ):
            findings.append(f"{rel}:{line_number}: {level}: {message}")

    def run_config_json_scan(self) -> None:
        findings: list[str] = []
        paths = self.config_json_paths(scoped=self.scope == "changed")
        if not paths:
            self.record_skipped("config json scan: no configs/*.json files found")
            return
        for rel_path in paths:
            try:
                with (self.root_dir / rel_path).open("r", encoding="utf-8") as handle:
                    json.load(handle)
            except json.JSONDecodeError as error:
                self.add_finding(
                    findings,
                    rel_path,
                    error.lineno,
                    "ERROR",
                    f"invalid JSON: {error.msg}",
                    "config-json-valid",
                )
            except OSError as error:
                self.add_finding(
                    findings,
                    rel_path,
                    1,
                    "ERROR",
                    f"cannot read config: {error}",
                    "config-json-readable",
                )
        self.write_builtin_findings("config json scan", "config-json-scan.log", findings)

    def run_header_contract_scan(self) -> None:
        findings: list[str] = []
        for rel_path in self.production_header_paths(scoped=self.scope == "changed"):
            lines = self.read_lines(rel_path)
            non_empty = [
                (index + 1, line.strip())
                for index, line in enumerate(lines[:20])
                if line.strip()
            ]
            if any(line == "#pragma once" for _, line in non_empty):
                self.add_finding(
                    findings,
                    rel_path,
                    1,
                    "ERROR",
                    "use project include guards instead of #pragma once",
                    "header-guard-style",
                )
                continue

            ifndef = None
            define = None
            for line_number, line in non_empty:
                if ifndef is None:
                    match = HEADER_IFNDEF_RE.match(line)
                    if match:
                        ifndef = (line_number, match.group(1))
                        continue
                if define is None:
                    match = HEADER_DEFINE_RE.match(line)
                    if match:
                        define = (line_number, match.group(1))
                if ifndef is not None and define is not None:
                    break

            if ifndef is None or define is None:
                self.add_finding(
                    findings,
                    rel_path,
                    1,
                    "ERROR",
                    "missing include guard",
                    "header-guard-present",
                )
                continue
            if ifndef[1] != define[1]:
                self.add_finding(
                    findings,
                    rel_path,
                    define[0],
                    "ERROR",
                    f"include guard define {define[1]} does not match {ifndef[1]}",
                    "header-guard-match",
                )
            if not ifndef[1].startswith("LIVE_STREAM_") or not ifndef[1].endswith("_H_"):
                self.add_finding(
                    findings,
                    rel_path,
                    ifndef[0],
                    "ERROR",
                    "include guard must use LIVE_STREAM_*_H_ form",
                    "header-guard-name",
                )
        self.write_builtin_findings(
            "header contract scan", "header-contract-scan.log", findings
        )

    def run_cpp_contract_scan(self) -> None:
        findings: list[str] = []
        for rel_path in self.production_cpp_paths(scoped=self.scope == "changed"):
            for line_number, line in enumerate(self.read_lines(rel_path), start=1):
                if CPP_FORBIDDEN_LANGUAGE_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "ERROR",
                        "C++ exceptions and RTTI are disabled by project convention",
                        "cpp-no-exceptions-rtti",
                    )
                if DEPENDENCIES_MEMBER_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "ERROR",
                        "*Dependencies should be unpacked into semantic members",
                        "cpp-dependencies-dto-member",
                    )
                if USING_NAMESPACE_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "WARN",
                        "avoid using namespace in production C++ files",
                        "cpp-using-namespace",
                    )
        self.write_builtin_findings("cpp contract scan", "cpp-contract-scan.log", findings)

    def run_boundary_scan(self) -> None:
        findings: list[str] = []
        for rel_path in self.production_cpp_paths(scoped=self.scope == "changed"):
            if not rel_path.parts:
                continue
            for line_number, line in enumerate(self.read_lines(rel_path), start=1):
                match = CPP_INCLUDE_RE.match(line)
                if not match:
                    continue
                include = match.group(1)
                if include.startswith("../") or "/../" in include:
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "ERROR",
                        f"relative parent include crosses module boundary: {include}",
                        "boundary-parent-include",
                    )
                if rel_path.parts[0] == "libs":
                    if include.startswith(("app/", "runtime/", "subsystems/", "platform/")):
                        self.add_finding(
                            findings,
                            rel_path,
                            line_number,
                            "ERROR",
                            f"libs must not include app-owned header: {include}",
                            "boundary-libs-to-app",
                        )
                    if include.startswith("www/"):
                        self.add_finding(
                            findings,
                            rel_path,
                            line_number,
                            "ERROR",
                            f"libs must not include web-owned header: {include}",
                            "boundary-libs-to-web",
                        )
        self.write_builtin_findings("boundary scan", "boundary-scan.log", findings)

    def module_has_sources(self, module_dir: Path) -> bool:
        for child_name in ("include", "src"):
            child = module_dir / child_name
            if not child.exists():
                continue
            for path in child.rglob("*"):
                if path.is_file() and path.suffix in CPP_SUFFIXES:
                    return True
        return False

    def run_module_structure_scan(self) -> None:
        findings: list[str] = []
        libs_dir = self.root_dir / "libs"
        if not libs_dir.exists():
            self.record_skipped("module structure scan: missing libs directory")
            return
        for module_dir in sorted(path for path in libs_dir.iterdir() if path.is_dir()):
            rel_module = module_dir.relative_to(self.root_dir)
            if self.scope == "changed":
                changed_paths = self.get_changed_paths()
                if changed_paths is not None and not any(
                    path.parts[:2] == rel_module.parts for path in changed_paths
                ):
                    continue
            if not self.module_has_sources(module_dir):
                continue
            for child_name in ("Makefile", "module.mk", "include", "src"):
                if not (module_dir / child_name).exists():
                    self.add_finding(
                        findings,
                        rel_module / child_name,
                        1,
                        "ERROR",
                        f"module with source files must provide {child_name}",
                        "module-required-layout",
                        scoped=False,
                    )
            makefile = module_dir / "Makefile"
            if makefile.exists():
                text = makefile.read_text(encoding="utf-8", errors="replace")
                if "include ../module_rules.mk" not in text:
                    self.add_finding(
                        findings,
                        rel_module / "Makefile",
                        1,
                        "ERROR",
                        "module Makefile must reuse libs/module_rules.mk",
                        "module-rules-reuse",
                        scoped=False,
                    )
            module_mk = module_dir / "module.mk"
            if module_mk.exists():
                text = module_mk.read_text(encoding="utf-8", errors="replace")
                if "ADD_MODULE_LIBRARY" not in text:
                    self.add_finding(
                        findings,
                        rel_module / "module.mk",
                        1,
                        "ERROR",
                        "module.mk must register ADD_MODULE_LIBRARY",
                        "module-register-library",
                        scoped=False,
                    )
        self.write_builtin_findings(
            "module structure scan", "module-structure-scan.log", findings
        )

    def run_frontend_contract_scan(self) -> None:
        findings: list[str] = []
        for rel_path in self.frontend_paths(scoped=self.scope == "changed"):
            in_api_dir = (
                len(rel_path.parts) >= 3
                and rel_path.parts[:2] == ("www", "src")
                and rel_path.parts[2] == "api"
            )
            for line_number, line in enumerate(self.read_lines(rel_path), start=1):
                if not in_api_dir and FRONTEND_FETCH_RE.search(line):
                    level = "ERROR" if re.search(r"['\"`]/api", line) else "WARN"
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        level,
                        "frontend direct fetch should stay behind www/src/api helpers when calling API routes",
                        "frontend-api-helper",
                    )
                if FRONTEND_COOKIE_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "WARN",
                        "manual document.cookie access needs auth/security review",
                        "frontend-cookie-access",
                    )
                if FRONTEND_STORAGE_RE.search(line) and rel_path != Path(
                    "www/src/api/authSession.ts"
                ):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "WARN",
                        "browser storage should stay behind auth/session helpers",
                        "frontend-storage-access",
                    )
        self.write_builtin_findings(
            "frontend contract scan", "frontend-contract-scan.log", findings
        )

    def is_naming_allowed_line(self, rel_path: Path, line: str, token: str) -> bool:
        if "docs/refactor/README.md" == str(rel_path):
            return True
        if "quality_baseline.json" == rel_path.name:
            return True
        if "device_service" in line and (
            rel_path.parts[:2] == ("libs", "onvif") or rel_path.parts[0] == "configs"
        ):
            return True
        if token in {"Stored", "kZipMethodStored", "ReadStoreOnlyZipEntries"}:
            return True
        if "Stored" in token:
            return True
        if "Stored" in token and rel_path.parts[:2] == ("libs", "infra"):
            return True
        return False

    def check_naming_file_name(self, rel_path: Path, findings: list[str]) -> None:
        name = rel_path.name
        suffix = rel_path.suffix
        if suffix in CPP_SUFFIXES and not CPP_FILE_NAME_RE.match(name):
            self.add_finding(
                findings,
                rel_path,
                1,
                "ERROR",
                "C/C++ file names must be lower_snake_case",
                "naming-cpp-file-name",
                scoped=False,
            )
        if suffix == ".css" and not CSS_FILE_NAME_RE.match(name):
            self.add_finding(
                findings,
                rel_path,
                1,
                "ERROR",
                "CSS file names must use lower kebab-case",
                "naming-css-file-name",
                scoped=False,
            )
        stem = rel_path.stem
        lower_stem = stem.lower()
        if suffix in CPP_SUFFIXES | WEB_SUFFIXES and (
            lower_stem.endswith("_utils")
            or lower_stem.endswith("utils")
            or lower_stem.endswith("_runtime")
            or lower_stem.endswith("runtime")
            or "manager" in lower_stem
            or "store" in lower_stem
            or "topology" in lower_stem
        ):
            self.add_finding(
                findings,
                rel_path,
                1,
                "REVIEW",
                "file name uses a broad naming term; prefer a concrete business role",
                "naming-broad-file-name",
                scoped=False,
            )

    def run_naming_scan(self) -> None:
        findings: list[str] = []
        for rel_path in self.naming_scan_paths(scoped=self.scope == "changed"):
            if rel_path == Path("scripts/scan/quality_baseline.json"):
                continue
            self.check_naming_file_name(rel_path, findings)
            inside_quality_scan_naming_rule = False
            for line_number, line in enumerate(self.read_lines(rel_path), start=1):
                if rel_path == Path("scripts/scan/quality_scan.py"):
                    if line.startswith("NAMING_"):
                        inside_quality_scan_naming_rule = line.rstrip().endswith("(")
                        continue
                    if inside_quality_scan_naming_rule:
                        if line.strip() == ")":
                            inside_quality_scan_naming_rule = False
                        continue
                if NAMING_DELETED_DOC_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "ERROR",
                        "reference points at a deleted refactor source document; use docs/refactor/README.md",
                        "naming-deleted-refactor-doc",
                    )
                if NAMING_FORBIDDEN_LEGACY_RE.search(line):
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "ERROR",
                        "legacy or explicitly banned name remains",
                        "naming-forbidden-legacy",
                    )
                for match in NAMING_BROAD_WORD_RE.finditer(line):
                    token = match.group(0)
                    if self.is_naming_allowed_line(rel_path, line, token):
                        continue
                    self.add_finding(
                        findings,
                        rel_path,
                        line_number,
                        "REVIEW",
                        f"broad naming term `{token}` should be replaced with a concrete role",
                        "naming-broad-term",
                    )
        self.write_builtin_findings(
            "naming scan", "naming-scan.log", findings, fail_on_error=False
        )

    def run_secret_material_scan(self) -> None:
        findings: list[str] = []
        secret_name_re = re.compile(
            r"(private[_-]?key|secret|token|password|passwd|credential)", re.IGNORECASE
        )
        pem_private_re = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
        for base in self.iter_existing_bases(("configs", "app", "libs", "www/src", "scripts")):
            for path in base.rglob("*"):
                if not path.is_file():
                    continue
                rel_path = path.relative_to(self.root_dir)
                if self.is_ignored_rel_path(rel_path):
                    continue
                if not self.path_in_scope(rel_path):
                    continue
                if secret_name_re.search(path.name):
                    self.add_finding(
                        findings,
                        rel_path,
                        1,
                        "WARN",
                        "file name looks like secret material; verify it is not production-sensitive",
                        "secret-file-name",
                    )
                if path.suffix not in {".pem", ".key", ".json", ".h", ".cpp", ".ts", ".tsx"}:
                    continue
                try:
                    text = path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                for line_number, line in enumerate(text.splitlines(), start=1):
                    if pem_private_re.search(line):
                        self.add_finding(
                            findings,
                            rel_path,
                            line_number,
                            "WARN",
                            "private key material is present in repository",
                            "secret-private-key",
                        )
        self.write_builtin_findings(
            "secret material scan",
            "secret-material-scan.log",
            findings,
            fail_on_error=False,
        )

    def run_product_scope_scan(self) -> None:
        findings: list[str] = []
        scope_re = re.compile(r"\b(audio|recording|playback|storage replay)\b", re.IGNORECASE)
        allowed_phrases = (
            "audio is not supported",
            "never calls audio APIs",
            "not link the real VoiceEngine",
        )
        for rel_path in [
            *self.production_cpp_paths(scoped=self.scope == "changed"),
            *self.frontend_paths(scoped=self.scope == "changed"),
            *self.config_json_paths(scoped=self.scope == "changed"),
        ]:
            for line_number, line in enumerate(self.read_lines(rel_path), start=1):
                if not scope_re.search(line):
                    continue
                if any(phrase in line for phrase in allowed_phrases):
                    continue
                self.add_finding(
                    findings,
                    rel_path,
                    line_number,
                    "REVIEW",
                    "product scope excludes audio, recording and storage playback features",
                    "product-scope",
                )
        self.write_builtin_findings(
            "product scope scan",
            "product-scope-scan.log",
            findings,
            fail_on_error=False,
        )

    def run_hot_path_risk_scan(self) -> None:
        findings: list[str] = []
        hot_path_modules = {
            "device_media",
            "http",
            "http_media",
            "media_pipeline",
            "media_source",
            "net",
            "rtp",
            "rtsp",
            "webrtc",
        }
        hot_context_re = re.compile(
            r"(Frame|Packet|Stream|Session|Sink|Client|Queue|Send|Write|Publish|OnFrame)"
        )
        risk_re = re.compile(
            r"\b(Trace|Debug|Info|Warn|Error)\s*\(|"
            r"\b(std::)?(vector|string|deque|map|unordered_map)<|"
            r"\b(resize|reserve|push_back|append|assign|memcpy|malloc|free|new|delete)\s*\("
        )
        for rel_path in self.production_cpp_paths(scoped=self.scope == "changed"):
            if len(rel_path.parts) < 3 or rel_path.parts[0] != "libs":
                continue
            if rel_path.parts[1] not in hot_path_modules:
                continue
            file_text = self.read_text(rel_path)
            if not hot_context_re.search(file_text):
                continue
            for line_number, line in enumerate(file_text.splitlines(), start=1):
                if not risk_re.search(line):
                    continue
                self.add_finding(
                    findings,
                    rel_path,
                    line_number,
                    "REVIEW",
                    "hot path candidate: verify logging/allocation/copy is bounded",
                    "hot-path-risk",
                )
        self.write_builtin_findings(
            "hot path risk scan",
            "hot-path-risk-scan.log",
            findings,
            fail_on_error=False,
        )

    def run_builtin_scans(self) -> None:
        if self.scope == "changed":
            changed_paths = self.get_changed_paths()
            if changed_paths is not None:
                self.log(f"changed scope has {len(changed_paths)} files")
        self.run_config_json_scan()
        self.run_header_contract_scan()
        self.run_cpp_contract_scan()
        self.run_boundary_scan()
        self.run_module_structure_scan()
        self.run_naming_scan()
        self.run_frontend_contract_scan()
        self.run_secret_material_scan()
        self.run_product_scope_scan()
        self.run_hot_path_risk_scan()

    def run_binary_info_command(self, name: str, cmd: list[str]) -> None:
        status = self.run_command(
            f"binary info {name}",
            "binary-info.txt",
            cmd,
            on_failure="warning",
            append=True,
        )
        if status != 0:
            with self.log_path("binary-info.txt").open("a", encoding="utf-8") as handle:
                handle.write(f"\n[{name} failed with exit code {status}]\n")

    def run_binary_info_step(self) -> None:
        binary = self.root_dir / "build" / "bin" / "live_stream"
        if not binary.exists():
            self.record_skipped("binary info: missing build/bin/live_stream")
            return

        self.log_path("binary-info.txt").write_text("", encoding="utf-8")
        if self.have_tool("file"):
            self.run_binary_info_command("file", ["file", "build/bin/live_stream"])
        else:
            self.record_warning("binary info file: missing file")

        if self.resolve_cross_toolchain() and self.cross_size:
            self.run_binary_info_command("size", [self.cross_size, "build/bin/live_stream"])
        elif self.have_tool("size"):
            self.run_binary_info_command("size", ["size", "build/bin/live_stream"])
        else:
            self.record_warning("binary info size: missing size")

        if self.have_tool("readelf"):
            self.run_binary_info_command("readelf", ["readelf", "-h", "build/bin/live_stream"])
        else:
            self.record_warning("binary info readelf: missing readelf")

        if self.have_tool("nm"):
            self.run_binary_info_command(
                "nm", ["nm", "-S", "--size-sort", "build/bin/live_stream"]
            )
        else:
            self.record_warning("binary info nm: missing nm")

    def write_command_output(self, log_file: str, cmd: list[str]) -> None:
        self.run_command(
            " ".join(cmd[:2]),
            log_file,
            cmd,
            on_failure="ignore",
        )

    def append_tool_status(self) -> None:
        tools = [
            "rg",
            "git",
            "make",
            "arm-himix200-linux-gcc",
            "arm-himix200-linux-g++",
            "arm-himix200-linux-size",
            "gcc",
            "g++",
            "clang",
            "clang++",
            "clang-tidy",
            "clang-format",
            "cppcheck",
            "scan-build",
            "/usr/lib/llvm-18/bin/scan-build",
            "/usr/lib/llvm-17/bin/scan-build",
            "/usr/lib/llvm-16/bin/scan-build",
            "/usr/lib/llvm-15/bin/scan-build",
            "/usr/lib/llvm-14/bin/scan-build",
            "scan-build-18",
            "scan-build-17",
            "scan-build-16",
            "scan-build-15",
            "scan-build-14",
            "scan-build-13",
            "scan-build-12",
            "scan-build-11",
            "scan-build-10",
            "bear",
            "cloc",
            "tokei",
            "lizard",
            "flawfinder",
            "semgrep",
            "perf",
            "valgrind",
            "strace",
            "ltrace",
            "gdb",
            "gdb-multiarch",
            "readelf",
            "objdump",
            "nm",
            "size",
            "addr2line",
            "file",
            "node",
            "npm",
            "npx",
            "include-what-you-use",
            "iwyu",
            "iwyu_tool",
        ]
        with self.log_path("tool-status.txt").open("w", encoding="utf-8") as handle:
            for tool in tools:
                resolved = shutil.which(tool)
                if resolved:
                    handle.write(f"yes {tool} {resolved}\n")
                else:
                    handle.write(f"no {tool}\n")
            local_tsc = self.root_dir / "www" / "node_modules" / ".bin" / "tsc"
            handle.write(
                f"{'yes' if local_tsc.exists() else 'no'} www-local-tsc {local_tsc}\n"
            )

    def find_clang_resource_dir(self) -> str:
        clang_tool = self.find_first_tool(["clang++", "clang"])
        if clang_tool:
            try:
                completed = subprocess.run(
                    [clang_tool, "-print-resource-dir"],
                    cwd=str(self.root_dir),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    check=False,
                )
                resource_dir = completed.stdout.strip()
                if resource_dir and (Path(resource_dir) / "include" / "stddef.h").exists():
                    return resource_dir
            except OSError:
                pass

        for pattern in (
            "/usr/lib/llvm-*/lib/clang/*",
            "/usr/lib/clang/*",
            "/usr/include/clang/*",
        ):
            for path in sorted(Path("/").glob(pattern.lstrip("/"))):
                if (path / "include" / "stddef.h").exists():
                    return str(path)
        return ""

    def sanitize_compile_command(
        self, args: list[str], directory: str, source_file: str, clang_cxx: str
    ) -> list[str]:
        remove_exact = {"-Werror"}
        remove_prefixes = (
            "-mcpu=",
            "-mfloat-abi=",
            "-mfpu=",
            "-march=",
            "-mtune=",
            "--target=",
            "-target=",
            "--sysroot=",
            "-isysroot",
            "-Wreserved-user-defined-literal",
        )
        remove_with_value = {
            "--target",
            "-target",
            "--sysroot",
            "-isysroot",
            "-gcc-toolchain",
            "--gcc-toolchain",
        }

        def absolute_path(path: str) -> str:
            if os.path.isabs(path):
                return path
            return os.path.normpath(os.path.join(directory, path))

        sanitized = [clang_cxx, "-Wno-reserved-user-defined-literal"]
        index = 1
        while index < len(args):
            arg = args[index]
            if arg in remove_exact or arg.startswith(remove_prefixes):
                index += 1
                continue
            if arg in remove_with_value:
                index += 2
                continue
            if arg == "-I" and index + 1 < len(args):
                sanitized.extend([arg, absolute_path(args[index + 1])])
                index += 2
                continue
            if arg.startswith("-I") and len(arg) > 2:
                sanitized.append("-I" + absolute_path(arg[2:]))
                index += 1
                continue
            if arg in ("-isystem", "-iquote", "-include", "-o") and index + 1 < len(args):
                sanitized.extend([arg, absolute_path(args[index + 1])])
                index += 2
                continue
            if arg.startswith(("-isystem", "-iquote")) and len(arg) > len("-isystem"):
                option = "-isystem" if arg.startswith("-isystem") else "-iquote"
                sanitized.append(option + absolute_path(arg[len(option) :]))
                index += 1
                continue
            if arg == source_file or (
                not arg.startswith("-") and arg.endswith((".c", ".cc", ".cpp", ".cxx"))
            ):
                sanitized.append(absolute_path(arg))
                index += 1
                continue
            sanitized.append(arg)
            index += 1
        return sanitized

    def prepare_clang_analysis_database(self) -> bool:
        if self.clang_analysis_db_ready:
            return True
        source_db = self.root_dir / "compile_commands.json"
        output_db = self.clang_analysis_db_dir / "compile_commands.json"
        source_list = self.clang_analysis_db_dir / "sources.txt"
        if not source_db.exists():
            self.log(f"missing compile database: {source_db}")
            return False

        clang_cxx = self.find_first_tool(["clang++", "clang"])
        if clang_cxx is None:
            self.log("missing clang++ or clang for clang analysis database")
            return False
        clang_cxx_path = shutil.which(clang_cxx) or clang_cxx

        self.clang_analysis_db_dir.mkdir(parents=True, exist_ok=True)
        log_path = self.log_path("clang-analysis-db.log")
        try:
            with source_db.open("r", encoding="utf-8") as handle:
                entries = json.load(handle)
        except (OSError, json.JSONDecodeError) as error:
            log_path.write_text(f"failed to read compile database: {error}\n", encoding="utf-8")
            return False

        sanitized_entries = []
        for entry in entries:
            directory = entry.get("directory", str(self.root_dir))
            args = entry.get("arguments")
            if args is None and entry.get("command"):
                args = shlex.split(entry["command"])
            if not args:
                continue
            source_file = entry.get("file", "")
            source_abs = source_file
            if source_file and not os.path.isabs(source_file):
                source_abs = os.path.normpath(os.path.join(directory, source_file))
            if not self.is_production_source(source_abs):
                continue
            sanitized_entry = dict(entry)
            sanitized_entry["arguments"] = self.sanitize_compile_command(
                args, directory, source_file, clang_cxx_path
            )
            sanitized_entry.pop("command", None)
            if source_abs:
                sanitized_entry["file"] = source_abs
            sanitized_entries.append(sanitized_entry)

        output_db.write_text(
            json.dumps(sanitized_entries, indent=2) + "\n",
            encoding="utf-8",
        )
        with source_list.open("w", encoding="utf-8") as handle:
            for entry in sanitized_entries:
                source = entry.get("file")
                if source:
                    handle.write(f"{source}\n")
        log_path.write_text(
            f"wrote {len(sanitized_entries)} entries to {output_db}\n",
            encoding="utf-8",
        )
        if not sanitized_entries:
            with log_path.open("a", encoding="utf-8") as handle:
                handle.write("no compile commands found\n")
            return False
        self.clang_analysis_db_ready = True
        return True

    def run_compile_database_step(self) -> None:
        if not self.have_tool("bear"):
            self.record_skipped("compile database: missing bear")
            return
        if not self.resolve_cross_toolchain():
            self.record_failure("compile database: missing cross toolchain")
            return
        status = self.run_sequence(
            "compile database",
            "bear.log",
            [
                ["make", "clean"],
                [
                    "bear",
                    "--",
                    "make",
                    "-j2",
                    "build/bin/live_stream",
                    f"CROSS_COMPILE={self.cross_prefix}",
                ],
            ],
        )
        if status == 0 and not (self.root_dir / "compile_commands.json").exists():
            self.record_failure("compile database")
            with self.log_path("bear.log").open("a", encoding="utf-8") as handle:
                handle.write("\ncompile_commands.json was not generated\n")

    def run_scan_build_step(self) -> None:
        scan_build_tool = self.find_first_tool(
            [
                "scan-build",
                "/usr/lib/llvm-18/bin/scan-build",
                "/usr/lib/llvm-17/bin/scan-build",
                "/usr/lib/llvm-16/bin/scan-build",
                "/usr/lib/llvm-15/bin/scan-build",
                "/usr/lib/llvm-14/bin/scan-build",
                "scan-build-18",
                "scan-build-17",
                "scan-build-16",
                "scan-build-15",
                "scan-build-14",
                "scan-build-13",
                "scan-build-12",
                "scan-build-11",
                "scan-build-10",
            ]
        )
        if scan_build_tool is None:
            self.record_skipped("scan-build: missing scan-build or scan-build-10..18")
            return
        if not self.resolve_cross_toolchain():
            self.record_warning("scan-build: missing cross toolchain")
            return
        self.run_sequence(
            "scan-build",
            "scan-build.log",
            [
                ["make", "clean"],
                [
                    scan_build_tool,
                    "--use-cc",
                    self.cross_cc,
                    "--use-c++",
                    self.cross_cxx,
                    "make",
                    "-j2",
                    "build/bin/live_stream",
                    f"CROSS_COMPILE={self.cross_prefix}",
                ],
            ],
            on_failure="warning",
        )
        self.fail_if_log_matches(
            "scan-build", "scan-build.log", SCAN_BUILD_DIAGNOSTIC_RE, "diagnostics"
        )

    def run_clang_tidy_step(self) -> None:
        if not self.have_tool("clang-tidy"):
            self.record_skipped("clang-tidy: missing clang-tidy")
            return
        if not (self.root_dir / "compile_commands.json").exists():
            self.record_skipped("clang-tidy: missing compile_commands.json")
            return
        if not self.prepare_clang_analysis_database():
            self.record_failure("clang-tidy: failed to prepare clang analysis database")
            return
        source_list = self.clang_analysis_db_dir / "sources.txt"
        sources = sorted(
            line.strip()
            for line in source_list.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
        if not sources:
            self.record_skipped("clang-tidy: no production .cpp files found")
            return
        self.run_command(
            "clang-tidy",
            "clang-tidy.log",
            [
                "clang-tidy",
                f"--checks={CLANG_TIDY_CHECKS}",
                "--header-filter=.*(/app|/libs)/.*",
                "--quiet",
                "--use-color=false",
                "-p",
                str(self.clang_analysis_db_dir),
                *sources,
            ],
        )
        self.parse_clang_tidy_log()
        self.fail_if_log_matches(
            "clang-tidy", "clang-tidy.log", CLANG_TIDY_DIAGNOSTIC_RE, "diagnostics"
        )

    def run_iwyu_step(self) -> None:
        if not (self.root_dir / "compile_commands.json").exists():
            self.record_skipped("include-what-you-use: missing compile_commands.json")
            return
        if not self.have_tool("iwyu_tool"):
            if self.have_tool("include-what-you-use") or self.have_tool("iwyu"):
                self.record_skipped("include-what-you-use: missing iwyu_tool")
            else:
                self.record_skipped(
                    "include-what-you-use: missing include-what-you-use or iwyu"
                )
            return
        if not self.prepare_clang_analysis_database():
            self.record_warning(
                "include-what-you-use: failed to prepare clang analysis database"
            )
            return
        cmd = ["iwyu_tool", "-p", str(self.clang_analysis_db_dir), "app", "libs", "--"]
        resource_dir = self.find_clang_resource_dir()
        if resource_dir:
            cmd.append(f"-resource-dir={resource_dir}")
        cmd.extend(["-Xiwyu", "--cxx17ns"])
        self.run_command(
            "include-what-you-use",
            "iwyu.log",
            cmd,
            on_failure="warning",
        )
        self.fail_if_log_matches(
            "include-what-you-use", "iwyu.log", IWYU_FINDING_RE, "findings"
        )

    def step_log_file(self, step: str) -> str:
        mapping = {
            "make": "make.log",
            "www build": "www-build.log",
            "www typecheck": "www-typecheck.log",
            "www lint": "www-lint.log",
            "www test": "www-test.log",
            "www format check": "www-format-check.log",
            "http/web contract": "http-web-contract.log",
            "cpp style contract": "cpp-style-contract.log",
            "clang-format": "clang-format.log",
            "config json scan": "config-json-scan.log",
            "header contract scan": "header-contract-scan.log",
            "cpp contract scan": "cpp-contract-scan.log",
            "boundary scan": "boundary-scan.log",
            "module structure scan": "module-structure-scan.log",
            "frontend contract scan": "frontend-contract-scan.log",
            "secret material scan": "secret-material-scan.log",
            "product scope scan": "product-scope-scan.log",
            "hot path risk scan": "hot-path-risk-scan.log",
            "cppcheck": "cppcheck.log",
            "lizard": "lizard.log",
            "flawfinder": "flawfinder.log",
            "semgrep": "semgrep.log",
            "quality baseline": "baseline-diff.json",
            "compile database": "bear.log",
            "scan-build": "scan-build.log",
            "clang-tidy": "clang-tidy.log",
            "include-what-you-use": "iwyu.log",
        }
        if step.startswith("scan-build"):
            return "scan-build.log"
        return mapping.get(step, "quality_report.md")

    def first_matches(
        self, log_file: str, pattern: re.Pattern[str], limit: int
    ) -> list[str]:
        path = self.log_path(log_file)
        if not path.exists():
            return []
        matches: list[str] = []
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if pattern.search(line):
                    matches.append(line.rstrip("\n"))
                    if len(matches) >= limit:
                        break
        return matches

    def top_files(self, log_file: str, pattern: re.Pattern[str], limit: int) -> list[str]:
        path = self.log_path(log_file)
        if not path.exists():
            return []
        counts: Counter[str] = Counter()
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if not pattern.search(line):
                    continue
                file_name = line.split(":", 1)[0]
                counts[file_name] += 1
        return [f"{count:7d} {file_name}" for file_name, count in counts.most_common(limit)]

    def append_first_matches(
        self,
        handle,
        title: str,
        log_file: str,
        pattern: re.Pattern[str],
        limit: int,
    ) -> None:
        handle.write(f"## {title}\n\n")
        path = self.log_path(log_file)
        if not path.exists():
            handle.write("_No log file generated._\n\n")
            return
        matches = self.first_matches(log_file, pattern, limit)
        if not matches:
            handle.write("_No findings in this category._\n\n")
            return
        handle.write("```text\n")
        handle.write("\n".join(matches))
        handle.write("\n```\n\n")

    def append_top_files(
        self,
        handle,
        title: str,
        log_file: str,
        pattern: re.Pattern[str],
        limit: int,
    ) -> None:
        handle.write(f"## {title}\n\n")
        path = self.log_path(log_file)
        if not path.exists():
            handle.write("_No log file generated._\n\n")
            return
        rows = self.top_files(log_file, pattern, limit)
        if not rows:
            handle.write("_No files in this category._\n\n")
            return
        handle.write("```text\n")
        handle.write("\n".join(rows))
        handle.write("\n```\n\n")

    def git_commit(self) -> str:
        if not self.have_tool("git"):
            return "unknown"
        completed = subprocess.run(
            ["git", "-C", str(self.root_dir), "rev-parse", "--short", "HEAD"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return completed.stdout.strip() or "unknown"

    def sarif_level(self, level: str) -> str:
        normalized = level.lower()
        if normalized == "error":
            return "error"
        if normalized in {"warn", "warning"}:
            return "warning"
        return "note"

    def finding_identity(self, finding: dict[str, object]) -> str:
        return finding_identity_value(finding)

    def baseline_payload(self) -> dict[str, object]:
        return {
            "version": 1,
            "generated": self.timestamp,
            "mode": self.mode,
            "scope": self.scope,
            "base_ref": self.base_ref if self.scope == "changed" else "",
            "git_commit": self.git_commit(),
            "finding_keys": [
                self.finding_identity(finding) for finding in self.findings
            ],
            "findings": self.findings,
        }

    def load_baseline_keys(self) -> set[str] | None:
        if self.baseline_path is None:
            return None
        if not self.baseline_path.exists():
            self.baseline_error = f"missing baseline file: {self.baseline_path}"
            self.record_failure("quality baseline")
            return None
        try:
            payload = json.loads(self.baseline_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            self.baseline_error = f"cannot read baseline {self.baseline_path}: {error}"
            self.record_failure("quality baseline")
            return None

        keys = payload.get("finding_keys")
        if isinstance(keys, list):
            baseline_keys = {str(key) for key in keys}
            self.baseline_total = len(baseline_keys)
            return baseline_keys

        findings = payload.get("findings")
        if isinstance(findings, list):
            baseline_keys = {
                self.finding_identity(finding)
                for finding in findings
                if isinstance(finding, dict)
            }
            self.baseline_total = len(baseline_keys)
            return baseline_keys

        self.baseline_error = (
            f"baseline {self.baseline_path} does not contain finding_keys or findings"
        )
        self.record_failure("quality baseline")
        return None

    def compare_baseline(self) -> None:
        if self.baseline_path is None:
            return
        self.baseline_checked = True
        baseline_keys = self.load_baseline_keys()
        if baseline_keys is None:
            self.write_baseline_diff()
            return

        threshold = SEVERITY_ORDER[self.fail_on_new]
        self.baseline_new_findings = [
            finding
            for finding in self.findings
            if self.finding_identity(finding) not in baseline_keys
        ]
        self.baseline_blocking_findings = [
            finding
            for finding in self.baseline_new_findings
            if SEVERITY_ORDER[self.sarif_level(str(finding.get("level", "note")))]
            >= threshold
        ]
        if self.baseline_blocking_findings:
            self.log(
                "quality baseline found "
                f"{len(self.baseline_blocking_findings)} new blocking findings"
            )
            self.record_failure("quality baseline")
        self.write_baseline_diff()

    def write_baseline_diff(self) -> None:
        if not self.baseline_checked:
            return
        payload = {
            "baseline": str(self.baseline_path) if self.baseline_path else "",
            "fail_on_new": self.fail_on_new,
            "baseline_error": self.baseline_error,
            "baseline_findings": self.baseline_total,
            "current_findings": len(self.findings),
            "new_findings": len(self.baseline_new_findings),
            "new_blocking_findings": len(self.baseline_blocking_findings),
            "findings": self.baseline_new_findings,
        }
        self.baseline_diff_file.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    def write_baseline_file(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps(self.baseline_payload(), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        self.log(f"baseline written: {path}")

    def write_machine_readable_findings(self) -> None:
        payload = {
            "generated": self.timestamp,
            "mode": self.mode,
            "scope": self.scope,
            "base_ref": self.base_ref if self.scope == "changed" else "",
            "git_commit": self.git_commit(),
            "findings": self.findings,
        }
        text = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
        self.findings_file.write_text(text, encoding="utf-8")
        self.latest_findings_file.write_text(text, encoding="utf-8")

    def write_sarif_findings(self) -> None:
        rules: dict[str, dict[str, object]] = {}
        results: list[dict[str, object]] = []
        for finding in self.findings:
            tool_name = str(finding.get("tool", "quality_scan"))
            rule_id = f"{tool_name}:{finding['rule_id']}"
            message = str(finding["message"])
            rules.setdefault(
                rule_id,
                {
                    "id": rule_id,
                    "shortDescription": {"text": rule_id},
                    "fullDescription": {"text": message},
                },
            )
            results.append(
                {
                    "ruleId": rule_id,
                    "level": self.sarif_level(str(finding["level"])),
                    "message": {"text": message},
                    "locations": [
                        {
                            "physicalLocation": {
                                "artifactLocation": {
                                    "uri": str(finding["path"]),
                                    "uriBaseId": "%SRCROOT%",
                                },
                                "region": {
                                    "startLine": int(finding["line"]),
                                    **(
                                        {"startColumn": int(finding["column"])}
                                        if finding.get("column")
                                        else {}
                                    ),
                                },
                            }
                        }
                    ],
                }
            )

        sarif = {
            "version": "2.1.0",
            "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
            "runs": [
                {
                    "tool": {
                        "driver": {
                            "name": "live_stream quality_scan.py",
                            "informationUri": "scripts/scan/quality_scan.py",
                            "rules": list(rules.values()),
                        }
                    },
                    "originalUriBaseIds": {
                        "%SRCROOT%": {"uri": self.root_dir.as_uri() + "/"}
                    },
                    "results": results,
                }
            ],
        }
        text = json.dumps(sarif, ensure_ascii=False, indent=2) + "\n"
        self.sarif_file.write_text(text, encoding="utf-8")
        self.latest_sarif_file.write_text(text, encoding="utf-8")

    def write_quality_report(self) -> None:
        with self.quality_report_file.open("w", encoding="utf-8") as handle:
            handle.write("# Quality Fix Report\n\n")
            handle.write(
                f"本文档由 `scripts/scan/quality_scan.py {self.mode}` 生成，"
                "只汇总当前需要修复的问题；原始日志保留在本次扫描目录中作为证据。\n\n"
            )
            handle.write(f"- Generated: `{self.timestamp}`\n")
            handle.write(f"- Scope: `{self.scope}`\n")
            if self.scope == "changed":
                handle.write(f"- Base ref: `{self.base_ref}`\n")
            handle.write(f"- Raw log directory: `{self.report_dir}`\n")
            handle.write(f"- Machine-readable findings: `{self.findings_file}`\n")
            handle.write(f"- SARIF findings: `{self.sarif_file}`\n")
            if self.baseline_checked:
                handle.write(f"- Baseline diff: `{self.baseline_diff_file}`\n")
            handle.write(f"- Git commit: `{self.git_commit()}`\n\n")

            handle.write("## 需要先修复的步骤\n\n")
            if not self.failed_steps:
                handle.write("- 必跑步骤没有失败。\n")
            else:
                for step in self.failed_steps:
                    handle.write(
                        f"- `{step}` 失败，先看 `{self.step_log_file(step)}`。\n"
                    )
            for step in self.warning_steps:
                handle.write(f"- `{step}` 有告警，确认对应工具结果是否可信。\n")
            for step in self.skipped_steps:
                handle.write(f"- `{step}` 未执行。\n")
            handle.write("\n")

            if self.baseline_checked:
                handle.write("## 基线门禁\n\n")
                handle.write(f"- Baseline: `{self.baseline_path}`\n")
                handle.write(f"- Fail on new: `{self.fail_on_new}`\n")
                if self.baseline_error:
                    handle.write(f"- Error: {self.baseline_error}\n\n")
                else:
                    handle.write(
                        "- 新增阻断问题: "
                        f"{len(self.baseline_blocking_findings)}\n\n"
                    )
                    self.append_findings_table(
                        handle,
                        "新增阻断问题",
                        self.baseline_blocking_findings,
                        120,
                    )
            else:
                blocking_findings = [
                    finding
                    for finding in self.findings
                    if self.sarif_level(str(finding.get("level", "note")))
                    in {"error", "warning"}
                ]
                self.append_findings_table(
                    handle,
                    "需要修复的问题",
                    blocking_findings,
                    120,
                )

            self.append_build_failure_tail(handle)

    def append_findings_table(
        self,
        handle,
        title: str,
        findings: list[dict[str, object]],
        limit: int,
    ) -> None:
        handle.write(f"## {title}\n\n")
        if not findings:
            handle.write("- 没有需要修复的问题。\n\n")
            return
        handle.write("| 位置 | 级别 | 工具 | 问题 |\n")
        handle.write("| --- | --- | --- | --- |\n")
        for finding in findings[:limit]:
            path = str(finding.get("path", ""))
            line = str(finding.get("line", "1"))
            level = str(finding.get("level", "note"))
            tool = str(finding.get("tool", "quality_scan"))
            rule_id = str(finding.get("rule_id", "unknown"))
            message = str(finding.get("message", "")).replace("|", "\\|")
            handle.write(
                f"| `{path}:{line}` | `{level}` | `{tool}/{rule_id}` | {message} |\n"
            )
        remaining = len(findings) - limit
        if remaining > 0:
            handle.write(f"\n还有 {remaining} 条未展开，见 `{self.findings_file}`。\n")
        handle.write("\n")

    def append_build_failure_tail(self, handle) -> None:
        handle.write("## Build Failure Tail\n\n")
        make_log = self.log_path("make.log")
        if not make_log.exists() or not self.log_contains("make.log", BUILD_FAILURE_RE):
            handle.write("_No build failure pattern detected._\n\n")
            return

        handle.write("### make -j2\n\n")
        handle.write("```text\n")
        matches = self.first_matches("make.log", BUILD_FAILURE_RE, 10_000)
        handle.write("\n".join(matches[-40:]))
        handle.write("\n```\n\n")

        make_serial_log = self.log_path("make-serial.log")
        if not make_serial_log.exists():
            return
        handle.write("### make -j1 fallback\n\n")
        if not self.log_contains("make-serial.log", BUILD_FAILURE_RE):
            handle.write("_No serial fallback build failure pattern detected._\n\n")
            return
        handle.write("```text\n")
        matches = self.first_matches("make-serial.log", BUILD_FAILURE_RE, 10_000)
        handle.write("\n".join(matches[-40:]))
        handle.write("\n```\n\n")

    def run(self) -> int:
        self.report_dir.mkdir(parents=True, exist_ok=True)
        self.log(f"writing reports to {self.report_dir}")

        self.append_tool_status()
        if self.have_tool("git"):
            self.write_command_output("git-status.txt", ["git", "status", "--short"])
            self.write_command_output("git-head.txt", ["git", "log", "--oneline", "-1"])

        self.run_build_step()
        self.run_command(
            "http/web contract",
            "http-web-contract.log",
            [sys.executable, "scripts/scan/check_http_web_contract.py"],
        )
        self.run_command(
            "cpp style contract",
            "cpp-style-contract.log",
            [sys.executable, "scripts/scan/check_cpp_style_contract.py"],
        )
        self.run_format_config_step()
        self.run_clang_format_step()

        self.run_frontend_build_step()
        self.run_frontend_typecheck_step()
        self.run_npm_script_if_present("lint", "www lint", "www-lint.log")
        self.run_npm_script_if_present("test", "www test", "www-test.log")
        self.run_npm_script_if_present(
            "format:check", "www format check", "www-format-check.log"
        )

        self.run_keyword_scan()
        self.run_hot_path_log_scan()
        self.run_builtin_scans()
        if self.mode == "full":
            self.run_compile_database_step()
        if self.have_tool("cloc"):
            self.run_warning_step(
                "cloc",
                "cloc",
                "code-size-cloc.log",
                ["cloc", "app", "libs", "configs", "www", "--exclude-dir=dist,node_modules"],
            )
        elif self.have_tool("tokei"):
            self.run_warning_step(
                "tokei",
                "tokei",
                "code-size-tokei.log",
                ["tokei", "app", "libs", "configs", "www"],
            )
        else:
            self.record_skipped("code size: missing cloc or tokei")

        self.run_cppcheck_step()
        self.run_lizard_step()
        self.run_flawfinder_step()
        self.run_semgrep_step()
        self.run_binary_info_step()

        if self.mode == "full":
            self.run_scan_build_step()
            self.run_clang_tidy_step()
            self.run_iwyu_step()

        self.compare_baseline()
        if self.write_baseline_path is not None:
            self.write_baseline_file(self.write_baseline_path)

        self.write_machine_readable_findings()
        self.write_sarif_findings()
        self.write_quality_report()
        self.log(f"report: {self.quality_report_file}")

        return 1 if self.failed_steps else 0


def resolve_cli_path(root_dir: Path, path_text: str | None) -> Path | None:
    if not path_text:
        return None
    path = Path(path_text)
    if path.is_absolute():
        return path
    return root_dir / path


def git_commit_for_root(root_dir: Path) -> str:
    if shutil.which("git") is None:
        return "unknown"
    completed = subprocess.run(
        ["git", "-C", str(root_dir), "rev-parse", "--short", "HEAD"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    return completed.stdout.strip() or "unknown"


def write_baseline_from_findings(
    root_dir: Path, source_path: Path, output_path: Path
) -> int:
    try:
        source_payload = json.loads(source_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"[quality-scan] cannot read findings source {source_path}: {error}")
        return 1

    findings = source_payload.get("findings")
    if not isinstance(findings, list):
        print(f"[quality-scan] findings source {source_path} has no findings list")
        return 1

    normalized_findings = [
        finding for finding in findings if isinstance(finding, dict)
    ]
    payload = {
        "version": 1,
        "generated": datetime.now().strftime("%Y%m%d-%H%M%S"),
        "source": str(source_path),
        "source_generated": source_payload.get("generated", ""),
        "source_mode": source_payload.get("mode", ""),
        "source_scope": source_payload.get("scope", ""),
        "git_commit": git_commit_for_root(root_dir),
        "finding_keys": [
            finding_identity_value(finding) for finding in normalized_findings
        ],
        "findings": normalized_findings,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        "[quality-scan] baseline written: "
        f"{output_path} ({len(normalized_findings)} findings)"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run project quality scans.")
    parser.add_argument(
        "mode",
        nargs="?",
        default="quick",
        choices=("quick", "full", "baseline"),
        help=(
            "quick runs local gates; full adds compile database and clang "
            "analysis; baseline writes a baseline from an existing findings JSON"
        ),
    )
    parser.add_argument(
        "--scope",
        choices=("all", "changed"),
        default="all",
        help="all scans the repository; changed narrows built-in file scans to git changes",
    )
    parser.add_argument(
        "--base-ref",
        default="HEAD",
        help="git ref used by --scope changed",
    )
    parser.add_argument(
        "--baseline",
        help="compare scan findings against a baseline JSON file",
    )
    parser.add_argument(
        "--fail-on-new",
        choices=("note", "warning", "error"),
        default="warning",
        help="minimum level of new baseline findings that fails the scan",
    )
    parser.add_argument(
        "--write-baseline",
        help="write current scan findings to this baseline JSON file after scanning",
    )
    parser.add_argument(
        "--from-findings",
        default="scripts/scan/reports/quality/quality_findings.json",
        help="findings JSON used by baseline mode",
    )
    parser.add_argument(
        "--output",
        default="scripts/scan/quality_baseline.json",
        help="baseline output path used by baseline mode",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root_dir = Path(__file__).resolve().parents[2]
    if args.mode == "baseline":
        source_path = resolve_cli_path(root_dir, args.from_findings)
        output_path = resolve_cli_path(root_dir, args.output)
        if source_path is None or output_path is None:
            print("[quality-scan] baseline mode requires --from-findings and --output")
            return 1
        return write_baseline_from_findings(root_dir, source_path, output_path)

    scanner = QualityScan(
        root_dir,
        args.mode,
        args.scope,
        args.base_ref,
        resolve_cli_path(root_dir, args.baseline),
        resolve_cli_path(root_dir, args.write_baseline),
        args.fail_on_new,
    )
    return scanner.run()


if __name__ == "__main__":
    raise SystemExit(main())

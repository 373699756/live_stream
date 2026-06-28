#!/usr/bin/env python3
"""Check that Web Console API calls are covered by backend HTTP routes."""

from __future__ import annotations

import dataclasses
import pathlib
import re
import sys
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[2]
BACKEND_GLOBS = (
    "libs/http/src/**/*.cpp",
    "libs/http_media/src/**/*.cpp",
)
FRONTEND_API_DIR = ROOT / "www" / "src" / "api"

METHOD_NAMES = {
    "Get": "GET",
    "Post": "POST",
    "Put": "PUT",
    "Delete": "DELETE",
}

HELPER_METHODS = {
    "requestJson": "GET",
    "postJson": "POST",
    "putJson": "PUT",
    "deleteJson": "DELETE",
}

SKIP_FRONTEND_FILES = {
    "authSession.ts",
    "client.ts",
    "configDefaults.ts",
    "resolution.ts",
    "types.ts",
}


@dataclasses.dataclass(frozen=True)
class BackendRoute:
    method: str
    path: str
    match_type: str
    source: str
    line: int


@dataclasses.dataclass(frozen=True)
class FrontendCall:
    method: str
    path: str
    source: str
    line: int


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def source_name(path: pathlib.Path) -> str:
    return str(path.relative_to(ROOT))


def strip_query(path: str) -> str:
    return path.split("?", 1)[0]


def normalize_template_path(raw_path: str) -> str:
    path = raw_path.strip()
    path = strip_query(path)
    path = re.sub(r"\$\{[^}]*\}", "{param}", path)
    if "${" in path:
        path = path.split("${", 1)[0]
    return path


def path_static_prefix(path: str) -> str:
    marker = path.find("{param}")
    if marker == -1:
        return path
    return path[:marker]


def extract_backend_routes() -> list[BackendRoute]:
    routes: list[BackendRoute] = []
    route_pattern = re.compile(
        r"Add(?P<match>Exact|Prefix)Route\s*\(\s*"
        r"HttpMethod::k(?P<method>Get|Post|Put|Delete)\s*,\s*"
        r'"(?P<path>/(?:api|live|snapshot)[^"]*)"',
        re.MULTILINE | re.DOTALL,
    )
    for glob in BACKEND_GLOBS:
        for path in sorted(ROOT.glob(glob)):
            text = path.read_text(encoding="utf-8")
            for match in route_pattern.finditer(text):
                routes.append(
                    BackendRoute(
                        method=METHOD_NAMES[match.group("method")],
                        path=match.group("path"),
                        match_type=match.group("match").lower(),
                        source=source_name(path),
                        line=line_number(text, match.start()),
                    )
                )
    return routes


def should_scan_frontend(path: pathlib.Path) -> bool:
    if path.name in SKIP_FRONTEND_FILES:
        return False
    return not path.name.startswith("mock")


def extract_quoted_path(text: str, start: int) -> tuple[str, int] | None:
    index = start
    while index < len(text) and text[index].isspace():
        index += 1
    if index >= len(text) or text[index] not in ("'", '"', "`"):
        return None

    quote = text[index]
    index += 1
    value_start = index
    escaped = False
    while index < len(text):
        char = text[index]
        if escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == quote:
            return text[value_start:index], index + 1
        index += 1
    return None


def extract_frontend_helper_calls(path: pathlib.Path,
                                  text: str) -> list[FrontendCall]:
    calls: list[FrontendCall] = []
    helper_pattern = re.compile(
        r"\b(?P<helper>requestJson|postJson|putJson|deleteJson)"
        r"(?:<[^>]*>)?\s*\(",
        re.MULTILINE | re.DOTALL,
    )
    for match in helper_pattern.finditer(text):
        extracted = extract_quoted_path(text, match.end())
        if extracted is None:
            continue
        raw_path, _ = extracted
        if not raw_path.startswith(("/api/", "/live/", "/snapshot/")):
            continue
        calls.append(
            FrontendCall(
                method=HELPER_METHODS[match.group("helper")],
                path=normalize_template_path(raw_path),
                source=source_name(path),
                line=line_number(text, match.start()),
            )
        )
    return calls


def extract_upload_binary_calls(path: pathlib.Path,
                                text: str) -> list[FrontendCall]:
    calls: list[FrontendCall] = []
    upload_pattern = re.compile(r"\buploadBinary(?:<[^>]*>)?\s*\(")
    path_pattern = re.compile(r"\bpath\s*:")
    for match in upload_pattern.finditer(text):
        body_start = match.end()
        body_end = text.find("});", body_start)
        if body_end == -1:
            body_end = text.find(")", body_start)
        if body_end == -1:
            continue
        body = text[body_start:body_end]
        path_match = path_pattern.search(body)
        if path_match is None:
            continue
        extracted = extract_quoted_path(body, path_match.end())
        if extracted is None:
            continue
        raw_path, _ = extracted
        if not raw_path.startswith(("/api/", "/live/", "/snapshot/")):
            continue
        calls.append(
            FrontendCall(
                method="POST",
                path=normalize_template_path(raw_path),
                source=source_name(path),
                line=line_number(text, match.start()),
            )
        )
    return calls


def extract_fetch_calls(path: pathlib.Path, text: str) -> list[FrontendCall]:
    calls: list[FrontendCall] = []
    fetch_pattern = re.compile(r"\bfetch\s*\(")
    method_pattern = re.compile(r"\bmethod\s*:\s*['\"]([A-Z]+)['\"]")
    for match in fetch_pattern.finditer(text):
        extracted = extract_quoted_path(text, match.end())
        if extracted is None:
            continue
        raw_path, end = extracted
        if not raw_path.startswith(("/api/", "/live/", "/snapshot/")):
            continue
        call_end = text.find(")", end)
        call_body = text[end:call_end if call_end != -1 else end]
        method_match = method_pattern.search(call_body)
        method = method_match.group(1) if method_match else "GET"
        calls.append(
            FrontendCall(
                method=method,
                path=normalize_template_path(raw_path),
                source=source_name(path),
                line=line_number(text, match.start()),
            )
        )
    return calls


def extract_returned_urls(path: pathlib.Path, text: str) -> list[FrontendCall]:
    calls: list[FrontendCall] = []
    return_pattern = re.compile(r"\breturn\s+")
    for match in return_pattern.finditer(text):
        extracted = extract_quoted_path(text, match.end())
        if extracted is None:
            continue
        raw_path, _ = extracted
        if not raw_path.startswith(("/api/", "/live/", "/snapshot/")):
            continue
        calls.append(
            FrontendCall(
                method="GET",
                path=normalize_template_path(raw_path),
                source=source_name(path),
                line=line_number(text, match.start()),
            )
        )
    return calls


def extract_frontend_calls() -> list[FrontendCall]:
    calls: list[FrontendCall] = []
    for path in sorted(FRONTEND_API_DIR.glob("*.ts")):
        if not should_scan_frontend(path):
            continue
        text = path.read_text(encoding="utf-8")
        calls.extend(extract_frontend_helper_calls(path, text))
        calls.extend(extract_upload_binary_calls(path, text))
        calls.extend(extract_fetch_calls(path, text))
        calls.extend(extract_returned_urls(path, text))

    unique: dict[tuple[str, str, str, int], FrontendCall] = {}
    for call in calls:
        unique[(call.method, call.path, call.source, call.line)] = call
    return sorted(unique.values(), key=lambda item: (item.source, item.line,
                                                     item.method, item.path))


def route_covers_call(route: BackendRoute, call: FrontendCall) -> bool:
    if route.method != call.method:
        return False
    if route.match_type == "exact":
        return route.path == call.path
    return path_static_prefix(call.path).startswith(route.path)


def find_missing_calls(routes: Iterable[BackendRoute],
                       calls: Iterable[FrontendCall]) -> list[FrontendCall]:
    route_list = list(routes)
    missing: list[FrontendCall] = []
    for call in calls:
        if not any(route_covers_call(route, call) for route in route_list):
            missing.append(call)
    return missing


def check_envelope_contract() -> list[str]:
    errors: list[str] = []
    http_cpp = (ROOT / "libs" / "http" / "src" / "http.cpp").read_text(
        encoding="utf-8")
    response_cpp = (
        ROOT / "libs" / "http" / "src" / "http_response.cpp"
    ).read_text(encoding="utf-8")
    client_ts = (ROOT / "www" / "src" / "api" / "client.ts").read_text(
        encoding="utf-8")

    if 'StartsWith(request_with_id.path, "/api/")' not in http_cpp:
        errors.append("backend does not special-case /api/* responses")
    if "AddJsonEnvelope" not in http_cpp:
        errors.append("backend request dispatcher does not call AddJsonEnvelope")
    for field in ('"ok"', '"data"', '"error"', '"request_id"'):
        if field not in response_cpp:
            errors.append(f"backend JSON envelope is missing {field}")
    if "unwrapEnvelope" not in client_ts or "hasEnvelopeShape" not in client_ts:
        errors.append("frontend API client does not unwrap JSON envelopes")
    return errors


def print_routes(routes: Iterable[BackendRoute]) -> None:
    for route in routes:
        print(f"  {route.method:6} {route.path:45} "
              f"{route.match_type:6} {route.source}:{route.line}")


def main() -> int:
    routes = extract_backend_routes()
    calls = extract_frontend_calls()
    missing = find_missing_calls(routes, calls)
    envelope_errors = check_envelope_contract()

    print(f"[http-web-contract] backend routes: {len(routes)}")
    print(f"[http-web-contract] frontend API calls: {len(calls)}")

    if missing:
        print("\nMissing backend route coverage:")
        for call in missing:
            print(f"  {call.method:6} {call.path:45} "
                  f"{call.source}:{call.line}")

    if envelope_errors:
        print("\nEnvelope contract errors:")
        for error in envelope_errors:
            print(f"  - {error}")

    if missing or envelope_errors:
        print("\nBackend routes scanned:")
        print_routes(routes)
        return 1

    print("[http-web-contract] route coverage and envelope checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

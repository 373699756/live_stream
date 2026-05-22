#!/usr/bin/env bash

set -u
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-quick}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_ROOT="${ROOT_DIR}/reports/quality"
REPORT_DIR="${REPORT_ROOT}/${TIMESTAMP}"
SUMMARY_FILE="${REPORT_DIR}/summary.md"

if [[ "${MODE}" != "quick" && "${MODE}" != "full" ]]; then
  echo "Usage: $0 [quick|full]" >&2
  exit 2
fi

mkdir -p "${REPORT_DIR}"

declare -a FAILED_STEPS=()
declare -a SKIPPED_STEPS=()

Log() {
  printf '[quality-scan] %s\n' "$*"
}

HaveTool() {
  command -v "$1" >/dev/null 2>&1
}

RecordFailure() {
  FAILED_STEPS+=("$1")
}

RecordSkipped() {
  SKIPPED_STEPS+=("$1")
}

RunStep() {
  local name="$1"
  local log_file="$2"
  shift 2

  Log "running ${name}"
  {
    printf '$'
    printf ' %q' "$@"
    printf '\n\n'
    "$@"
  } >"${REPORT_DIR}/${log_file}" 2>&1

  local status=$?
  if [[ ${status} -ne 0 ]]; then
    Log "${name} failed with exit code ${status}"
    RecordFailure "${name}"
  fi
}

RunOptionalStep() {
  local tool="$1"
  local name="$2"
  local log_file="$3"
  shift 3

  if ! HaveTool "${tool}"; then
    Log "skipping ${name}: missing ${tool}"
    RecordSkipped "${name}: missing ${tool}"
    return 0
  fi

  RunStep "${name}" "${log_file}" "$@"
}

RunRgStep() {
  local name="$1"
  local log_file="$2"
  shift 2

  if ! HaveTool rg; then
    Log "skipping ${name}: missing rg"
    RecordSkipped "${name}: missing rg"
    return 0
  fi

  Log "running ${name}"
  {
    printf '$'
    printf ' %q' rg "$@"
    printf '\n\n'
    rg "$@"
  } >"${REPORT_DIR}/${log_file}" 2>&1

  local status=$?
  if [[ ${status} -eq 1 ]]; then
    printf '\n(no matches)\n' >>"${REPORT_DIR}/${log_file}"
    return 0
  fi
  if [[ ${status} -ne 0 ]]; then
    Log "${name} failed with exit code ${status}"
    RecordFailure "${name}"
  fi
}

WriteCommandOutput() {
  local output_file="$1"
  shift

  {
    printf '$'
    printf ' %q' "$@"
    printf '\n\n'
    "$@"
  } >"${REPORT_DIR}/${output_file}" 2>&1
}

AppendToolStatus() {
  local tool="$1"
  if HaveTool "${tool}"; then
    printf 'yes %s %s\n' "${tool}" "$(command -v "${tool}")"
  else
    printf 'no %s\n' "${tool}"
  fi
}

WriteSummary() {
  {
    printf '# Quality Scan Summary\n\n'
    printf '%s\n' "- Mode: \`${MODE}\`"
    printf '%s\n' "- Report directory: \`${REPORT_DIR}\`"
    printf '%s\n' "- Timestamp: \`${TIMESTAMP}\`"
    if HaveTool git; then
      printf '%s\n' "- Git commit: \`$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || printf 'unknown')\`"
    fi
    printf '\n'

    printf '## Result\n\n'
    if [[ ${#FAILED_STEPS[@]} -eq 0 ]]; then
      printf '%s\n' '- Required steps: passed'
    else
      printf '%s\n' '- Required steps: failed'
      for step in "${FAILED_STEPS[@]}"; do
        printf '  - %s\n' "${step}"
      done
    fi
    if [[ ${#SKIPPED_STEPS[@]} -gt 0 ]]; then
      printf '%s\n' '- Skipped optional steps:'
      for step in "${SKIPPED_STEPS[@]}"; do
        printf '  - %s\n' "${step}"
      done
    fi
    printf '\n'

    printf '## Logs\n\n'
    find "${REPORT_DIR}" -maxdepth 1 -type f ! -name 'summary.md' \
      -printf '- `%f`\n' | sort
  } >"${SUMMARY_FILE}"
}

cd "${ROOT_DIR}" || exit 1

Log "writing reports to ${REPORT_DIR}"

{
  for tool in rg git make arm-himix200-linux-gcc arm-himix200-linux-g++ \
    arm-himix200-linux-size gcc g++ clang clang++ clang-tidy clang-format \
    cppcheck scan-build scan-build-10 bear cloc tokei perf valgrind strace \
    ltrace gdb gdb-multiarch readelf objdump nm size addr2line file node npm \
    npx include-what-you-use iwyu; do
    AppendToolStatus "${tool}"
  done
} >"${REPORT_DIR}/tool-status.txt"

if HaveTool git; then
  WriteCommandOutput "git-status.txt" git status --short
  WriteCommandOutput "git-head.txt" git log --oneline -1
fi

RunStep "make" "make.log" make -j2

if [[ -d "${ROOT_DIR}/www" ]]; then
  Log "running frontend build"
  (
    cd "${ROOT_DIR}/www" || exit 1
    printf '$ cd www && npm run build\n\n'
    npm run build
  ) >"${REPORT_DIR}/www-build.log" 2>&1
  status=$?
  if [[ ${status} -ne 0 ]]; then
    Log "frontend build failed with exit code ${status}"
    RecordFailure "www build"
  fi
else
  RecordSkipped "www build: missing www directory"
fi

RunRgStep "keyword scan" "keyword-scan.log" \
  -n "TODO|FIXME|XXX|HACK|sleep|usleep|malloc|free|new |delete |memcpy|strcpy|sprintf|printf|pthread|mutex|lock|detach" \
  app libs configs www

RunRgStep "hot path log scan" "hot-path-log-scan.log" \
  -n "LOG|Log|printf|std::cout|PublishFrame|OnFrame|Encode|Write|Send|Push" app libs

if HaveTool cloc; then
  RunStep "cloc" "code-size-cloc.log" cloc app libs configs www --exclude-dir=dist,node_modules
elif HaveTool tokei; then
  RunStep "tokei" "code-size-tokei.log" tokei app libs configs www
else
  RecordSkipped "code size: missing cloc or tokei"
fi

RunOptionalStep cppcheck "cppcheck" "cppcheck.log" \
  cppcheck --enable=warning,performance,portability --std=c++17 \
  --suppress=missingIncludeSystem app libs/*/include libs/*/src

if [[ -f "${ROOT_DIR}/build/bin/live_stream" ]]; then
  {
    file build/bin/live_stream
    if HaveTool arm-himix200-linux-size; then
      arm-himix200-linux-size build/bin/live_stream
    elif HaveTool size; then
      size build/bin/live_stream
    fi
    if HaveTool readelf; then
      readelf -h build/bin/live_stream
    fi
    if HaveTool nm; then
      nm -S --size-sort build/bin/live_stream
    fi
  } >"${REPORT_DIR}/binary-info.txt" 2>&1
else
  RecordSkipped "binary info: missing build/bin/live_stream"
fi

if [[ "${MODE}" == "full" ]]; then
  RunOptionalStep bear "compile database" "bear.log" bear -- make -j2

  if HaveTool scan-build-10; then
    RunStep "scan-build-10" "scan-build.log" scan-build-10 make -j2
  elif HaveTool scan-build; then
    RunStep "scan-build" "scan-build.log" scan-build make -j2
  else
    RecordSkipped "scan-build: missing scan-build-10 or scan-build"
  fi

  if [[ -f "${ROOT_DIR}/compile_commands.json" ]] && HaveTool clang-tidy; then
    mapfile -t cpp_sources < <(find app libs -path '*/tests/*' -prune -o \
      -name '*.cpp' -print | sort)
    if [[ ${#cpp_sources[@]} -gt 0 ]]; then
      RunStep "clang-tidy" "clang-tidy.log" clang-tidy -p "${ROOT_DIR}" "${cpp_sources[@]}"
    else
      RecordSkipped "clang-tidy: no production .cpp files found"
    fi
  elif ! HaveTool clang-tidy; then
    RecordSkipped "clang-tidy: missing clang-tidy"
  else
    RecordSkipped "clang-tidy: missing compile_commands.json"
  fi

  if HaveTool include-what-you-use; then
    RecordSkipped "include-what-you-use: installed but not wired into this scan"
  elif HaveTool iwyu; then
    RecordSkipped "iwyu: installed but not wired into this scan"
  else
    RecordSkipped "include-what-you-use: missing include-what-you-use or iwyu"
  fi
fi

WriteSummary

Log "summary: ${SUMMARY_FILE}"

if [[ ${#FAILED_STEPS[@]} -gt 0 ]]; then
  exit 1
fi

exit 0

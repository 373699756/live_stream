#!/usr/bin/env bash

set -u
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-quick}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_ROOT="${ROOT_DIR}/reports/quality"
REPORT_DIR="${REPORT_ROOT}/${TIMESTAMP}"
SUMMARY_FILE="${REPORT_DIR}/summary.md"
DOC_REPORT_DIR="${ROOT_DIR}/docs/quality"
DOC_REPORT_FILE="${DOC_REPORT_DIR}/quality_report.md"

if [[ "${MODE}" != "quick" && "${MODE}" != "full" ]]; then
  echo "Usage: $0 [quick|full]" >&2
  exit 2
fi

mkdir -p "${REPORT_DIR}" "${DOC_REPORT_DIR}"

declare -a FAILED_STEPS=()
declare -a SKIPPED_STEPS=()
declare -a WARNING_STEPS=()

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

RecordWarning() {
  WARNING_STEPS+=("$1")
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

RunWarningStep() {
  local tool="$1"
  local name="$2"
  local log_file="$3"
  shift 3

  if ! HaveTool "${tool}"; then
    Log "skipping ${name}: missing ${tool}"
    RecordSkipped "${name}: missing ${tool}"
    return 0
  fi

  Log "running ${name}"
  {
    printf '$'
    printf ' %q' "$@"
    printf '\n\n'
    "$@"
  } >"${REPORT_DIR}/${log_file}" 2>&1

  local status=$?
  if [[ ${status} -ne 0 ]]; then
    Log "${name} finished with warning exit code ${status}"
    RecordWarning "${name}: exit code ${status}"
  fi
}

RunShellStep() {
  local name="$1"
  local log_file="$2"
  local script="$3"

  Log "running ${name}"
  {
    printf '$ %s\n\n' "${script}"
    bash -lc "${script}"
  } >"${REPORT_DIR}/${log_file}" 2>&1

  local status=$?
  if [[ ${status} -ne 0 ]]; then
    Log "${name} failed with exit code ${status}"
    RecordFailure "${name}"
  fi
}

RunShellWarningStep() {
  local name="$1"
  local log_file="$2"
  local script="$3"

  Log "running ${name}"
  {
    printf '$ %s\n\n' "${script}"
    bash -lc "${script}"
  } >"${REPORT_DIR}/${log_file}" 2>&1

  local status=$?
  if [[ ${status} -ne 0 ]]; then
    Log "${name} finished with warning exit code ${status}"
    RecordWarning "${name}: exit code ${status}"
  fi
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
    if [[ ${#WARNING_STEPS[@]} -gt 0 ]]; then
      printf '%s\n' '- Warning steps:'
      for step in "${WARNING_STEPS[@]}"; do
        printf '  - %s\n' "${step}"
      done
    fi
    printf '\n'

    printf '## Logs\n\n'
    find "${REPORT_DIR}" -maxdepth 1 -type f ! -name 'summary.md' \
      -printf '- `%f`\n' | sort
  } >"${SUMMARY_FILE}"
}

AppendFirstMatches() {
  local title="$1"
  local source_file="$2"
  local pattern="$3"
  local limit="$4"

  printf '## %s\n\n' "${title}"
  if [[ ! -f "${source_file}" ]]; then
    printf '_No log file generated._\n\n'
    return 0
  fi

  local matches
  matches="$(awk -v pat="${pattern}" -v limit="${limit}" \
    '$0 ~ pat { print; count++; if (count >= limit) exit }' \
    "${source_file}" || true)"
  if [[ -z "${matches}" ]]; then
    printf '_No findings in this category._\n\n'
    return 0
  fi

  printf '```text\n'
  printf '%s\n' "${matches}"
  printf '```\n\n'
}

AppendTopFiles() {
  local title="$1"
  local source_file="$2"
  local pattern="$3"
  local limit="$4"

  printf '## %s\n\n' "${title}"
  if [[ ! -f "${source_file}" ]]; then
    printf '_No log file generated._\n\n'
    return 0
  fi

  local matches
  matches="$(awk -F: -v pat="${pattern}" '$0 ~ pat {print $1}' "${source_file}" \
    | sort \
    | uniq -c \
    | sort -nr \
    | head -n "${limit}" || true)"
  if [[ -z "${matches}" ]]; then
    printf '_No files in this category._\n\n'
    return 0
  fi

  printf '```text\n'
  printf '%s\n' "${matches}"
  printf '```\n\n'
}

CountMatches() {
  local source_file="$1"
  local pattern="$2"

  if [[ ! -f "${source_file}" ]]; then
    printf '0'
    return 0
  fi

  awk -v pat="${pattern}" '$0 ~ pat {count++} END {print count + 0}' "${source_file}"
}

StepLogFile() {
  case "$1" in
    make)
      printf 'make.log'
      ;;
    "www build")
      printf 'www-build.log'
      ;;
    cppcheck)
      printf 'cppcheck.log'
      ;;
    "compile database")
      printf 'bear.log'
      ;;
    clang-tidy)
      printf 'clang-tidy.log'
      ;;
    "include-what-you-use")
      printf 'iwyu.log'
      ;;
    *)
      printf 'summary.md'
      ;;
  esac
}

WriteQualityReport() {
  local cppcheck_log="${REPORT_DIR}/cppcheck.log"
  local keyword_log="${REPORT_DIR}/keyword-scan.log"
  local hot_path_log="${REPORT_DIR}/hot-path-log-scan.log"
  local clang_tidy_log="${REPORT_DIR}/clang-tidy.log"
  local iwyu_log="${REPORT_DIR}/iwyu.log"
  local make_log="${REPORT_DIR}/make.log"

  local cppcheck_count
  local cppcheck_error_count
  local keyword_count
  local hot_path_count
  local clang_tidy_count
  local iwyu_count
  cppcheck_count="$(CountMatches "${cppcheck_log}" '^(app|libs)/.*:[0-9]+:[0-9]+: (error|warning|style|performance|portability):')"
  cppcheck_error_count="$(CountMatches "${cppcheck_log}" '^(app|libs)/.*:[0-9]+:[0-9]+: error:')"
  keyword_count="$(CountMatches "${keyword_log}" '^(app|libs|configs|www)/.*:')"
  hot_path_count="$(CountMatches "${hot_path_log}" '^(app|libs)/.*:')"
  clang_tidy_count="$(CountMatches "${clang_tidy_log}" '^(app|libs)/.*:[0-9]+:[0-9]+: (error|warning):')"
  iwyu_count="$(CountMatches "${iwyu_log}" ' should add these lines:| should remove these lines:| has correct #includes/fwd-decls')"

  {
    printf '# Quality Report\n\n'
    printf '本文档由 `scripts/quality_scan.sh %s` 生成，汇总代码质量、性能和设计候选问题。\n\n' "${MODE}"
    printf '%s\n' "- Generated: \`${TIMESTAMP}\`"
    printf '%s\n' "- Raw log directory: \`${REPORT_DIR}\`"
    if HaveTool git; then
      printf '%s\n' "- Git commit: \`$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || printf 'unknown')\`"
    fi
    printf '\n'

    printf '## Counts\n\n'
    printf '%s\n' "- cppcheck diagnostics: ${cppcheck_count}"
    printf '%s\n' "- cppcheck errors: ${cppcheck_error_count}"
    printf '%s\n' "- keyword risk hits: ${keyword_count}"
    printf '%s\n' "- hot-path/logging hits: ${hot_path_count}"
    printf '%s\n' "- clang-tidy diagnostics: ${clang_tidy_count}"
    printf '%s\n' "- include-what-you-use findings: ${iwyu_count}"
    printf '\n'

    printf '## Must Check First\n\n'
    if [[ ${#FAILED_STEPS[@]} -eq 0 ]]; then
      printf '%s\n' '- No required step failed.'
    else
      for step in "${FAILED_STEPS[@]}"; do
        printf '%s\n' "- Required step failed: \`${step}\`; inspect \`$(StepLogFile "${step}")\`."
      done
    fi
    if [[ ${#WARNING_STEPS[@]} -gt 0 ]]; then
      for step in "${WARNING_STEPS[@]}"; do
        printf '%s\n' "- Warning step: \`${step}\`; inspect related log before trusting that tool result."
      done
    fi
    if [[ ${#SKIPPED_STEPS[@]} -gt 0 ]]; then
      for step in "${SKIPPED_STEPS[@]}"; do
        printf '%s\n' "- Skipped: \`${step}\`."
      done
    fi
    printf '\n'

    AppendFirstMatches "Must Fix: Cppcheck Errors" "${cppcheck_log}" \
      '^(app|libs)/.*:[0-9]+:[0-9]+: error:' 40

    AppendFirstMatches "Review: Cppcheck Warnings" "${cppcheck_log}" \
      '^(app|libs)/.*:[0-9]+:[0-9]+: (warning|style|performance|portability):' 80

    AppendFirstMatches "Review: Clang-Tidy Diagnostics" "${clang_tidy_log}" \
      '^(app|libs)/.*:[0-9]+:[0-9]+: (error|warning):' 80

    AppendFirstMatches "Review: Include-What-You-Use" "${iwyu_log}" \
      ' should add these lines:| should remove these lines:| has correct #includes/fwd-decls' 80

    AppendTopFiles "Optimization Candidates: Files With Most Keyword Risk Hits" "${keyword_log}" \
      '^(app|libs|configs|www)/.*:' 20

    AppendTopFiles "Optimization Candidates: Files With Most Hot-Path Or Logging Hits" "${hot_path_log}" \
      '^(app|libs)/.*:' 20

    printf '## Build Failure Tail\n\n'
    if [[ -f "${make_log}" ]] && grep -qE '(^|[[:space:]])(error:|Error [0-9]+|Bad system call|undefined reference|No such file)' "${make_log}"; then
      printf '```text\n'
      grep -E '(^|[[:space:]])(error:|Error [0-9]+|Bad system call|undefined reference|No such file)' "${make_log}" | tail -n 40
      printf '\n```\n\n'
    else
      printf '_No build failure pattern detected._\n\n'
    fi

    printf '## How To Use This Report\n\n'
    printf '1. 先处理 `Must Check First` 中的失败步骤。\n'
    printf '2. 再处理 `Must Fix`，这些比关键词命中更可靠。\n'
    printf '3. `Review` 是设计/生命周期风险，逐项确认是否真实影响业务。\n'
    printf '4. `Optimization Candidates` 只列热点候选文件，具体行号到原始日志里追。\n'
    printf '5. 原始工具日志只作证据，不作为主要阅读入口。\n'
  } >"${DOC_REPORT_FILE}"
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
  -n --glob '!**/tests/**' --glob '!www/package-lock.json' \
  --glob '!www/public/vendor/**' \
  "TODO|FIXME|XXX|HACK|sleep_for|usleep|malloc|free|new |delete |memcpy|strcpy|sprintf|pthread|recursive_mutex|detach" \
  app libs configs www/src

RunRgStep "hot path log scan" "hot-path-log-scan.log" \
  -n --glob '!**/tests/**' \
  "PublishFrame|OnFrame|EncodedFrame|Encode|WriteFrame|SendFrame|Send\\(|Push|memcpy|malloc|free|usleep|sleep_for" \
  app libs

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
  if HaveTool bear; then
    RunShellStep "compile database" "bear.log" \
      "make clean && bear -- make -j2"
  else
    RecordSkipped "compile database: missing bear"
  fi

  if HaveTool scan-build-10; then
    RunShellWarningStep "scan-build-10" "scan-build.log" \
      "make clean && scan-build-10 --use-cc arm-himix200-linux-gcc --use-c++ arm-himix200-linux-g++ make -j2"
  elif HaveTool scan-build; then
    RunShellWarningStep "scan-build" "scan-build.log" \
      "make clean && scan-build --use-cc arm-himix200-linux-gcc --use-c++ arm-himix200-linux-g++ make -j2"
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

  if [[ -f "${ROOT_DIR}/compile_commands.json" ]] && HaveTool iwyu_tool; then
    RunShellWarningStep "include-what-you-use" "iwyu.log" \
      "iwyu_tool -p '${ROOT_DIR}' app libs -- -Xiwyu --cxx17ns"
  elif [[ -f "${ROOT_DIR}/compile_commands.json" ]] && HaveTool include-what-you-use; then
    RunShellWarningStep "include-what-you-use" "iwyu.log" \
      "find app libs -path '*/tests/*' -prune -o -name '*.cpp' -print | sort | head -n 40 | xargs -r -n 1 include-what-you-use -std=c++17 -Iapp -Ilibs/infra_service/include"
  elif ! HaveTool iwyu_tool && ! HaveTool include-what-you-use && ! HaveTool iwyu; then
    RecordSkipped "include-what-you-use: missing include-what-you-use or iwyu"
  else
    RecordSkipped "include-what-you-use: missing compile_commands.json"
  fi
fi

WriteQualityReport
WriteSummary

Log "report: ${DOC_REPORT_FILE}"
Log "summary: ${SUMMARY_FILE}"

if [[ ${#FAILED_STEPS[@]} -gt 0 ]]; then
  exit 1
fi

exit 0

#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
debug_dir="${1:-${repo_root}/debug}"

resolve_output_dir() {
  case "$1" in
    /*)
      printf '%s\n' "$1"
      ;;
    *)
      printf '%s/%s\n' "${repo_root}" "$1"
      ;;
  esac
}

reject_repo_root() {
  if [ "$1" = "${repo_root}" ]; then
    echo "debug output directory must not be the repository root" >&2
    exit 1
  fi
}

copy_debug_inputs() {
  rm -rf "${debug_dir}/bin" "${debug_dir}/configs" "${debug_dir}/web"
  mkdir -p "${debug_dir}/bin" "${debug_dir}/configs" \
    "${debug_dir}/log" "${debug_dir}/web"

  cp -f "${repo_root}/build/bin/live_stream" "${debug_dir}/bin/"
  cp -f "${repo_root}"/configs/*.json "${debug_dir}/configs/"
  if [ -f "${repo_root}/configs/upgrade_public_key.pem" ]; then
    cp -f "${repo_root}/configs/upgrade_public_key.pem" \
      "${debug_dir}/configs/"
  fi
  cp -rf "${repo_root}/www/dist/." "${debug_dir}/web/"
}

debug_dir=$(resolve_output_dir "${debug_dir}")
mkdir -p "${debug_dir}"
debug_dir=$(CDPATH= cd -- "${debug_dir}" && pwd -P)

case "${debug_dir}" in
  ""|"/")
    echo "invalid debug output directory: ${debug_dir}" >&2
    exit 1
    ;;
esac
reject_repo_root "${debug_dir}"

copy_debug_inputs

echo "Debug output: ${debug_dir}"

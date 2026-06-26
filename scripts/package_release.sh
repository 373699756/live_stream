#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
release_dir="${1:-${repo_root}/release}"
version="${2:-1.0.0}"
profile="${3:-all}"
tools_dir="${repo_root}/tools/pc"
default_public_key="${repo_root}/configs/upgrade_public_key.pem"
sign_key="${UPGRADE_SIGN_KEY:-}"
public_key="${UPGRADE_PUBLIC_KEY:-}"
bin_partition_size=10485760
web_partition_size=2097152
config_partition_size=1048576

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

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing tool: $1" >&2
    exit 1
  fi
}

signing_key_pair_ok() {
  private_key="$1"
  verify_key="$2"
  probe_dir="${release_dir}/.signing_probe"
  probe_data="${probe_dir}/data"
  probe_sig="${probe_dir}/data.sig"

  if [ ! -f "${private_key}" ] || [ ! -f "${verify_key}" ]; then
    return 1
  fi
  rm -rf "${probe_dir}"
  mkdir -p "${probe_dir}"
  printf 'live_stream upgrade signing probe\n' > "${probe_data}"
  if ! openssl dgst -sha256 -sign "${private_key}" \
      -out "${probe_sig}" "${probe_data}" >/dev/null 2>&1; then
    rm -rf "${probe_dir}"
    return 1
  fi
  if ! openssl dgst -sha256 -verify "${verify_key}" \
      -signature "${probe_sig}" "${probe_data}" >/dev/null 2>&1; then
    rm -rf "${probe_dir}"
    return 1
  fi
  rm -rf "${probe_dir}"
  return 0
}

generate_default_signing_key_pair() {
  old_umask=$(umask)
  umask 077
  rm -f "${sign_key}" "${public_key}"
  openssl genrsa -out "${sign_key}" 2048 >/dev/null 2>&1
  umask "${old_umask}"
  openssl rsa -in "${sign_key}" -pubout -out "${public_key}" >/dev/null 2>&1
  chmod 600 "${sign_key}" 2>/dev/null || true
}

prepare_default_signing_key() {
  if [ -n "${public_key}" ]; then
    echo "UPGRADE_PUBLIC_KEY requires a matching UPGRADE_SIGN_KEY" >&2
    exit 1
  fi

  signing_dir=$(resolve_output_dir "${RELEASE_SIGNING_DIR:-scripts/release_signing}")
  mkdir -p "${signing_dir}"

  sign_key="${signing_dir}/default_upgrade_private_key.pem"
  public_key="${signing_dir}/default_upgrade_public_key.pem"

  if signing_key_pair_ok "${sign_key}" "${public_key}"; then
    echo "using default upgrade signing key: ${sign_key}" >&2
  else
    generate_default_signing_key_pair
    echo "generated default upgrade signing key: ${sign_key}" >&2
  fi

  echo "default upgrade public key: ${public_key}" >&2
  echo "deploy it to /config/upgrade_public_key.pem before using this package on a board" >&2
}

resolve_host_tool() {
  requested="$1"
  tool_name="$2"
  bundled_tool="${tools_dir}/${tool_name}"

  if [ -n "${requested}" ]; then
    if [ -x "${requested}" ]; then
      printf '%s\n' "${requested}"
      return 0
    fi
    if command -v "${requested}" >/dev/null 2>&1; then
      command -v "${requested}"
      return 0
    fi
    echo "missing tool: ${requested}" >&2
    exit 1
  fi

  if [ -x "${bundled_tool}" ]; then
    printf '%s\n' "${bundled_tool}"
    return 0
  fi
  if command -v "${tool_name}" >/dev/null 2>&1; then
    command -v "${tool_name}"
    return 0
  fi
  echo "missing tool: ${tool_name} (expected ${bundled_tool} or PATH)" >&2
  exit 1
}

resolve_strip_tool() {
  if [ -n "${STRIP:-}" ]; then
    resolve_host_tool "${STRIP}" strip
    return 0
  fi
  if command -v arm-himix200-linux-strip >/dev/null 2>&1; then
    command -v arm-himix200-linux-strip
    return 0
  fi
  return 1
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

file_size_bytes() {
  wc -c < "$1" | awk '{print $1}'
}

squashfs_image_ok() {
  image_path="$1"
  [ -s "${image_path}" ] || return 1
  image_magic=$(dd if="${image_path}" bs=4 count=1 2>/dev/null)
  [ "${image_magic}" = "hsqs" ]
}

build_squashfs_image() {
  input_root="$1"
  image_path="$2"
  partition="$3"

  if "${mksquashfs_bin}" "${input_root}" "${image_path}" \
      -noappend -comp xz -processors 1; then
    return 0
  fi
  if squashfs_image_ok "${image_path}"; then
    echo "warning: mksquashfs failed after writing valid ${partition} image; continuing" >&2
    return 0
  fi
  echo "failed to build ${partition} squashfs image: ${image_path}" >&2
  exit 1
}

strip_release_binary() {
  binary_path="$1"
  if command -v file >/dev/null 2>&1 && ! file "${binary_path}" | grep -q 'ELF'; then
    return 0
  fi
  if [ -z "${strip_bin}" ]; then
    echo "warning: strip tool not found; ${binary_path} keeps debug symbols" >&2
    return 0
  fi
  "${strip_bin}" "${binary_path}"
}

check_image_size() {
  image_path="$1"
  limit_bytes="$2"
  partition="$3"
  image_size=$(file_size_bytes "${image_path}")
  if [ "${image_size}" -gt "${limit_bytes}" ]; then
    echo "${partition} image too large: ${image_size} bytes > ${limit_bytes} bytes" >&2
    exit 1
  fi
}

append_install_command() {
  partition="$1"
  image_file="$2"
  image_sha="$3"

  if [ "${command_count}" -gt 0 ]; then
    printf ',\n' >> "${commands_file}"
  fi
  cat >> "${commands_file}" <<EOF
    {
      "Action": "burn",
      "Partition": "${partition}",
      "File": "${image_file}",
      "Sha256": "${image_sha}"
    }
EOF
  command_count=$((command_count + 1))
  zip_entries="${zip_entries} ${image_file}"
}

copy_release_inputs() {
  rm -rf "${release_dir}/bin" "${release_dir}/configs" \
    "${release_dir}/web" "${release_dir}/flash" "${work_dir}"
  mkdir -p "${release_dir}/bin" "${release_dir}/configs" \
    "${release_dir}/log" "${release_dir}/web" "${manifest_dir}"

  cp -f "${repo_root}/build/bin/live_stream" "${release_dir}/bin/"
  cp -f "${repo_root}/build/bin/live_sysupgrade" "${release_dir}/bin/"
  cp -f "${repo_root}"/configs/*.json "${release_dir}/configs/"
  if [ -f "${repo_root}/configs/upgrade_public_key.pem" ]; then
    cp -f "${repo_root}/configs/upgrade_public_key.pem" "${release_dir}/configs/"
  fi
  cp -rf "${repo_root}/www/dist/." "${release_dir}/web/"
}

strip_release_inputs() {
  if [ -n "${STRIP:-}" ]; then
    strip_bin=$(resolve_strip_tool)
  else
    strip_bin=$(resolve_strip_tool || true)
  fi
  strip_release_binary "${release_dir}/bin/live_stream"
  strip_release_binary "${release_dir}/bin/live_sysupgrade"
}

build_bin_image() {
  mkdir -p "${work_dir}/bin_root/bin" \
    "${work_dir}/bin_root/sbin" \
    "${work_dir}/bin_root/lib" \
    "${work_dir}/bin_root/scripts"
  cp -f "${release_dir}/bin/live_stream" "${work_dir}/bin_root/bin/"
  cp -f "${release_dir}/bin/live_sysupgrade" "${work_dir}/bin_root/sbin/"
  printf '%s\n' "${version}" > "${work_dir}/bin_root/version"
  build_squashfs_image "${work_dir}/bin_root" "${release_dir}/bin.squashfs" bin
  check_image_size "${release_dir}/bin.squashfs" "${bin_partition_size}" bin
}

build_web_image() {
  mkdir -p "${work_dir}/web_root"
  cp -rf "${release_dir}/web/." "${work_dir}/web_root/"
  printf '%s\n' "${version}" > "${work_dir}/web_root/version"
  build_squashfs_image "${work_dir}/web_root" "${release_dir}/web.squashfs" web
  check_image_size "${release_dir}/web.squashfs" "${web_partition_size}" web
}

build_config_image() {
  mkdir -p "${work_dir}/config_root"
  cp -rf "${release_dir}/configs/." "${work_dir}/config_root/"
  cp -f "${public_key}" "${work_dir}/config_root/upgrade_public_key.pem"
  "${mkfs_jffs2_bin}" -r "${work_dir}/config_root" \
    -o "${release_dir}/config.jffs2" \
    -e 0x10000 --pad=0x100000 -n
  check_image_size "${release_dir}/config.jffs2" "${config_partition_size}" config
}

write_install_manifest() {
  cat > "${manifest_dir}/Install" <<EOF
{
  "Version": "${version}",
  "Board": "Hi3516DV300",
  "Flash": "spi-nor-32m",
  "PackageType": "normal",
  "Reboot": ${requires_reboot},
  "Commands": [
$(cat "${commands_file}")
  ]
}
EOF
}

sign_install_manifest() {
  openssl dgst -sha256 -sign "${sign_key}" \
    -out "${manifest_dir}/Install.sig" "${manifest_dir}/Install"
  openssl dgst -sha256 -verify "${public_key}" \
    -signature "${manifest_dir}/Install.sig" "${manifest_dir}/Install" >/dev/null
}

write_upgrade_zip() {
  cp -f "${manifest_dir}/Install" "${release_dir}/Install"
  cp -f "${manifest_dir}/Install.sig" "${release_dir}/Install.sig"
  (cd "${release_dir}" && zip -0 -q -FS "upgrade-${profile}.zip" ${zip_entries})
  cp -f "${release_dir}/upgrade-${profile}.zip" "${release_dir}/upgrade.zip"
  rm -f "${release_dir}/Install" "${release_dir}/Install.sig"
}

cleanup_release_intermediates() {
  rm -rf "${release_dir}/bin" "${release_dir}/configs" \
    "${release_dir}/log" "${release_dir}/web" "${work_dir}" \
    "${release_dir}/flash"
  rm -f "${release_dir}/Install" "${release_dir}/Install.sig" \
    "${release_dir}/Install.commands"
}

release_dir=$(resolve_output_dir "${release_dir}")
mkdir -p "${release_dir}"
release_dir=$(CDPATH= cd -- "${release_dir}" && pwd -P)
work_dir="${release_dir}/.package_work"
manifest_dir="${work_dir}/manifest"

case "${release_dir}" in
  ""|"/")
    echo "invalid release output directory: ${release_dir}" >&2
    exit 1
    ;;
esac
if [ "${release_dir}" = "${repo_root}" ]; then
  echo "release output directory must not be the repository root" >&2
  exit 1
fi

include_bin=false
include_web=false
include_config=false
requires_reboot=false

case "${profile}" in
  all)
    include_bin=true
    include_web=true
    include_config=true
    requires_reboot=true
    ;;
  web)
    include_web=true
    ;;
  config)
    include_config=true
    requires_reboot=true
    ;;
  *)
    echo "usage: $0 [release_dir] [version] [all|web|config]" >&2
    exit 1
    ;;
esac

case "${version}" in
  *[!A-Za-z0-9._+-]*)
    echo "invalid version: ${version}" >&2
    exit 1
    ;;
esac

if [ "${include_bin}" = true ] || [ "${include_web}" = true ]; then
  mksquashfs_bin=$(resolve_host_tool "${MKSQUASHFS:-}" mksquashfs)
fi
if [ "${include_config}" = true ]; then
  mkfs_jffs2_bin=$(resolve_host_tool "${MKFS_JFFS2:-}" mkfs.jffs2)
fi
require_cmd zip
require_cmd openssl
require_cmd sha256sum
require_cmd awk

if [ -z "${sign_key}" ]; then
  prepare_default_signing_key
elif [ ! -f "${sign_key}" ]; then
  echo "missing upgrade signing key: ${sign_key}" >&2
  exit 1
fi
if [ -z "${public_key}" ]; then
  public_key="${default_public_key}"
fi
if [ ! -f "${public_key}" ]; then
  echo "missing upgrade public key: ${public_key}" >&2
  exit 1
fi

copy_release_inputs
strip_release_inputs

if [ "${include_bin}" = true ]; then
  build_bin_image
fi
if [ "${include_web}" = true ]; then
  build_web_image
fi
if [ "${include_config}" = true ]; then
  build_config_image
fi

command_count=0
commands_file="${manifest_dir}/Install.commands"
rm -f "${commands_file}"
touch "${commands_file}"
zip_entries="Install"

case "${profile}" in
  all)
    append_install_command bin bin.squashfs "$(sha256_file "${release_dir}/bin.squashfs")"
    append_install_command web web.squashfs "$(sha256_file "${release_dir}/web.squashfs")"
    append_install_command config config.jffs2 "$(sha256_file "${release_dir}/config.jffs2")"
    ;;
  web)
    append_install_command web web.squashfs "$(sha256_file "${release_dir}/web.squashfs")"
    ;;
  config)
    append_install_command config config.jffs2 "$(sha256_file "${release_dir}/config.jffs2")"
    ;;
esac

write_install_manifest
sign_install_manifest
zip_entries="${zip_entries} Install.sig"
write_upgrade_zip
cleanup_release_intermediates

echo "Upgrade package: ${release_dir}/upgrade-${profile}.zip"
echo "Default package: ${release_dir}/upgrade.zip"
echo "Profile: ${profile}"
echo "Release output: ${release_dir}"

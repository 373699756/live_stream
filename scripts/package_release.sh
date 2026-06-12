#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
release_dir="${1:-${repo_root}/release}"
version="${2:-1.0.0}"
profile="${3:-web-only}"
tools_dir="${repo_root}/tools/pc"
default_public_key="${repo_root}/configs/upgrade_public_key.pem"
sign_key="${UPGRADE_SIGN_KEY:-}"
public_key="${UPGRADE_PUBLIC_KEY:-}"

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

prepare_development_signing_key() {
  if [ -n "${public_key}" ]; then
    echo "UPGRADE_PUBLIC_KEY requires a matching UPGRADE_SIGN_KEY" >&2
    exit 1
  fi

  signing_dir=$(resolve_output_dir "${RELEASE_SIGNING_DIR:-build/release_signing}")
  mkdir -p "${signing_dir}"

  sign_key="${signing_dir}/development_upgrade_private_key.pem"
  public_key="${signing_dir}/development_upgrade_public_key.pem"

  if [ ! -f "${sign_key}" ]; then
    old_umask=$(umask)
    umask 077
    openssl genrsa -out "${sign_key}" 2048 >/dev/null 2>&1
    umask "${old_umask}"
    echo "generated development upgrade signing key: ${sign_key}" >&2
  else
    echo "using development upgrade signing key: ${sign_key}" >&2
  fi

  openssl rsa -in "${sign_key}" -pubout -out "${public_key}" >/dev/null 2>&1
  chmod 600 "${sign_key}" 2>/dev/null || true

  echo "development upgrade public key: ${public_key}" >&2
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

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
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
    "${release_dir}/web" "${flash_dir}"
  mkdir -p "${release_dir}/bin" "${release_dir}/configs" \
    "${release_dir}/log" "${release_dir}/web" "${flash_dir}"

  cp -f "${repo_root}/build/bin/live_stream" "${release_dir}/bin/"
  cp -f "${repo_root}/build/bin/live_sysupgrade" "${release_dir}/bin/"
  cp -f "${repo_root}"/configs/*.json "${release_dir}/configs/"
  if [ -f "${repo_root}/configs/upgrade_public_key.pem" ]; then
    cp -f "${repo_root}/configs/upgrade_public_key.pem" \
      "${release_dir}/configs/"
  fi
  cp -rf "${repo_root}/www/dist/." "${release_dir}/web/"
}

build_bin_image() {
  mkdir -p "${flash_dir}/bin_root/bin" \
    "${flash_dir}/bin_root/sbin" \
    "${flash_dir}/bin_root/lib" \
    "${flash_dir}/bin_root/scripts"
  cp -f "${release_dir}/bin/live_stream" "${flash_dir}/bin_root/bin/"
  cp -f "${release_dir}/bin/live_sysupgrade" "${flash_dir}/bin_root/sbin/"
  printf '%s\n' "${version}" > "${flash_dir}/bin_root/version"
  "${mksquashfs_bin}" "${flash_dir}/bin_root" "${flash_dir}/bin.squashfs" \
    -noappend -comp xz
}

build_web_image() {
  mkdir -p "${flash_dir}/web_root"
  cp -rf "${release_dir}/web/." "${flash_dir}/web_root/"
  printf '%s\n' "${version}" > "${flash_dir}/web_root/version"
  "${mksquashfs_bin}" "${flash_dir}/web_root" "${flash_dir}/web.squashfs" \
    -noappend -comp xz
}

build_config_image() {
  mkdir -p "${flash_dir}/config_root"
  cp -rf "${release_dir}/configs/." "${flash_dir}/config_root/"
  cp -f "${public_key}" "${flash_dir}/config_root/upgrade_public_key.pem"
  "${mkfs_jffs2_bin}" -r "${flash_dir}/config_root" \
    -o "${flash_dir}/config.jffs2" \
    -e 0x10000 --pad=0x100000 -n
}

write_install_manifest() {
  cat > "${flash_dir}/Install" <<EOF
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
    -out "${flash_dir}/Install.sig" "${flash_dir}/Install"
  openssl dgst -sha256 -verify "${public_key}" \
    -signature "${flash_dir}/Install.sig" "${flash_dir}/Install" >/dev/null
}

write_upgrade_zip() {
  (cd "${flash_dir}" && zip -0 -q -FS "upgrade-${profile}.zip" ${zip_entries})
  cp -f "${flash_dir}/upgrade-${profile}.zip" "${flash_dir}/upgrade.zip"
}

release_dir=$(resolve_output_dir "${release_dir}")
mkdir -p "${release_dir}"
release_dir=$(CDPATH= cd -- "${release_dir}" && pwd -P)
flash_dir="${release_dir}/flash"

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
  web-only)
    include_web=true
    ;;
  bin-web)
    include_bin=true
    include_web=true
    requires_reboot=true
    ;;
  config-only)
    include_config=true
    requires_reboot=true
    ;;
  kernel-rootfs|full)
    echo "${profile} profile is disabled: rootfs online upgrade is unsafe" >&2
    exit 1
    ;;
  *)
    echo "usage: $0 [release_dir] [version] [web-only|bin-web|config-only|kernel-rootfs|full]" >&2
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
  prepare_development_signing_key
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
commands_file="${flash_dir}/Install.commands"
rm -f "${commands_file}"
touch "${commands_file}"
zip_entries="Install"

case "${profile}" in
  web-only)
    append_install_command web web.squashfs "$(sha256_file "${flash_dir}/web.squashfs")"
    ;;
  bin-web)
    append_install_command bin bin.squashfs "$(sha256_file "${flash_dir}/bin.squashfs")"
    append_install_command web web.squashfs "$(sha256_file "${flash_dir}/web.squashfs")"
    ;;
  config-only)
    append_install_command config config.jffs2 "$(sha256_file "${flash_dir}/config.jffs2")"
    ;;
esac

write_install_manifest
sign_install_manifest
zip_entries="${zip_entries} Install.sig"
write_upgrade_zip
rm -f "${commands_file}"

echo "Upgrade package: ${flash_dir}/upgrade-${profile}.zip"
echo "Default package: ${flash_dir}/upgrade.zip"
echo "Profile: ${profile}"
echo "Release output: ${release_dir}"

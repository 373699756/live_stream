#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${ROOT_DIR}/out"
FLASH_DIR="${OUT_DIR}/flash"
TOOLS_DIR="${ROOT_DIR}/tools/pc"
VERSION="${1:-1.0.0}"
PACKAGE_PROFILE="${2:-web-only}"
UPGRADE_SIGN_KEY="${UPGRADE_SIGN_KEY:-}"
UPGRADE_PUBLIC_KEY="${UPGRADE_PUBLIC_KEY:-${ROOT_DIR}/configs/upgrade_public_key.pem}"

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing tool: $1" >&2
    exit 1
  fi
}

resolve_local_tool() {
  requested="$1"
  fallback_name="$2"
  local_path="${TOOLS_DIR}/${fallback_name}"

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

  if [ -x "${local_path}" ]; then
    printf '%s\n' "${local_path}"
    return 0
  fi
  if command -v "${fallback_name}" >/dev/null 2>&1; then
    command -v "${fallback_name}"
    return 0
  fi
  echo "missing tool: ${fallback_name} (expected ${local_path} or PATH)" >&2
  exit 1
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

need_bin=false
need_web=false
need_config=false
reboot="false"

case "${PACKAGE_PROFILE}" in
  web-only)
    need_web=true
    ;;
  bin-web)
    need_bin=true
    need_web=true
    reboot="true"
    ;;
  config-only)
    need_config=true
    reboot="true"
    ;;
  kernel-rootfs)
    echo "kernel-rootfs profile is disabled: rootfs online upgrade is unsafe" >&2
    exit 1
    ;;
  full)
    echo "full profile is disabled: rootfs online upgrade is unsafe" >&2
    exit 1
    ;;
  *)
    echo "usage: $0 [version] [web-only|bin-web|config-only|kernel-rootfs|full]" >&2
    exit 1
    ;;
esac

if [ "${need_bin}" = true ] || [ "${need_web}" = true ]; then
  MKSQUASHFS_TOOL=$(resolve_local_tool "${MKSQUASHFS:-}" mksquashfs)
fi
if [ "${need_config}" = true ]; then
  MKFS_JFFS2_TOOL=$(resolve_local_tool "${MKFS_JFFS2:-}" mkfs.jffs2)
fi
need_tool zip
need_tool openssl
need_tool sha256sum
need_tool awk
case "${VERSION}" in
  *[!A-Za-z0-9._+-]*)
    echo "invalid version: ${VERSION}" >&2
    exit 1
    ;;
esac

mkdir -p "${FLASH_DIR}"

if [ -z "${UPGRADE_SIGN_KEY}" ] || [ ! -f "${UPGRADE_SIGN_KEY}" ]; then
  echo "set UPGRADE_SIGN_KEY=/path/to/private_key.pem to sign Install" >&2
  exit 1
fi
if [ ! -f "${UPGRADE_PUBLIC_KEY}" ]; then
  echo "missing upgrade public key: ${UPGRADE_PUBLIC_KEY}" >&2
  exit 1
fi

if [ "${need_bin}" = true ]; then
  mkdir -p "${FLASH_DIR}/bin_root/bin" \
    "${FLASH_DIR}/bin_root/sbin" \
    "${FLASH_DIR}/bin_root/lib" \
    "${FLASH_DIR}/bin_root/scripts"
  cp -f "${OUT_DIR}/bin/live_stream" "${FLASH_DIR}/bin_root/bin/"
  cp -f "${OUT_DIR}/bin/live_sysupgrade" "${FLASH_DIR}/bin_root/sbin/"
  printf '%s\n' "${VERSION}" > "${FLASH_DIR}/bin_root/version"
  "${MKSQUASHFS_TOOL}" "${FLASH_DIR}/bin_root" "${FLASH_DIR}/bin.squashfs" \
    -noappend -comp xz
fi

if [ "${need_web}" = true ]; then
  rm -rf "${FLASH_DIR}/web_root"
  mkdir -p "${FLASH_DIR}/web_root"
  if [ -d "${OUT_DIR}/web" ]; then
    cp -rf "${OUT_DIR}/web/." "${FLASH_DIR}/web_root/"
  fi
  printf '%s\n' "${VERSION}" > "${FLASH_DIR}/web_root/version"
  "${MKSQUASHFS_TOOL}" "${FLASH_DIR}/web_root" "${FLASH_DIR}/web.squashfs" \
    -noappend -comp xz
fi

if [ "${need_config}" = true ]; then
  rm -rf "${FLASH_DIR}/config_root"
  mkdir -p "${FLASH_DIR}/config_root"
  if [ -d "${OUT_DIR}/configs" ]; then
    cp -rf "${OUT_DIR}/configs/." "${FLASH_DIR}/config_root/"
  fi
  cp -f "${UPGRADE_PUBLIC_KEY}" "${FLASH_DIR}/config_root/upgrade_public_key.pem"
  "${MKFS_JFFS2_TOOL}" -r "${FLASH_DIR}/config_root" \
    -o "${FLASH_DIR}/config.jffs2" \
    -e 0x10000 --pad=0x100000 -n
fi

add_command() {
  partition="$1"
  file="$2"
  sha="$3"
  if [ "${command_count}" -gt 0 ]; then
    printf ',\n' >> "${INSTALL_TMP}"
  fi
  cat >> "${INSTALL_TMP}" <<EOF
    {
      "Action": "burn",
      "Partition": "${partition}",
      "File": "${file}",
      "Sha256": "${sha}"
    }
EOF
  command_count=$((command_count + 1))
  zip_inputs="${zip_inputs} ${file}"
}

INSTALL_TMP="${FLASH_DIR}/Install.commands"
rm -f "${INSTALL_TMP}"
touch "${INSTALL_TMP}"
command_count=0
zip_inputs="Install"

case "${PACKAGE_PROFILE}" in
  web-only)
    add_command web web.squashfs "$(sha256_file "${FLASH_DIR}/web.squashfs")"
    ;;
  bin-web)
    add_command bin bin.squashfs "$(sha256_file "${FLASH_DIR}/bin.squashfs")"
    add_command web web.squashfs "$(sha256_file "${FLASH_DIR}/web.squashfs")"
    ;;
  config-only)
    add_command config config.jffs2 "$(sha256_file "${FLASH_DIR}/config.jffs2")"
    ;;
  kernel-rootfs)
    echo "kernel-rootfs profile is disabled: rootfs online upgrade is unsafe" >&2
    exit 1
    ;;
  full)
    echo "full profile is disabled: rootfs online upgrade is unsafe" >&2
    exit 1
    ;;
  *)
    echo "usage: $0 [version] [web-only|bin-web|config-only|kernel-rootfs|full]" >&2
    exit 1
    ;;
esac

cat > "${FLASH_DIR}/Install" <<EOF
{
  "Version": "${VERSION}",
  "Board": "Hi3516DV300",
  "Flash": "spi-nor-32m",
  "PackageType": "normal",
  "Reboot": ${reboot},
  "Commands": [
$(cat "${INSTALL_TMP}")
  ]
}
EOF

openssl dgst -sha256 -sign "${UPGRADE_SIGN_KEY}" \
  -out "${FLASH_DIR}/Install.sig" "${FLASH_DIR}/Install"
openssl dgst -sha256 -verify "${UPGRADE_PUBLIC_KEY}" \
  -signature "${FLASH_DIR}/Install.sig" "${FLASH_DIR}/Install" >/dev/null
zip_inputs="${zip_inputs} Install.sig"

(cd "${FLASH_DIR}" && zip -0 -q -FS "upgrade-${PACKAGE_PROFILE}.zip" ${zip_inputs})
cp -f "${FLASH_DIR}/upgrade-${PACKAGE_PROFILE}.zip" "${FLASH_DIR}/upgrade.zip"
rm -f "${INSTALL_TMP}"

echo "Upgrade package: ${FLASH_DIR}/upgrade-${PACKAGE_PROFILE}.zip"
echo "Default package: ${FLASH_DIR}/upgrade.zip"
echo "Profile: ${PACKAGE_PROFILE}"

#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${ROOT_DIR}/out"
FLASH_DIR="${OUT_DIR}/flash"
VERSION="${1:-1.0.0}"
PACKAGE_PROFILE="${2:-web-only}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${OUT_DIR}/flash/uImage_hi3516dv300}"
ROOTFS_IMAGE="${ROOTFS_IMAGE:-${OUT_DIR}/flash/rootfs_hi3516dv300_64k.jffs2}"

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing tool: $1" >&2
    exit 1
  fi
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

need_bin=false
need_web=false
need_config=false
need_kernel=false
need_rootfs=false
need_helper=false
reboot="false"

case "${PACKAGE_PROFILE}" in
  web-only)
    need_web=true
    ;;
  bin-web)
    need_bin=true
    need_web=true
    need_helper=true
    reboot="true"
    ;;
  config-only)
    need_config=true
    need_helper=true
    reboot="true"
    ;;
  kernel-rootfs)
    need_kernel=true
    need_rootfs=true
    need_helper=true
    reboot="true"
    ;;
  full)
    need_bin=true
    need_web=true
    need_config=true
    need_kernel=true
    need_rootfs=true
    need_helper=true
    reboot="true"
    ;;
  *)
    echo "usage: $0 [version] [web-only|bin-web|config-only|kernel-rootfs|full]" >&2
    exit 1
    ;;
esac

if [ "${need_bin}" = true ] || [ "${need_web}" = true ]; then
  need_tool mksquashfs
fi
if [ "${need_config}" = true ]; then
  need_tool mkfs.jffs2
fi
need_tool zip
need_tool sha256sum
need_tool awk

mkdir -p "${FLASH_DIR}"

if [ "${need_helper}" = true ]; then
  if [ ! -f "${OUT_DIR}/bin/live_sysupgrade" ]; then
    echo "missing helper: ${OUT_DIR}/bin/live_sysupgrade" >&2
    exit 1
  fi
  cp -f "${OUT_DIR}/bin/live_sysupgrade" "${FLASH_DIR}/live_sysupgrade"
fi

if [ "${need_bin}" = true ]; then
  mkdir -p "${FLASH_DIR}/bin_root/bin" \
    "${FLASH_DIR}/bin_root/sbin" \
    "${FLASH_DIR}/bin_root/lib" \
    "${FLASH_DIR}/bin_root/scripts"
  cp -f "${OUT_DIR}/bin/live_stream" "${FLASH_DIR}/bin_root/bin/"
  cp -f "${OUT_DIR}/bin/live_sysupgrade" "${FLASH_DIR}/bin_root/sbin/"
  printf '%s\n' "${VERSION}" > "${FLASH_DIR}/bin_root/version"
  mksquashfs "${FLASH_DIR}/bin_root" "${FLASH_DIR}/bin.squashfs" \
    -noappend -comp xz
fi

if [ "${need_web}" = true ]; then
  rm -rf "${FLASH_DIR}/web_root"
  mkdir -p "${FLASH_DIR}/web_root"
  if [ -d "${OUT_DIR}/web" ]; then
    cp -rf "${OUT_DIR}/web/." "${FLASH_DIR}/web_root/"
  fi
  printf '%s\n' "${VERSION}" > "${FLASH_DIR}/web_root/version"
  mksquashfs "${FLASH_DIR}/web_root" "${FLASH_DIR}/web.squashfs" \
    -noappend -comp xz
fi

if [ "${need_config}" = true ]; then
  rm -rf "${FLASH_DIR}/config_root"
  mkdir -p "${FLASH_DIR}/config_root"
  if [ -d "${OUT_DIR}/configs" ]; then
    cp -rf "${OUT_DIR}/configs/." "${FLASH_DIR}/config_root/"
  fi
  mkfs.jffs2 -r "${FLASH_DIR}/config_root" \
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

copy_optional_image() {
  source_path="$1"
  file_name="$2"
  if [ ! -f "${source_path}" ]; then
    echo "missing image for ${file_name}: ${source_path}" >&2
    exit 1
  fi
  cp -f "${source_path}" "${FLASH_DIR}/${file_name}"
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
    zip_inputs="${zip_inputs} live_sysupgrade"
    ;;
  config-only)
    add_command config config.jffs2 "$(sha256_file "${FLASH_DIR}/config.jffs2")"
    zip_inputs="${zip_inputs} live_sysupgrade"
    ;;
  kernel-rootfs)
    copy_optional_image "${KERNEL_IMAGE}" uImage_hi3516dv300
    copy_optional_image "${ROOTFS_IMAGE}" rootfs_hi3516dv300_64k.jffs2
    add_command kernel uImage_hi3516dv300 \
      "$(sha256_file "${FLASH_DIR}/uImage_hi3516dv300")"
    add_command rootfs rootfs_hi3516dv300_64k.jffs2 \
      "$(sha256_file "${FLASH_DIR}/rootfs_hi3516dv300_64k.jffs2")"
    zip_inputs="${zip_inputs} live_sysupgrade"
    ;;
  full)
    copy_optional_image "${KERNEL_IMAGE}" uImage_hi3516dv300
    copy_optional_image "${ROOTFS_IMAGE}" rootfs_hi3516dv300_64k.jffs2
    add_command kernel uImage_hi3516dv300 \
      "$(sha256_file "${FLASH_DIR}/uImage_hi3516dv300")"
    add_command rootfs rootfs_hi3516dv300_64k.jffs2 \
      "$(sha256_file "${FLASH_DIR}/rootfs_hi3516dv300_64k.jffs2")"
    add_command bin bin.squashfs "$(sha256_file "${FLASH_DIR}/bin.squashfs")"
    add_command web web.squashfs "$(sha256_file "${FLASH_DIR}/web.squashfs")"
    add_command config config.jffs2 "$(sha256_file "${FLASH_DIR}/config.jffs2")"
    zip_inputs="${zip_inputs} live_sysupgrade"
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

(cd "${FLASH_DIR}" && zip -0 -q -FS "upgrade-${PACKAGE_PROFILE}.zip" ${zip_inputs})
cp -f "${FLASH_DIR}/upgrade-${PACKAGE_PROFILE}.zip" "${FLASH_DIR}/upgrade.zip"
rm -f "${INSTALL_TMP}"

echo "Upgrade package: ${FLASH_DIR}/upgrade-${PACKAGE_PROFILE}.zip"
echo "Default package: ${FLASH_DIR}/upgrade.zip"
echo "Profile: ${PACKAGE_PROFILE}"

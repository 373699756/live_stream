#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${ROOT_DIR}/out"
FLASH_DIR="${OUT_DIR}/flash"
VERSION="${1:-1.0.0}"

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing tool: $1" >&2
    exit 1
  fi
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

need_tool mksquashfs
need_tool mkfs.jffs2
need_tool zip
need_tool sha256sum
need_tool awk

mkdir -p "${FLASH_DIR}/bin_root/bin" \
  "${FLASH_DIR}/bin_root/lib" \
  "${FLASH_DIR}/bin_root/scripts" \
  "${FLASH_DIR}/web_root" \
  "${FLASH_DIR}/config_root"

cp -f "${OUT_DIR}/bin/live_stream" "${FLASH_DIR}/bin_root/bin/"
if [ -d "${OUT_DIR}/web" ]; then
  rm -rf "${FLASH_DIR}/web_root"
  mkdir -p "${FLASH_DIR}/web_root"
  cp -rf "${OUT_DIR}/web/." "${FLASH_DIR}/web_root/"
fi
if [ -d "${OUT_DIR}/configs" ]; then
  rm -rf "${FLASH_DIR}/config_root"
  mkdir -p "${FLASH_DIR}/config_root"
  cp -rf "${OUT_DIR}/configs/." "${FLASH_DIR}/config_root/"
fi

printf '%s\n' "${VERSION}" > "${FLASH_DIR}/bin_root/version"
printf '%s\n' "${VERSION}" > "${FLASH_DIR}/web_root/version"

mksquashfs "${FLASH_DIR}/bin_root" "${FLASH_DIR}/bin.squashfs" \
  -noappend -comp xz
mksquashfs "${FLASH_DIR}/web_root" "${FLASH_DIR}/web.squashfs" \
  -noappend -comp xz
mkfs.jffs2 -r "${FLASH_DIR}/config_root" \
  -o "${FLASH_DIR}/config.jffs2" \
  -e 0x10000 --pad=0x100000 -n

cat > "${FLASH_DIR}/Install" <<EOF
{
  "Version": "${VERSION}",
  "Board": "Hi3516DV300",
  "Flash": "spi-nor-32m",
  "PackageType": "normal",
  "Reboot": false,
  "Commands": [
    {
      "Action": "burn",
      "Partition": "web",
      "File": "web.squashfs",
      "Sha256": "$(sha256_file "${FLASH_DIR}/web.squashfs")"
    }
  ]
}
EOF

(cd "${FLASH_DIR}" && zip -0 -q -FS upgrade.zip Install web.squashfs)

echo "Upgrade package: ${FLASH_DIR}/upgrade.zip"
echo "Images: ${FLASH_DIR}/bin.squashfs ${FLASH_DIR}/web.squashfs ${FLASH_DIR}/config.jffs2"

#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SCRIPT="${ROOT_DIR}/scripts/package_upgrade.sh"
TEST_DIR="${TMPDIR:-/tmp}/live_stream_package_upgrade_test_$$"
OUT_BACKUP_DIR="${TEST_DIR}/out_backup"
KEY_PATH="${TEST_DIR}/upgrade_private_key.pem"
PUBLIC_KEY_PATH="${TEST_DIR}/upgrade_public_key.pem"
FAKE_TOOLS_DIR="${TEST_DIR}/tools"
REJECT_LOG="${TEST_DIR}/reject.log"

cleanup() {
  if [ -d "${OUT_BACKUP_DIR}" ]; then
    rm -rf "${ROOT_DIR}/out"
    mv "${OUT_BACKUP_DIR}" "${ROOT_DIR}/out"
  else
    rm -rf "${ROOT_DIR}/out"
  fi
  rm -rf "${TEST_DIR}"
}
trap cleanup EXIT INT TERM

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_tool() {
  command -v "$1" >/dev/null 2>&1 || fail "missing tool: $1"
}

make_fake_mksquashfs() {
  cat > "${FAKE_TOOLS_DIR}/mksquashfs" <<'EOF'
#!/bin/sh
set -eu
out="$2"
printf 'hsqs-test-image\n' > "${out}"
EOF
  chmod +x "${FAKE_TOOLS_DIR}/mksquashfs"
}

make_fake_mkfs_jffs2() {
  cat > "${FAKE_TOOLS_DIR}/mkfs.jffs2" <<'EOF'
#!/bin/sh
set -eu
out=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o)
      shift
      out="$1"
      ;;
  esac
  shift || true
done
[ -n "${out}" ] || exit 2
printf '\205\031test-jffs2-image\n' > "${out}"
EOF
  chmod +x "${FAKE_TOOLS_DIR}/mkfs.jffs2"
}

assert_zip_entries() {
  zip_path="$1"
  expected="$2"
  actual=$(zipinfo -1 "${zip_path}" | sort | tr '\n' ' ')
  [ "${actual}" = "${expected}" ] || fail "unexpected zip entries: ${actual}"
}

assert_install_signature_ok() {
  zip_path="$1"
  work_dir="${TEST_DIR}/verify"
  rm -rf "${work_dir}"
  mkdir -p "${work_dir}"
  unzip -q "${zip_path}" Install Install.sig -d "${work_dir}"
  openssl dgst -sha256 -verify "${PUBLIC_KEY_PATH}" \
    -signature "${work_dir}/Install.sig" "${work_dir}/Install" >/dev/null
}

require_tool openssl
require_tool unzip
require_tool zipinfo

rm -rf "${TEST_DIR}"
mkdir -p "${TEST_DIR}" "${FAKE_TOOLS_DIR}"
if [ -d "${ROOT_DIR}/out" ]; then
  rm -rf "${OUT_BACKUP_DIR}"
  cp -a "${ROOT_DIR}/out" "${OUT_BACKUP_DIR}"
fi
rm -rf "${ROOT_DIR}/out"
mkdir -p "${ROOT_DIR}/out/bin" "${ROOT_DIR}/out/web" \
  "${ROOT_DIR}/out/configs"
make_fake_mksquashfs
make_fake_mkfs_jffs2

printf 'live_stream\n' > "${ROOT_DIR}/out/bin/live_stream"
printf 'live_sysupgrade\n' > "${ROOT_DIR}/out/bin/live_sysupgrade"
printf 'index\n' > "${ROOT_DIR}/out/web/index.html"
printf '{"ok":true}\n' > "${ROOT_DIR}/out/configs/default_config.json"

openssl genrsa -out "${KEY_PATH}" 2048 >/dev/null 2>&1
openssl rsa -in "${KEY_PATH}" -pubout -out "${PUBLIC_KEY_PATH}" >/dev/null 2>&1

UPGRADE_SIGN_KEY="${KEY_PATH}" UPGRADE_PUBLIC_KEY="${PUBLIC_KEY_PATH}" \
  MKSQUASHFS="${FAKE_TOOLS_DIR}/mksquashfs" \
  "${SCRIPT}" 9.9.1 web-only >/dev/null
assert_zip_entries "${ROOT_DIR}/out/flash/upgrade-web-only.zip" \
  "Install Install.sig web.squashfs "
assert_install_signature_ok "${ROOT_DIR}/out/flash/upgrade-web-only.zip"
if unzip -p "${ROOT_DIR}/out/flash/upgrade-web-only.zip" Install |
    grep -q '"Partition": "rootfs"'; then
  fail "web-only package declared rootfs"
fi

UPGRADE_SIGN_KEY="${KEY_PATH}" UPGRADE_PUBLIC_KEY="${PUBLIC_KEY_PATH}" \
  MKSQUASHFS="${FAKE_TOOLS_DIR}/mksquashfs" \
  "${SCRIPT}" 9.9.2 bin-web >/dev/null
assert_zip_entries "${ROOT_DIR}/out/flash/upgrade-bin-web.zip" \
  "Install Install.sig bin.squashfs web.squashfs "
if unzip -l "${ROOT_DIR}/out/flash/upgrade-bin-web.zip" |
    grep -q 'live_sysupgrade'; then
  fail "package must not carry live_sysupgrade as executable entry"
fi

UPGRADE_SIGN_KEY="${KEY_PATH}" UPGRADE_PUBLIC_KEY="${PUBLIC_KEY_PATH}" \
  MKFS_JFFS2="${FAKE_TOOLS_DIR}/mkfs.jffs2" \
  "${SCRIPT}" 9.9.3 config-only >/dev/null
assert_zip_entries "${ROOT_DIR}/out/flash/upgrade-config-only.zip" \
  "Install Install.sig config.jffs2 "
cmp -s "${PUBLIC_KEY_PATH}" \
  "${ROOT_DIR}/out/flash/config_root/upgrade_public_key.pem" ||
  fail "config-only package did not stage public key"

if UPGRADE_SIGN_KEY="${KEY_PATH}" UPGRADE_PUBLIC_KEY="${PUBLIC_KEY_PATH}" \
    "${SCRIPT}" 9.9.4 kernel-rootfs >"${REJECT_LOG}" 2>&1; then
  fail "kernel-rootfs profile should be rejected"
fi
grep -q 'rootfs online upgrade is unsafe' "${REJECT_LOG}" ||
  fail "kernel-rootfs rejection reason missing"

if UPGRADE_SIGN_KEY="${KEY_PATH}" UPGRADE_PUBLIC_KEY="${PUBLIC_KEY_PATH}" \
    "${SCRIPT}" 9.9.5 full >"${REJECT_LOG}" 2>&1; then
  fail "full profile should be rejected"
fi
grep -q 'rootfs online upgrade is unsafe' "${REJECT_LOG}" ||
  fail "full rejection reason missing"

echo "package_upgrade_test: ok"

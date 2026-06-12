#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
release_script="${repo_root}/scripts/package_release.sh"
test_dir="${TMPDIR:-/tmp}/live_stream_package_release_test_$$"
release_dir="${test_dir}/release"
key_dir="${repo_root}/build/package_release_test_signing"
sign_key="${key_dir}/upgrade_private_key.pem"
public_key="${key_dir}/upgrade_public_key.pem"
auto_signing_dir="${key_dir}/auto"
fake_tools_dir="${test_dir}/tools"
reject_log="${test_dir}/reject.log"
build_bin_backup="${test_dir}/build_bin_backup"
web_dist_backup="${test_dir}/web_dist_backup"
had_build_bin=false
had_web_dist=false

cleanup() {
  rm -rf "${key_dir}"
  rm -rf "${repo_root}/build/bin" "${repo_root}/www/dist"
  if [ "${had_build_bin}" = true ]; then
    mkdir -p "${repo_root}/build"
    mv "${build_bin_backup}" "${repo_root}/build/bin"
  fi
  if [ "${had_web_dist}" = true ]; then
    mkdir -p "${repo_root}/www"
    mv "${web_dist_backup}" "${repo_root}/www/dist"
  fi
  rm -rf "${test_dir}"
}
trap cleanup EXIT INT TERM

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing tool: $1"
}

make_fake_mksquashfs() {
  cat > "${fake_tools_dir}/mksquashfs" <<'EOF'
#!/bin/sh
set -eu
image_file="$2"
printf 'hsqs-test-image\n' > "${image_file}"
EOF
  chmod +x "${fake_tools_dir}/mksquashfs"
}

make_fake_mkfs_jffs2() {
  cat > "${fake_tools_dir}/mkfs.jffs2" <<'EOF'
#!/bin/sh
set -eu
image_file=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o)
      shift
      image_file="$1"
      ;;
  esac
  shift || true
done
[ -n "${image_file}" ] || exit 2
printf '\205\031test-jffs2-image\n' > "${image_file}"
EOF
  chmod +x "${fake_tools_dir}/mkfs.jffs2"
}

stage_fake_build_inputs() {
  if [ -d "${repo_root}/build/bin" ]; then
    had_build_bin=true
    mv "${repo_root}/build/bin" "${build_bin_backup}"
  fi
  if [ -d "${repo_root}/www/dist" ]; then
    had_web_dist=true
    mv "${repo_root}/www/dist" "${web_dist_backup}"
  fi
  mkdir -p "${repo_root}/build/bin" "${repo_root}/www/dist"

  printf 'live_stream\n' > "${repo_root}/build/bin/live_stream"
  printf 'live_sysupgrade\n' > "${repo_root}/build/bin/live_sysupgrade"
  printf 'index\n' > "${repo_root}/www/dist/index.html"
}

assert_zip_entries() {
  zip_file="$1"
  expected="$2"
  actual=$(zipinfo -1 "${zip_file}" | sort | tr '\n' ' ')
  [ "${actual}" = "${expected}" ] || fail "unexpected zip entries: ${actual}"
}

assert_install_signature_ok() {
  zip_file="$1"
  verify_public_key="${2:-${public_key}}"
  verify_dir="${test_dir}/verify"
  rm -rf "${verify_dir}"
  mkdir -p "${verify_dir}"
  unzip -q "${zip_file}" Install Install.sig -d "${verify_dir}"
  openssl dgst -sha256 -verify "${verify_public_key}" \
    -signature "${verify_dir}/Install.sig" "${verify_dir}/Install" >/dev/null
}

require_cmd openssl
require_cmd unzip
require_cmd zipinfo

rm -rf "${test_dir}"
rm -rf "${key_dir}"
mkdir -p "${test_dir}" "${fake_tools_dir}" "${key_dir}"
make_fake_mksquashfs
make_fake_mkfs_jffs2
stage_fake_build_inputs

openssl genrsa -out "${sign_key}" 2048 >/dev/null 2>&1
openssl rsa -in "${sign_key}" -pubout -out "${public_key}" >/dev/null 2>&1

(
  unset UPGRADE_SIGN_KEY
  unset UPGRADE_PUBLIC_KEY
  RELEASE_SIGNING_DIR="${auto_signing_dir}" \
  MKSQUASHFS="${fake_tools_dir}/mksquashfs" \
    "${release_script}" "${release_dir}" 9.9.0 web-only >/dev/null
)
assert_zip_entries "${release_dir}/flash/upgrade-web-only.zip" \
  "Install Install.sig web.squashfs "
assert_install_signature_ok "${release_dir}/flash/upgrade-web-only.zip" \
  "${auto_signing_dir}/development_upgrade_public_key.pem"
[ -f "${auto_signing_dir}/development_upgrade_private_key.pem" ] ||
  fail "auto signing key was not generated"

UPGRADE_SIGN_KEY="${sign_key}" UPGRADE_PUBLIC_KEY="${public_key}" \
  MKSQUASHFS="${fake_tools_dir}/mksquashfs" \
  "${release_script}" "${release_dir}" 9.9.1 web-only >/dev/null
assert_zip_entries "${release_dir}/flash/upgrade-web-only.zip" \
  "Install Install.sig web.squashfs "
assert_install_signature_ok "${release_dir}/flash/upgrade-web-only.zip"
if unzip -p "${release_dir}/flash/upgrade-web-only.zip" Install |
    grep -q '"Partition": "rootfs"'; then
  fail "web-only package declared rootfs"
fi

UPGRADE_SIGN_KEY="${sign_key}" UPGRADE_PUBLIC_KEY="${public_key}" \
  MKSQUASHFS="${fake_tools_dir}/mksquashfs" \
  "${release_script}" "${release_dir}" 9.9.2 bin-web >/dev/null
assert_zip_entries "${release_dir}/flash/upgrade-bin-web.zip" \
  "Install Install.sig bin.squashfs web.squashfs "
if unzip -l "${release_dir}/flash/upgrade-bin-web.zip" |
    grep -q 'live_sysupgrade'; then
  fail "package must not carry live_sysupgrade as executable entry"
fi

UPGRADE_SIGN_KEY="${sign_key}" UPGRADE_PUBLIC_KEY="${public_key}" \
  MKFS_JFFS2="${fake_tools_dir}/mkfs.jffs2" \
  "${release_script}" "${release_dir}" 9.9.3 config-only >/dev/null
assert_zip_entries "${release_dir}/flash/upgrade-config-only.zip" \
  "Install Install.sig config.jffs2 "
cmp -s "${public_key}" \
  "${release_dir}/flash/config_root/upgrade_public_key.pem" ||
  fail "config-only package did not stage public key"

if UPGRADE_SIGN_KEY="${sign_key}" UPGRADE_PUBLIC_KEY="${public_key}" \
    "${release_script}" "${release_dir}" 9.9.4 kernel-rootfs \
    >"${reject_log}" 2>&1; then
  fail "kernel-rootfs profile should be rejected"
fi
grep -q 'rootfs online upgrade is unsafe' "${reject_log}" ||
  fail "kernel-rootfs rejection reason missing"

if UPGRADE_SIGN_KEY="${sign_key}" UPGRADE_PUBLIC_KEY="${public_key}" \
    "${release_script}" "${release_dir}" 9.9.5 full \
    >"${reject_log}" 2>&1; then
  fail "full profile should be rejected"
fi
grep -q 'rootfs online upgrade is unsafe' "${reject_log}" ||
  fail "full rejection reason missing"

echo "package_release_test: ok"

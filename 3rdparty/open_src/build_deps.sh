#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SRC_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${ROOT_DIR}/install"
INSTALL_LIB_DIR="${INSTALL_DIR}/lib"
INSTALL_INCLUDE_DIR="${INSTALL_DIR}/include"

OPENSSL_SRC_DIR="${SRC_DIR}/openssl-1.1.1w"
OPENSSL_INSTALL_DIR="${OPENSSL_SRC_DIR}/install"
LIBSRTP_SRC_DIR="${SRC_DIR}/libsrtp"

JOBS="${JOBS:-$(nproc)}"
CROSS_PREFIX="${CROSS_PREFIX:-arm-himix200-linux-}"
CROSS_HOST="${CROSS_HOST:-arm-himix200-linux}"
CROSS_CXX_FROM_ENV="${CROSS_CXX+x}"
CROSS_AR_FROM_ENV="${CROSS_AR+x}"
CROSS_RANLIB_FROM_ENV="${CROSS_RANLIB+x}"
CROSS_STRIP_FROM_ENV="${CROSS_STRIP+x}"
CROSS_CC="${CROSS_CC:-${CROSS_PREFIX}gcc}"
CROSS_CXX="${CROSS_CXX:-${CROSS_PREFIX}g++}"
CROSS_AR="${CROSS_AR:-${CROSS_PREFIX}ar}"
CROSS_RANLIB="${CROSS_RANLIB:-${CROSS_PREFIX}ranlib}"
CROSS_STRIP="${CROSS_STRIP:-${CROSS_PREFIX}strip}"
TOOLCHAIN_DETECTED=0

absolute_tool_path() {
  local tool_path="$1"
  local tool_dir
  local tool_name

  tool_dir="$(cd "$(dirname "${tool_path}")" && pwd -P)"
  tool_name="$(basename "${tool_path}")"
  printf '%s/%s\n' "${tool_dir}" "${tool_name}"
}

resolve_required_tool_path() {
  local configured_tool="$1"
  local tool_label="$2"
  local resolved_tool

  resolved_tool="$(command -v "${configured_tool}" 2>/dev/null || true)"
  if [[ -z "${resolved_tool}" ]]; then
    echo "[deps] missing ${tool_label}: ${configured_tool}" >&2
    echo "[deps] add arm-himix200-linux toolchain bin directory to PATH, or set CROSS_PREFIX/CROSS_CC" >&2
    exit 1
  fi

  absolute_tool_path "${resolved_tool}"
}

resolve_optional_tool_path() {
  local configured_tool="$1"
  local resolved_tool

  if [[ -z "${configured_tool}" ]]; then
    return 0
  fi

  resolved_tool="$(command -v "${configured_tool}" 2>/dev/null || true)"
  if [[ -n "${resolved_tool}" ]]; then
    absolute_tool_path "${resolved_tool}"
  fi
}

detect_toolchain() {
  if [[ "${TOOLCHAIN_DETECTED}" -eq 1 ]]; then
    return 0
  fi

  CROSS_CC="$(resolve_required_tool_path "${CROSS_CC}" "C compiler")"
  if [[ "${CROSS_CC}" != *gcc ]]; then
    echo "[deps] cannot derive OpenSSL cross prefix from compiler: ${CROSS_CC}" >&2
    exit 1
  fi
  CROSS_PREFIX="${CROSS_CC%gcc}"

  if [[ -z "${CROSS_CXX_FROM_ENV}" ]]; then
    CROSS_CXX="${CROSS_PREFIX}g++"
  fi
  if [[ -z "${CROSS_AR_FROM_ENV}" ]]; then
    CROSS_AR="${CROSS_PREFIX}ar"
  fi
  if [[ -z "${CROSS_RANLIB_FROM_ENV}" ]]; then
    CROSS_RANLIB="${CROSS_PREFIX}ranlib"
  fi
  if [[ -z "${CROSS_STRIP_FROM_ENV}" ]]; then
    CROSS_STRIP="${CROSS_PREFIX}strip"
  fi

  CROSS_CXX="$(resolve_required_tool_path "${CROSS_CXX}" "C++ compiler")"
  CROSS_AR="$(resolve_required_tool_path "${CROSS_AR}" "archiver")"
  CROSS_RANLIB="$(resolve_required_tool_path "${CROSS_RANLIB}" "ranlib")"
  CROSS_STRIP="$(resolve_optional_tool_path "${CROSS_STRIP}")"

  export CROSS_PREFIX CROSS_HOST CROSS_CC CROSS_CXX CROSS_AR CROSS_RANLIB CROSS_STRIP
  echo "[deps] using cross compiler ${CROSS_CC}"
  TOOLCHAIN_DETECTED=1
}

ensure_dirs() {
  mkdir -p "${BUILD_DIR}" "${INSTALL_LIB_DIR}" "${INSTALL_INCLUDE_DIR}"
}

clean_removed_deps() {
  rm -rf "${BUILD_DIR}/usrsctp"
  rm -f "${INSTALL_INCLUDE_DIR}/usrsctp.h" \
    "${INSTALL_LIB_DIR}/libusrsctp.a" \
    "${INSTALL_LIB_DIR}/pkgconfig/usrsctp.pc"
}

copy_file() {
  local source="$1"
  local target_dir="$2"
  if [[ -f "${source}" ]]; then
    cp "${source}" "${target_dir}/"
  fi
}

build_openssl() {
  echo "[deps] build openssl"
  (
    cd "${OPENSSL_SRC_DIR}"
    make clean >/dev/null 2>&1 || true
    ./Configure linux-armv4 no-shared no-asm no-tests no-ui-console \
      --cross-compile-prefix="${CROSS_PREFIX}" \
      --prefix="${OPENSSL_INSTALL_DIR}" \
      --openssldir="${OPENSSL_INSTALL_DIR}/ssl"
    make -j"${JOBS}" build_libs
    make install_dev
  )

  copy_file "${OPENSSL_INSTALL_DIR}/lib/libssl.a" "${INSTALL_LIB_DIR}"
  copy_file "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a" "${INSTALL_LIB_DIR}"
}

build_libsrtp() {
  echo "[deps] build libsrtp"
  rm -rf "${BUILD_DIR}/libsrtp"
  mkdir -p "${BUILD_DIR}/libsrtp"
  (
    cd "${BUILD_DIR}/libsrtp"
    "${LIBSRTP_SRC_DIR}/configure" \
      --host="${CROSS_HOST}" \
      --prefix="${INSTALL_DIR}" \
      --enable-openssl \
      --with-openssl-dir="${OPENSSL_INSTALL_DIR}" \
      --disable-pcap \
      CC="${CROSS_CC}" \
      CXX="${CROSS_CXX}" \
      AR="${CROSS_AR}" \
      RANLIB="${CROSS_RANLIB}" \
      CPPFLAGS="-I${OPENSSL_INSTALL_DIR}/include" \
      LDFLAGS="-L${OPENSSL_INSTALL_DIR}/lib -pthread" \
      LIBS="-lcrypto -lpthread -ldl" \
      crypto_CFLAGS="-I${OPENSSL_INSTALL_DIR}/include" \
      crypto_LIBS="-L${OPENSSL_INSTALL_DIR}/lib -lcrypto -lpthread -ldl"
    make -j"${JOBS}" libsrtp2.a
    make install
  )
}

build_all() {
  ensure_dirs
  clean_removed_deps
  detect_toolchain
  build_openssl
  build_libsrtp
}

case "${1:-all}" in
  all)
    build_all
    ;;
  openssl)
    ensure_dirs
    clean_removed_deps
    detect_toolchain
    build_openssl
    ;;
  libsrtp)
    ensure_dirs
    clean_removed_deps
    detect_toolchain
    build_libsrtp
    ;;
  *)
    echo "usage: $0 [all|openssl|libsrtp]" >&2
    exit 2
    ;;
esac

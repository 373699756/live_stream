#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SRC_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${ROOT_DIR}/install"
INSTALL_LIB_DIR="${INSTALL_DIR}/lib"
INSTALL_INCLUDE_DIR="${INSTALL_DIR}/include"
TOOLCHAIN_FILE="${SRC_DIR}/toolchains/arm-himix200-linux.cmake"

OPENSSL_SRC_DIR="${SRC_DIR}/openssl-1.1.1w"
OPENSSL_INSTALL_DIR="${OPENSSL_SRC_DIR}/install"
LIBSRTP_SRC_DIR="${SRC_DIR}/libsrtp"
USRSCTP_SRC_DIR="${SRC_DIR}/usrsctp"
METARTC_SRC_DIR="${SRC_DIR}/metaRTC_src"
METARTC_BIN_LIB_DIR="${METARTC_SRC_DIR}/bin/lib_debug"

JOBS="${JOBS:-$(nproc)}"
CROSS_PREFIX="${CROSS_PREFIX:-arm-himix200-linux-}"

ensure_dirs() {
  mkdir -p "${BUILD_DIR}" "${INSTALL_LIB_DIR}" "${INSTALL_INCLUDE_DIR}" \
    "${METARTC_BIN_LIB_DIR}"
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
  copy_file "${OPENSSL_INSTALL_DIR}/lib/libssl.a" "${METARTC_BIN_LIB_DIR}"
  copy_file "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a" "${METARTC_BIN_LIB_DIR}"
}

build_libsrtp() {
  echo "[deps] build libsrtp"
  rm -rf "${BUILD_DIR}/libsrtp"
  mkdir -p "${BUILD_DIR}/libsrtp"
  (
    cd "${BUILD_DIR}/libsrtp"
    "${LIBSRTP_SRC_DIR}/configure" \
      --host=arm-himix200-linux \
      --prefix="${INSTALL_DIR}" \
      --enable-openssl \
      --with-openssl-dir="${OPENSSL_INSTALL_DIR}" \
      --disable-pcap \
      CC="${CROSS_PREFIX}gcc" \
      CXX="${CROSS_PREFIX}g++" \
      AR="${CROSS_PREFIX}ar" \
      RANLIB="${CROSS_PREFIX}ranlib" \
      CPPFLAGS="-I${OPENSSL_INSTALL_DIR}/include" \
      LDFLAGS="-L${OPENSSL_INSTALL_DIR}/lib -pthread" \
      LIBS="-lcrypto -lpthread -ldl" \
      crypto_CFLAGS="-I${OPENSSL_INSTALL_DIR}/include" \
      crypto_LIBS="-L${OPENSSL_INSTALL_DIR}/lib -lcrypto -lpthread -ldl"
    make -j"${JOBS}" libsrtp2.a
    make install
  )
}

build_usrsctp() {
  echo "[deps] build usrsctp"
  rm -rf "${BUILD_DIR}/usrsctp"
  cmake -S "${USRSCTP_SRC_DIR}" -B "${BUILD_DIR}/usrsctp" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -Dsctp_build_shared_lib=OFF \
    -Dsctp_build_programs=OFF \
    -Dsctp_build_fuzzer=OFF \
    -Dsctp_werror=OFF \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
  cmake --build "${BUILD_DIR}/usrsctp" -j"${JOBS}"
  cmake --install "${BUILD_DIR}/usrsctp"
}

build_metartc_library() {
  local name="$1"
  local source_dir="$2"
  local build_dir="$3"
  shift 3

  echo "[deps] build ${name}"
  rm -rf "${build_dir}"
  cmake -S "${source_dir}" -B "${build_dir}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    "$@"
  cmake --build "${build_dir}" -j"${JOBS}"
}

build_metartc() {
  build_metartc_library "libyangutil8" \
    "${METARTC_SRC_DIR}/libyangutil8" \
    "${BUILD_DIR}/meta_libyangutil8" \
    -DYANG_OPENSSL_ROOT="${OPENSSL_INSTALL_DIR}" \
    -DYang_Moc=2

  build_metartc_library "libmetartccore8" \
    "${METARTC_SRC_DIR}/libmetartccore8" \
    "${BUILD_DIR}/meta_libmetartccore8" \
    -DYANG_OPENSSL_ROOT="${OPENSSL_INSTALL_DIR}"

  build_metartc_library "libmetartc8" \
    "${METARTC_SRC_DIR}/libmetartc8" \
    "${BUILD_DIR}/meta_libmetartc8" \
    -DNoCapture=ON -DNoPlayer=ON

  copy_file "${BUILD_DIR}/meta_libyangutil8/libyangutil8.a" "${INSTALL_LIB_DIR}"
  copy_file "${BUILD_DIR}/meta_libmetartccore8/libmetartccore8.a" "${INSTALL_LIB_DIR}"
  copy_file "${BUILD_DIR}/meta_libmetartc8/libmetartc8.a" "${INSTALL_LIB_DIR}"
  copy_file "${BUILD_DIR}/meta_libyangutil8/libyangutil8.a" "${METARTC_BIN_LIB_DIR}"
  copy_file "${BUILD_DIR}/meta_libmetartccore8/libmetartccore8.a" "${METARTC_BIN_LIB_DIR}"
  copy_file "${BUILD_DIR}/meta_libmetartc8/libmetartc8.a" "${METARTC_BIN_LIB_DIR}"
}

build_all() {
  ensure_dirs
  build_openssl
  build_libsrtp
  build_usrsctp
  build_metartc
}

case "${1:-all}" in
  all)
    build_all
    ;;
  openssl)
    ensure_dirs
    build_openssl
    ;;
  libsrtp)
    ensure_dirs
    build_libsrtp
    ;;
  usrsctp)
    ensure_dirs
    build_usrsctp
    ;;
  metartc)
    ensure_dirs
    build_metartc
    ;;
  *)
    echo "usage: $0 [all|openssl|libsrtp|usrsctp|metartc]" >&2
    exit 2
    ;;
esac

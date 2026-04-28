#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${ROOT_DIR}/src"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${ROOT_DIR}/install"
TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/arm-himix200-linux.cmake"
JOBS="${JOBS:-$(nproc)}"
MBEDTLS_SRC_DIR="${SRC_DIR}/mbedtls"

build_mbedtls() {
  rm -rf "${BUILD_DIR}/mbedtls"
  rm -f "${INSTALL_DIR}/lib/libmbedtls.a" "${INSTALL_DIR}/lib/libmbedx509.a" "${INSTALL_DIR}/lib/libmbedcrypto.a"
  cmake -S "${MBEDTLS_SRC_DIR}" -B "${BUILD_DIR}/mbedtls" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_PROGRAMS=OFF \
    -DENABLE_TESTING=OFF \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
  cmake --build "${BUILD_DIR}/mbedtls" -j"${JOBS}"
  cmake --install "${BUILD_DIR}/mbedtls"
}

build_libsrtp() {
  rm -rf "${BUILD_DIR}/libsrtp"
  cmake -S "${SRC_DIR}/libsrtp" -B "${BUILD_DIR}/libsrtp" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_OPENSSL=OFF \
    -DLIBSRTP_TEST_APPS=OFF \
    -DBUILD_TESTING=OFF \
    -DENABLE_WARNINGS=OFF \
    -DENABLE_WARNINGS_AS_ERRORS=OFF \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
  cmake --build "${BUILD_DIR}/libsrtp" -j"${JOBS}"
  cmake --install "${BUILD_DIR}/libsrtp"
}

build_usrsctp() {
  rm -rf "${BUILD_DIR}/usrsctp"
  cmake -S "${SRC_DIR}/usrsctp" -B "${BUILD_DIR}/usrsctp" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -Dsctp_build_programs=OFF \
    -Dsctp_build_tests=OFF \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
  cmake --build "${BUILD_DIR}/usrsctp" -j"${JOBS}"
  cmake --install "${BUILD_DIR}/usrsctp"
}

build_mbedtls
build_libsrtp
build_usrsctp

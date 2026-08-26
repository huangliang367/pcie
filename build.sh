#!/bin/bash
# build.sh — kernel config / build / QEMU run helper
# Usage: ./build.sh {kconfig|bkernel|run}

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$ROOT_DIR/linux-6.1.183"
BUILD_DIR="$ROOT_DIR/build/kernel"
ROOTFS_IMG="$ROOT_DIR/images/rootfs.ext4"

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

usage() {
    cat <<USAGE
Usage: $0 {kconfig|bkernel|run}

  kconfig   Configure the kernel (menuconfig). Output dir: $BUILD_DIR
  bkernel   Build the kernel. Artifacts go to $BUILD_DIR
  run       Boot the built kernel in QEMU (virt machine)
USAGE
    exit 1
}

do_kconfig() {
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/.config" ]; then
        echo "[kconfig] no .config found, running defconfig first..."
        make -C "$KERNEL_DIR" O="$BUILD_DIR" defconfig
    fi
    make -C "$KERNEL_DIR" O="$BUILD_DIR" menuconfig

    # 保存精简配置回 defconfig
    make -C "$KERNEL_DIR" O="$BUILD_DIR" savedefconfig
    cp "$BUILD_DIR/defconfig" "$KERNEL_DIR/arch/arm64/configs/defconfig"
    echo "[kconfig] saved: $KERNEL_DIR/arch/arm64/configs/defconfig"
}

do_bkernel() {
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/.config" ]; then
        echo "error: $BUILD_DIR/.config not found. Run: $0 kconfig" >&2
        exit 1
    fi
    make -C "$KERNEL_DIR" O="$BUILD_DIR" -j"$(nproc)" Image
    echo "[bkernel] done: $BUILD_DIR/arch/arm64/boot/Image"
}

do_run() {
    local image="$BUILD_DIR/arch/arm64/boot/Image"
    if [ ! -f "$image" ]; then
        echo "error: $image not found. Run: $0 bkernel" >&2
        exit 1
    fi
    qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a72 \
        -smp 4 \
        -m 2G \
        -nographic \
        -kernel "$image" \
		-append "console=ttyAMA0 root=/dev/vda rw" \
		-drive if=none,file="$ROOTFS_IMG",format=raw,id=hd0 \
		-device virtio-blk-pci,drive=hd0
}

[ $# -eq 1 ] || usage

case "$1" in
    kconfig) do_kconfig ;;
    kernel) do_bkernel ;;
    run)     do_run ;;
    *)       usage ;;
esac

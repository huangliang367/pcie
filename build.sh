#!/bin/bash
# build.sh — kernel config / build / QEMU run helper
# Usage: ./build.sh {kconfig|kernel|run|urootfs}

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$ROOT_DIR/linux-6.1.183"
BUILD_DIR="$ROOT_DIR/build/kernel"
ROOTFS_IMG="$ROOT_DIR/debian-build/images/debian-arm64.ext4"
QEMU=${ROOT_DIR}/qemu/build/qemu-system-aarch64

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

usage() {
    cat <<USAGE
Usage: $0 {kconfig|kernel|run|urootfs}

  kconfig   Configure the kernel (menuconfig). Output dir: $BUILD_DIR
  bkernel   Build the kernel. Artifacts go to $BUILD_DIR
  run       Boot the built kernel in QEMU (virt machine)
  urootfs   Sync debian-build/rootfs into the ext4 rootfs image
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

    echo "[bkernel] build Image..."
    make -C "$KERNEL_DIR" \
        O="$BUILD_DIR" \
        -j"$(nproc)" \
        Image

    echo "[bkernel] build modules..."
    make -C "$KERNEL_DIR" \
        O="$BUILD_DIR" \
        -j"$(nproc)" \
        modules

    echo "[bkernel] install modules..."
    make -C "$KERNEL_DIR" \
        O="$BUILD_DIR" \
        INSTALL_MOD_PATH="$ROOT_DIR/debian-build/rootfs" \
        modules_install

    echo "[bkernel] done: $BUILD_DIR/arch/arm64/boot/Image"
}

do_urootfs() {
    local rootfs_dir="$ROOT_DIR/debian-build/rootfs"
    local mnt="$ROOT_DIR/build/rootfs-mnt"

    if [ ! -f "$ROOTFS_IMG" ]; then
        echo "error: $ROOTFS_IMG not found" >&2
        exit 1
    fi
    if [ ! -d "$rootfs_dir" ]; then
        echo "error: $rootfs_dir not found" >&2
        exit 1
    fi

    mkdir -p "$mnt"

    local loop
    loop=$(sudo losetup -f --show "$ROOTFS_IMG")
    echo "[urootfs] attached $ROOTFS_IMG -> $loop"

    cleanup() {
        sudo umount "$mnt" 2>/dev/null || true
        sudo losetup -d "$loop" 2>/dev/null || true
    }
    trap cleanup EXIT

    sudo mount "$loop" "$mnt"

    echo "[urootfs] syncing $rootfs_dir -> image"
    sudo rsync -a --delete \
        --exclude=/dev \
        --exclude=/proc \
        --exclude=/sys \
        --exclude=/run \
        --exclude=/lost+found \
        "$rootfs_dir/" "$mnt/"

    # 保证虚拟文件系统挂载点存在
    sudo mkdir -p "$mnt/dev" "$mnt/proc" "$mnt/sys" "$mnt/run"

    sudo umount "$mnt"
    sudo losetup -d "$loop"
    trap - EXIT

    echo "[urootfs] done: $ROOTFS_IMG updated"
}

do_run() {
    local image="$BUILD_DIR/arch/arm64/boot/Image"
    if [ ! -f "$image" ]; then
        echo "error: $image not found. Run: $0 bkernel" >&2
        exit 1
    fi
    ${QEMU} \
        -machine virt \
        -cpu cortex-a72 \
        -smp 4 \
        -m 2G \
        -nographic \
        -kernel "$image" \
		-append "console=ttyAMA0 root=/dev/vda rw" \
		-drive if=none,file="$ROOTFS_IMG",format=raw,id=hd0 \
		-device virtio-blk-pci,drive=hd0 \
		-device demo-pcie-ep
}

[ $# -eq 1 ] || usage

case "$1" in
    kconfig) do_kconfig ;;
    kernel) do_bkernel ;;
    run)     do_run ;;
    urootfs) do_urootfs ;;
    *)       usage ;;
esac



# mkdir build
# cd build

# ../configure \
#     --target-list=aarch64-softmmu \
#     --enable-debug

# make -j$(nproc)
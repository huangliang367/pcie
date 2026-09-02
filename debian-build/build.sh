#!/bin/bash

sudo debootstrap \
    --foreign \
    --arch=arm64 \
    trixie \
    ./rootfs \
    http://deb.debian.org/debian

sudo chroot rootfs \
    /debootstrap/debootstrap \
    --second-stage
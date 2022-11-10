#! /bin/bash
#
# Copyright (C) 2022 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

. "$(dirname $0)/buildbot-utils.sh"

known_actions="create|boot|setup-ssh|connect|quit"
known_arches="arm64|riscv64"

if [[ -z $ANDROID_BUILD_TOP ]]; then
    msgerror "ANDROID_BUILD_TOP is not set"
    exit 1
elif [[ -z $ART_TEST_SSH_USER ]]; then
    msgerror "ART_TEST_SSH_USER is not set"
    exit 1
elif [[ -z $ART_TEST_SSH_HOST ]]; then
    msgerror "ART_TEST_SSH_HOST is not set"
    exit 1
elif [[ -z $ART_TEST_SSH_PORT ]]; then
    msgerror "ART_TEST_SSH_PORT is not set"
    exit 1
elif [[ ( $# -ne 1 ) || ! ( "$1" =~ ^($known_actions)$ ) ]]; then
    msgerror "usage: $0 <$known_actions>"
    exit 1
elif [[ ! "$ART_TEST_ARCH" =~ ^($known_arches)$ ]]; then
    msgerror "ART_TEST_ARCH must be one of <$known_arches>"
    exit 1
fi

if [[ "$ART_TEST_ARCH" = "riscv64" ]]; then
    ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-riscv64.img"
elif [[ "$ART_TEST_ARCH" = "arm64" ]]; then
    ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-arm64.img"
fi
ART_TEST_VM_DIR=$ANDROID_BUILD_TOP/vm/$ART_TEST_ARCH
ART_TEST_VM=$ART_TEST_VM_DIR/$ART_TEST_VM_IMG

action="$1"

if [[ $action = create ]]; then
(
    rm -rf "$ART_TEST_VM_DIR"
    mkdir -p "$ART_TEST_VM_DIR"
    cd "$ART_TEST_VM_DIR"

    # sudo apt install qemu-system-<arm,riscv> qemu-efi cloud-image-utils
    wget "http://cloud-images.ubuntu.com/releases/22.04/release/$ART_TEST_VM_IMG"

    if [[ "$ART_TEST_ARCH" = "arm64" ]]; then
        dd if=/dev/zero of=flash0.img bs=1M count=64
        dd if=/usr/share/qemu-efi/QEMU_EFI.fd of=flash0.img conv=notrunc
        dd if=/dev/zero of=flash1.img bs=1M count=64
    fi

    qemu-img resize "$ART_TEST_VM_IMG" +128G

    # https://help.ubuntu.com/community/CloudInit
    cat >user-data <<EOF
#cloud-config
ssh_pwauth: true
chpasswd:
  expire: false
  list:
    - $ART_TEST_SSH_USER:ubuntu
EOF
    cloud-localds user-data.img user-data
)
elif [[ $action = boot ]]; then
(
    cd "$ART_TEST_VM_DIR"
    if [[ "$ART_TEST_ARCH" = "riscv64" ]]; then
        qemu-system-riscv64 \
            -m 16G \
            -smp 8 \
            -M virt \
            -nographic \
            -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.elf \
            -kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
            -drive file="$ART_TEST_VM_IMG",if=virtio \
            -drive file=user-data.img,format=raw,if=virtio \
            -device virtio-net-device,netdev=usernet \
            -netdev user,id=usernet,hostfwd=tcp::$ART_TEST_SSH_PORT-:22
            ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-riscv64.img"
    elif [[ "$ART_TEST_ARCH" = "arm64" ]]; then
        qemu-system-aarch64 \
            -m 16G \
            -smp 8 \
            -cpu cortex-a57 \
            -M virt \
            -nographic \
            -drive if=none,file="$ART_TEST_VM_IMG",id=hd0 \
            -pflash flash0.img \
            -pflash flash1.img \
            -drive file=user-data.img,format=raw,id=cloud \
            -device virtio-blk-device,drive=hd0 \
            -device virtio-net-device,netdev=usernet \
            -netdev user,id=usernet,hostfwd=tcp::$ART_TEST_SSH_PORT-:22
            ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-arm64.img"
    fi

)
elif [[ $action = setup-ssh ]]; then
    # Clean up mentions of this VM from known_hosts
    sed -i -E "/\[$ART_TEST_SSH_HOST.*\]:$ART_TEST_SSH_PORT .*/d" $HOME/.ssh/known_hosts
    ssh-copy-id -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST

elif [[ $action = connect ]]; then
    ssh -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST

elif [[ $action = quit ]]; then
    ssh -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST "sudo poweroff"

fi

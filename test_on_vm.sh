#!/bin/bash
#
# Copyright 2022, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

unset ART_TEST_ANDROID_ROOT
unset CUSTOM_TARGET_LINKER
unset ART_TEST_ANDROID_ART_ROOT
unset ART_TEST_ANDROID_RUNTIME_ROOT
unset ART_TEST_ANDROID_I18N_ROOT
unset ART_TEST_ANDROID_TZDATA_ROOT

export ANDROID_SERIAL=nonexistent
export SOONG_ALLOW_MISSING_DEPENDENCIES=true

. ./build/envsetup.sh
lunch aosp_riscv64-userdebug
art/tools/buildbot-build.sh --target -j72 #--installclean

export ART_TEST_SSH_USER=ubuntu
export ART_TEST_SSH_HOST=localhost
export ART_TEST_SSH_PORT=10001
export ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-riscv64.img"
export ART_TEST_VM=$ANDROID_BUILD_TOP/vm/riscv64
export ART_TEST_CHROOT=/home/$ART_TEST_SSH_USER/art-test-chroot
export SSH_CMD="ssh -q -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST"
export RSYNC_RSH="ssh -p $ART_TEST_SSH_PORT"
export RSYNC_CMD="rsync -az" #pPc
export SCP_CMD="scp -P $ART_TEST_SSH_PORT -p -r"
export CHROOT_CMD="unshare --user --map-root-user chroot art-test-chroot"

copy() {
    src="$1"
    dst="$2"
#    cd $(dirname $src)
#    tar cf - $(basename $src) | ssh -q -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST "(cd $(dirname $dst); tar xf - )"
    tar cf - $src | ssh -q -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST "(cd $ART_TEST_CHROOT; tar xf - )"
}
export -f copy

#art/tools/buildbot-cleanup-device.sh
#art/tools/buildbot-setup-device.sh
art/tools/buildbot-sync.sh

#art/test/run-test --chroot $ART_TEST_CHROOT --64 --interpreter -O --no-relocate --no-image 001-HelloWorld
#art/test/run-test --chroot $ART_TEST_CHROOT --64 --interpreter -O --never-clean --no-relocate --runtime-option -Xcheck:jni --runtime-option -verbose:jni,startup,threads,class --no-image 001-HelloWorld
#art/test/run-test --chroot $ART_TEST_CHROOT --64 --interpreter -O --never-clean --no-relocate --runtime-option -Xcheck:jni --no-image 001-HelloWorld

art/test.py -j8 --target -r --no-prebuild --ndebug --no-image --64 --interpreter $@

#art/tools/run-gtests.sh

#art/test/run-test --chroot $ART_TEST_CHROOT --64 --interpreter -O --never-clean --no-relocate --no-image 001-HelloWorld

#art/tools/buildbot-cleanup-device.sh

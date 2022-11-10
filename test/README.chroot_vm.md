# ART chroot-based testing in a Linux VM

This doc describes how to set up a Linux VM and how to run ART tests on it.

## Set up the VM

Use script art/build/buildbot-vm.sh. It has different commands (actions).

Set up some environment variables used by the script (change as you see fit):
```
export ART_TEST_SSH_USER=ubuntu
export ART_TEST_SSH_HOST=localhost
export ART_TEST_SSH_PORT=10002
export ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-arm64.img"
```
Create the VM (download it and do some initial setup):
```
art/tools/buildbot-vm.sh create
```
Boot the VM (login is `$ART_TEST_SSH_USER`, password is `ubuntu`):
```
art/tools/buildbot-vm.sh boot
```
Configure SSH (enter `yes` to add VM to `known_hosts` and then the password):
```
art/tools/buildbot-vm.sh setup-ssh
```
Now you have the shell (no need to enter password every time):
```
art/tools/buildbot-vm.sh connect
```
To power off the VM, do:
```
art/tools/buildbot-vm.sh quit
```
To speed up SSH access, set `UseDNS no` in /etc/ssh/sshd_config on the VM (and
apply other tweaks described in https://jrs-s.net/2017/07/01/slow-ssh-logins).

# Run ART tests
```
This is done in the same way as you would run tests in chroot on device (except
for a few extra environment variables):

unset ART_TEST_ANDROID_ROOT
unset CUSTOM_TARGET_LINKER
unset ART_TEST_ANDROID_ART_ROOT
unset ART_TEST_ANDROID_RUNTIME_ROOT
unset ART_TEST_ANDROID_I18N_ROOT
unset ART_TEST_ANDROID_TZDATA_ROOT

export ANDROID_SERIAL=nonexistent

. ./build/envsetup.sh
lunch armv8-eng
art/tools/buildbot-build.sh --target -j72 # --installclean

export ART_TEST_SSH_USER=ubuntu
export ART_TEST_SSH_HOST=localhost
export ART_TEST_SSH_PORT=10002
export ART_TEST_VM_IMG="ubuntu-22.04-server-cloudimg-arm64.img"
export ART_TEST_VM=$ANDROID_BUILD_TOP/vm/$ART_TEST_VM_IMG
export ART_TEST_CHROOT=/home/$ART_TEST_SSH_USER/art-test-chroot
export SSH_CMD="ssh -q -p $ART_TEST_SSH_PORT $ART_TEST_SSH_USER@$ART_TEST_SSH_HOST"
export RSYNC_RSH="ssh -p $ART_TEST_SSH_PORT"
export RSYNC_CMD="rsync -rlz --mkpath"
export CHROOT_CMD="unshare --user --map-root-user chroot art-test-chroot"

art/tools/buildbot-cleanup-device.sh
art/tools/buildbot-setup-device.sh
art/tools/buildbot-sync.sh

art/test/run-test --chroot $ART_TEST_CHROOT --64 --interpreter -O 001-HelloWorld
art/test.py --target -r --ndebug --no-image --64 --interpreter $@

art/tools/buildbot-cleanup-device.sh
```
Both test.py and run-test scripts can be used. Tweak options as necessary.

# Limitations

Limitations are mostly related to the absence of system properties on the VM.
They are not really needed for ART tests, but they are used for test-related
things, e.g. to find out if the tests should run in debug configuration (option
`ro.debuggable`). Therefore debug configuration is currently broken.

#!/bin/bash
set -euo pipefail

FILE="$(basename "$0")"

# Enable the multilib repository
cat << EOM >> /etc/pacman.conf
[multilib]
Include = /etc/pacman.d/mirrorlist
EOM

pacman -Syu --noconfirm --needed base-devel

# Makepkg does not allow running as root
# Create a new user `builder`
# `builder` needs to have a home directory because some PKGBUILDs will try to
# write to it (e.g. for cache)
useradd builder -m -u $PUID
# When installing dependencies, makepkg will use sudo
# Give user `builder` passwordless sudo access
echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

sudo -H -u builder makepkg -scCf --noconfirm
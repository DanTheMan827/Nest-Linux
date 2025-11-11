#!/bin/bash
set -e

# Get the path this script is located in
SCRIPT_DIR="$(realpath "$(dirname "$0")")"
cd "$SCRIPT_DIR" || exit 1
echo $SCRIPT_DIR

NEEDED_TOOLS=""
DEF_CONFIG="$SCRIPT_DIR/configs/gtvhacker/defconfig"
ROOTFS_PATH="$SCRIPT_DIR/rootfs"
LOGO_PATH="$SCRIPT_DIR/logo/nest-logo-320x320.png"

if [[ -f "$1" ]]; then
  DEF_CONFIG="$1"
fi

if [[ -d "$2" ]]; then
  ROOTFS_PATH="$2"
fi

if [[ -f "$3" ]]; then
  LOGO_PATH="$3"
fi

DEF_CONFIG="$(realpath "$DEF_CONFIG")"
ROOTFS_PATH="$(realpath "$ROOTFS_PATH")"
LOGO_PATH="$(realpath "$LOGO_PATH")"

# Check for cpio
if ! which cpio > /dev/null; then
  NEEDED_TOOLS="$NEEDED_TOOLS cpio"
fi

# Check for mkimage
if ! which mkimage > /dev/null; then
  NEEDED_TOOLS="$NEEDED_TOOLS u-boot-tools"
fi

# Check for pngtopnm
if ! which pngtopnm > /dev/null; then
  NEEDED_TOOLS="$NEEDED_TOOLS netpbm"
fi

if [[ ! -z "$NEEDED_TOOLS" ]]; then
  printf 'The following packages are required but not installed:\n\n' &&
  printf '  %s\n' $NEEDED_TOOLS &&
  printf '\nPlease install them with apt and try again.\n'
  exit 1
fi

if [[ ! -e "$SCRIPT_DIR/toolchain/bin/arm-linux-gnueabi-gcc" ]]; then
  mkdir -p "$SCRIPT_DIR/toolchain"
  wget "$(cat "$SCRIPT_DIR/toolchain.txt")" -qO- | \
    tar -xJvf - -C "$SCRIPT_DIR/toolchain" --strip-components=1
fi

# Create the logo file
(
  echo "Converting \"$(basename "$LOGO_PATH")\" to \"logo_diamond_clut224.ppm\"..."
	pngtopnm -mix "$LOGO_PATH" | \
		  ppmquant -fs 223 | \
		  pnmtoplainpnm > "$SCRIPT_DIR/linux/drivers/video/logo/logo_diamond_clut224.ppm"
)

export PATH="$SCRIPT_DIR/toolchain/bin:$PATH"
(
  cd "$ROOTFS_PATH" || exit 1
  find . -print0 | cpio -ocv0 > "$SCRIPT_DIR/initramfs_data.cpio" || exit 1
)

(
  cd "$SCRIPT_DIR/linux" || exit 1
  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j"$(nproc)" distclean || exit 1
  cp "$DEF_CONFIG" ".config"
  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j"$(nproc)" uImage || exit 1
)
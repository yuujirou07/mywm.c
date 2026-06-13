#!/bin/bash
set -e

amdgpu_card=""
for driver in /sys/class/drm/card*/device/driver; do
    [ -e "$driver" ] || continue
    if [ "$(basename "$(readlink "$driver")")" = "amdgpu" ]; then
        card_dir="$(basename "$(dirname "$(dirname "$driver")")")"
        amdgpu_card="/dev/dri/$card_dir"
        break
    fi
done

if [ -z "$amdgpu_card" ]; then
    echo "amdgpu device not found" >&2
    exit 1
fi

export WLR_DRM_DEVICES="$amdgpu_card"
export SWAY_UNSUPPORTED_GPU=true
unset __GLX_VENDOR_LIBRARY_NAME
unset __NV_PRIME_RENDER_OFFLOAD
unset __VK_LAYER_NV_optimus
unset VK_DRIVER_FILES
unset VK_ICD_FILENAMES
unset GBM_BACKEND

exec sway "$@"

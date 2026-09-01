#!/bin/bash
#
# Copyright (C) 2016 The CyanogenMod Project
# Copyright (C) 2017-2020 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

set -e

DEVICE=xuanyuan
VENDOR=xiaomi

# Load extract_utils and do some sanity checks
MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "${MY_DIR}" ]]; then MY_DIR="${PWD}"; fi

ANDROID_ROOT="${MY_DIR}/../../.."

HELPER="${ANDROID_ROOT}/tools/extract-utils/extract_utils.sh"
if [ ! -f "${HELPER}" ]; then
    echo "Unable to find helper script at ${HELPER}"
    exit 1
fi
source "${HELPER}"

# Default to sanitizing the vendor folder before extraction
CLEAN_VENDOR=true

KANG=
SECTION=

while [ "${#}" -gt 0 ]; do
    case "${1}" in
    -n | --no-cleanup)
        CLEAN_VENDOR=false
        ;;
    -k | --kang)
        KANG="--kang"
        ;;
    -s | --section)
        SECTION="${2}"
        shift
        CLEAN_VENDOR=false
        ;;
    *)
        SRC="${1}"
        ;;
    esac
    shift
done

if [ -z "${SRC}" ]; then
    SRC="adb"
fi

function blob_fixup() {
    case "${1}" in
        odm/etc/camera/enhance_motiontuning.xml | odm/etc/camera/motiontuning.xml | odm/etc/camera/snsc_bokeh_motiontuning.xml | odm/etc/camera/snsc_motiontuning.xml | odm/etc/camera/snsc_enhance_motiontuning.xml | odm/etc/camera/snsc_noface_motiontuning.xml)
            sed -i 's/<?xml=/<?xml /g' "${2}"
            ;;
        vendor/etc/init/hw/init.qcom.rc)
            sed -i '/interface vendor\.qti\.hardware\.wigig\.netperftuner@1\.0::INetPerfTuner default/d' "${2}"
            ;;
        vendor/etc/seccomp_policy/c2audio.vendor.ext-arm64.policy)
            [ "$2" = "" ] && return 0
            [ -n "$(tail -c 1 "${2}")" ] && echo >> "${2}"
            grep -q "setsockopt: 1" "${2}" || echo "setsockopt: 1" >> "${2}"
            grep -q "^lseek:" "${2}" || echo "lseek: 1" >> "${2}"
            ;;
        vendor/etc/seccomp_policy/gnss@2.0-qsap-location.policy)
            [ "$2" = "" ] && return 0
            [ -n "$(tail -c 1 "${2}")" ] && echo >> "${2}"
            grep -q "sched_get_priority_min: 1" "${2}" || echo "sched_get_priority_min: 1" >> "${2}"
            grep -q "sched_get_priority_max: 1" "${2}" || echo "sched_get_priority_max: 1" >> "${2}"
            grep -q "^lseek:" "${2}" || echo "lseek: 1" >> "${2}"
            ;;
        vendor/etc/seccomp_policy/atfwd@2.0.policy | \
        vendor/etc/seccomp_policy/c2audio.vendor.ext-arm.policy | \
        vendor/etc/seccomp_policy/codec2.vendor.ext-arm64.policy | \
        vendor/etc/seccomp_policy/gnss@2.0-edgnss-daemon.policy | \
        vendor/etc/seccomp_policy/gnss@2.0-xtra-daemon.policy | \
        vendor/etc/seccomp_policy/qesdksec.policy | \
        vendor/etc/seccomp_policy/qsap_qapeservice.policy | \
        vendor/etc/seccomp_policy/qsap_voiceui.policy | \
        vendor/etc/seccomp_policy/qti-systemd.policy | \
        vendor/etc/seccomp_policy/syshealthmon.policy)
            [ "$2" = "" ] && return 0
            [ -n "$(tail -c 1 "${2}")" ] && echo >> "${2}"
            grep -q "^lseek:" "${2}" || echo "lseek: 1" >> "${2}"
            ;;
        odm/lib64/libaudioroute_ext.so | vendor/lib64/libar-pal.so)
            "${PATCHELF}" --replace-needed "libaudioroute.so" "libaudioroute-v34.so" "${2}"
            ;;
        vendor/lib64/libcameraopt.so)
            "${PATCHELF}" --add-needed "libprocessgroup_shim.so" "$2"
            ;;
        odm/etc/init/vendor.qti.camera.provider-service_64.rc)
            sed -i '/^service vendor\.camera-provider /a\    setenv JE_MALLOC_ZERO_FILLING 1' "${2}"
            ;;
        vendor/etc/init/vendor.qti.hardware.display.composer-service.rc)
            sed -i '/^service vendor\.qti\.hardware\.display\.composer /a\    setenv JE_MALLOC_ZERO_FILLING 1' "${2}"
            ;;
        vendor/etc/init/vendor.xiaomi.hardware.vibratorfeature.service.rc)
            sed -i "s/\/odm\/bin\//\/vendor\/bin\//g" "${2}"
            sed -i "s/\/odm\/etc\//\/vendor\/etc\//g" "${2}"
            ;;
        vendor/lib64/libdlbdsservice.so | vendor/lib64/libdlbpreg.so | vendor/lib64/libswspatializeraidl_ext.so | vendor/lib64/soundfx/libdlbvolaidl.so | vendor/lib64/soundfx/libhwdapaidl.so | vendor/lib64/soundfx/libswspatializeraidl.so)
            "${PATCHELF}" --replace-needed "libstagefright_foundation.so" "libstagefright_foundation-v33.so" "${2}"
            ;;
        vendor/etc/sensors/hals.conf)
            sed -i '$a sensors.xiaomi.so' "${2}"
            ;;
    esac
}

# Initialize the helper
setup_vendor "${DEVICE}" "${VENDOR}" "${ANDROID_ROOT}" false "${CLEAN_VENDOR}"

extract "${MY_DIR}/proprietary-files.txt" "${SRC}" "${KANG}" --section "${SECTION}"

"${MY_DIR}/setup-makefiles.sh"

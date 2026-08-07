#
# Copyright (C) 2024 The Android Open Source Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from the Neoteric configuration.
$(call inherit-product, vendor/neoteric/target/product/neoteric-target.mk)

# Inherit from xuanyuan device.
$(call inherit-product, device/xiaomi/xuanyuan/sun.mk)

# Device identifier
PRODUCT_DEVICE := xuanyuan
PRODUCT_NAME := xuanyuan
PRODUCT_BRAND := Xiaomi
PRODUCT_MODEL := 25010PN30G
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="xuanyuan_eea-user 15 AQ3A.250226.002 OS3.0.303.0.WOAEUXM release-keys" \
    BuildFingerprint=Xiaomi/xuanyuan_eea/xuanyuan:15/AQ3A.250226.002/OS3.0.303.0.WOAEUXM:user/release-keys \
    DeviceName=xuanyuan \
    DeviceProduct=xuanyuan \
    SystemDevice=xuanyuan \
    SystemName=xuanyuan

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

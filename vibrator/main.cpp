/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "vibratorfeature-wrapper"

#include <android-base/logging.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <unistd.h>

#include "VibratorWrapper.h"

using aidl::android::hardware::vibrator::IVibrator;
using aidl::android::hardware::vibrator::VibratorWrapper;

namespace {

constexpr char kDownstream[] = "android.hardware.vibrator.IVibrator/vibratorfeature";

// Nothing left to forward to, so let init restart us against the new one.
void onDownstreamDied(void*) {
    LOG(ERROR) << "downstream vibrator died, restarting";
    _exit(EXIT_FAILURE);  // on a binder thread, skip static dtors
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);

    ndk::SpAIBinder binder(AServiceManager_waitForService(kDownstream));
    auto downstream = IVibrator::fromBinder(binder);
    CHECK(downstream != nullptr) << "no downstream vibrator to wrap";

    static ndk::ScopedAIBinder_DeathRecipient recipient(
            AIBinder_DeathRecipient_new(onDownstreamDied));
    // Null cookie, a non-null one without onUnlinked aborts in newer libbinder_ndk.
    binder_status_t linked = AIBinder_linkToDeath(binder.get(), recipient.get(), nullptr);
    if (linked != STATUS_OK) {
        LOG(WARNING) << "linkToDeath failed: " << linked;
    }

    auto wrapper = ndk::SharedRefBase::make<VibratorWrapper>(downstream);
    const std::string instance = std::string(IVibrator::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(wrapper->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // joinThreadPool does not return
}

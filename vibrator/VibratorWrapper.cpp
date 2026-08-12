/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "VibratorWrapper"

#include "VibratorWrapper.h"

#include <android-base/logging.h>

#include <chrono>

namespace aidl::android::hardware::vibrator {

namespace {

/*
 * Primitive -> Xiaomi effect id, named after the RTP waveforms in /vendor/firmware.
 * Retune by swapping ids. Only CLICK/THUD/SPIN/LIGHT_TICK/LOW_TICK come from MSDL,
 * the rise/fall ones are a best guess.
 */
struct PrimitiveMapping {
    CompositePrimitive primitive;
    int32_t effectId;
    int32_t durationMs;
};

constexpr PrimitiveMapping kPrimitives[] = {
        {CompositePrimitive::NOOP, -1, 0},           // delay only
        {CompositePrimitive::CLICK, 0, 30},          // 0_click
        {CompositePrimitive::THUD, 3, 30},           // 3_thud
        {CompositePrimitive::SPIN, 170, 60},         // 170_slidingDamping
        {CompositePrimitive::QUICK_RISE, 57, 60},    // 57_FadeIn
        {CompositePrimitive::SLOW_RISE, 57, 90},     // 57_FadeIn
        {CompositePrimitive::QUICK_FALL, 58, 60},    // 58_FadeOut
        {CompositePrimitive::LIGHT_TICK, 183, 20},   // 183_signalList
        {CompositePrimitive::LOW_TICK, 166, 20},     // 166_feedbackNeutral
};

constexpr int32_t kCompositionSizeMax = 32;
constexpr int32_t kCompositionDelayMax = 1000;

const PrimitiveMapping* lookup(CompositePrimitive primitive) {
    for (const auto& entry : kPrimitives) {
        if (entry.primitive == primitive) return &entry;
    }
    return nullptr;
}

// Downstream only takes a coarse strength.
EffectStrength scaleToStrength(float scale) {
    if (scale <= 0.34f) return EffectStrength::LIGHT;
    if (scale <= 0.67f) return EffectStrength::MEDIUM;
    return EffectStrength::STRONG;
}

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

}  // namespace

VibratorWrapper::VibratorWrapper(std::shared_ptr<IVibrator> downstream)
    : mDownstream(std::move(downstream)) {}

VibratorWrapper::~VibratorWrapper() {
    stopComposition();
}

void VibratorWrapper::stopComposition() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mAbort = true;
        worker = std::move(mWorker);
    }
    mWake.notify_all();
    if (worker.joinable()) worker.join();
}

// perform() hands back the effect length, which is what paces the next primitive.
void VibratorWrapper::play(std::vector<CompositeEffect> composite,
                        std::shared_ptr<IVibratorCallback> callback) {
    for (const auto& effect : composite) {
        std::unique_lock<std::mutex> lock(mLock);
        if (effect.delayMs > 0) {
            mWake.wait_for(lock, std::chrono::milliseconds(effect.delayMs),
                           [this] { return mAbort; });
        }
        if (mAbort) break;
        lock.unlock();

        const PrimitiveMapping* mapping = lookup(effect.primitive);
        if (mapping == nullptr || mapping->effectId < 0) continue;

        int32_t lengthMs = 0;
        auto status = mDownstream->perform(static_cast<Effect>(mapping->effectId),
                                           scaleToStrength(effect.scale), nullptr, &lengthMs);
        if (!status.isOk()) {
            LOG(WARNING) << "downstream perform(" << mapping->effectId
                         << ") failed: " << status.getDescription();
            continue;
        }
        if (lengthMs <= 0) lengthMs = mapping->durationMs;

        lock.lock();
        mWake.wait_for(lock, std::chrono::milliseconds(lengthMs), [this] { return mAbort; });
        if (mAbort) break;
    }

    if (callback != nullptr) callback->onComplete();
}

ndk::ScopedAStatus VibratorWrapper::compose(const std::vector<CompositeEffect>& composite,
                                         const std::shared_ptr<IVibratorCallback>& callback) {
    if (composite.size() > static_cast<size_t>(kCompositionSizeMax)) {
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_ILLEGAL_ARGUMENT));
    }
    for (const auto& effect : composite) {
        if (effect.delayMs > kCompositionDelayMax || effect.scale < 0.0f || effect.scale > 1.0f ||
            lookup(effect.primitive) == nullptr) {
            return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_ILLEGAL_ARGUMENT));
        }
    }

    stopComposition();

    std::lock_guard<std::mutex> lock(mLock);
    mAbort = false;
    mWorker = std::thread(&VibratorWrapper::play, this, composite, callback);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::off() {
    stopComposition();
    return mDownstream->off();
}

ndk::ScopedAStatus VibratorWrapper::getCapabilities(int32_t* _aidl_return) {
    auto status = mDownstream->getCapabilities(_aidl_return);
    if (!status.isOk()) return status;
    *_aidl_return |= IVibrator::CAP_COMPOSE_EFFECTS;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::getSupportedPrimitives(
        std::vector<CompositePrimitive>* _aidl_return) {
    _aidl_return->clear();
    for (const auto& entry : kPrimitives) _aidl_return->push_back(entry.primitive);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::getPrimitiveDuration(CompositePrimitive primitive,
                                                      int32_t* _aidl_return) {
    const PrimitiveMapping* mapping = lookup(primitive);
    if (mapping == nullptr) {
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    }
    *_aidl_return = mapping->durationMs;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::getCompositionDelayMax(int32_t* _aidl_return) {
    *_aidl_return = kCompositionDelayMax;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::getCompositionSizeMax(int32_t* _aidl_return) {
    *_aidl_return = kCompositionSizeMax;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorWrapper::on(int32_t timeoutMs,
                                    const std::shared_ptr<IVibratorCallback>& callback) {
    stopComposition();
    return mDownstream->on(timeoutMs, callback);
}

ndk::ScopedAStatus VibratorWrapper::perform(Effect effect, EffectStrength strength,
                                         const std::shared_ptr<IVibratorCallback>& callback,
                                         int32_t* _aidl_return) {
    stopComposition();
    return mDownstream->perform(effect, strength, callback, _aidl_return);
}

ndk::ScopedAStatus VibratorWrapper::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    return mDownstream->getSupportedEffects(_aidl_return);
}

ndk::ScopedAStatus VibratorWrapper::setAmplitude(float amplitude) {
    return mDownstream->setAmplitude(amplitude);
}

ndk::ScopedAStatus VibratorWrapper::setExternalControl(bool enabled) {
    return mDownstream->setExternalControl(enabled);
}

ndk::ScopedAStatus VibratorWrapper::getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) {
    return mDownstream->getSupportedAlwaysOnEffects(_aidl_return);
}

ndk::ScopedAStatus VibratorWrapper::alwaysOnEnable(int32_t id, Effect effect, EffectStrength strength) {
    return mDownstream->alwaysOnEnable(id, effect, strength);
}

ndk::ScopedAStatus VibratorWrapper::alwaysOnDisable(int32_t id) {
    return mDownstream->alwaysOnDisable(id);
}

ndk::ScopedAStatus VibratorWrapper::getResonantFrequency(float*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getQFactor(float*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getFrequencyResolution(float*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getFrequencyMinimum(float*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getBandwidthAmplitudeMap(std::vector<float>*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getPwlePrimitiveDurationMax(int32_t*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getPwleCompositionSizeMax(int32_t*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::getSupportedBraking(std::vector<Braking>*) {
    return unsupported();
}
ndk::ScopedAStatus VibratorWrapper::composePwle(const std::vector<PrimitivePwle>&,
                                             const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

}  // namespace aidl::android::hardware::vibrator

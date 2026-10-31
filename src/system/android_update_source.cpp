// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/android_update_source.h"

#ifdef __ANDROID__
#include "system/android_jni.h"

#include <spdlog/spdlog.h>

#include <SDL.h>
#include <jni.h>
#endif

namespace helix::android {

std::string update_url(const std::string& installer_package, const OfferedRelease& offered) {
    if (installed_by_play(installer_package)) {
        return kPlayStoreMarketUrl;
    }
    return release_page_url(offered);
}

std::optional<std::string> version_from_version_code(int64_t code) {
    // Android's versionCode ceiling; scripts/android-version-code.sh refuses past it.
    constexpr int64_t kMaxVersionCode = 2100000000;
    if (code <= 0 || code > kMaxVersionCode) {
        return std::nullopt;
    }

    const int64_t ordinal = code % 100;
    const int64_t triple = code / 100;
    const int64_t patch = triple % 1000;
    const int64_t minor = (triple / 1000) % 1000;
    const int64_t major = triple / 1000000;

    std::string version =
        std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (ordinal == 99) {
        return version;
    }

    // Each prerelease stage owns a band of 30 ordinals; the band's first ordinal
    // is the bare stage with no .N. Ordinals 90-98 belong to no stage.
    static constexpr const char* kStages[] = {"alpha", "beta", "rc"};
    const int64_t stage = ordinal / 30;
    if (stage >= 3) {
        return std::nullopt;
    }
    version += "-";
    version += kStages[stage];
    if (const int64_t n = ordinal % 30; n > 0) {
        version += "." + std::to_string(n);
    }
    return version;
}

UpdateVerdict decide_update(const std::string& installer_package, const PlayUpdateQuery& play,
                            const std::string& manifest_version) {
    UpdateVerdict verdict;
    if (!installed_by_play(installer_package)) {
        verdict.kind = UpdateVerdict::Kind::Manifest;
        return verdict;
    }

    switch (play.outcome) {
    case PlayUpdateQuery::Outcome::NotAvailable:
        verdict.reason = "Google Play reports no update for this device";
        return verdict;
    case PlayUpdateQuery::Outcome::Failed:
        verdict.reason = "Google Play could not be asked (" +
                         (play.detail.empty() ? std::string("no detail") : play.detail) + ")";
        return verdict;
    case PlayUpdateQuery::Outcome::Available:
        break;
    }

    auto version = version_from_version_code(play.available_version_code);
    if (!version) {
        verdict.reason = "Google Play offers versionCode " +
                         std::to_string(play.available_version_code) +
                         ", which matches no release version";
        return verdict;
    }

    std::string manifest = manifest_version;
    if (!manifest.empty() && (manifest[0] == 'v' || manifest[0] == 'V')) {
        manifest.erase(0, 1);
    }
    verdict.kind = UpdateVerdict::Kind::Play;
    verdict.version = *version;
    verdict.manifest_describes_it = manifest == *version;
    return verdict;
}

#ifdef __ANDROID__
std::string installer_package() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) {
        return {};
    }

    // Cached global ref owned by helix_activity_class(), never released here.
    jclass cls = helix_activity_class(env);
    if (!cls) {
        return {};
    }

    jmethodID method = env->GetStaticMethodID(cls, "getInstallerPackage", "()Ljava/lang/String;");
    if (!method) {
        spdlog::error("[android_update_source] Failed to find getInstallerPackage method");
        env->ExceptionClear();
        return {};
    }

    auto j_result = static_cast<jstring>(env->CallStaticObjectMethod(cls, method));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (j_result) {
            env->DeleteLocalRef(j_result);
        }
        return {};
    }
    if (!j_result) {
        return {};
    }

    std::string result;
    if (const char* chars = env->GetStringUTFChars(j_result, nullptr)) {
        result.assign(chars);
        env->ReleaseStringUTFChars(j_result, chars);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(j_result);
    return result;
}

PlayUpdateQuery query_play_update(int timeout_sec) {
    PlayUpdateQuery query;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) {
        query.detail = "JNI env unavailable";
        return query;
    }
    jclass cls = helix_activity_class(env);
    if (!cls) {
        query.detail = "HelixActivity class not found";
        return query;
    }
    jmethodID method = env->GetStaticMethodID(cls, "queryPlayUpdate", "(I)J");
    if (!method) {
        env->ExceptionClear();
        query.detail = "queryPlayUpdate method not found";
        return query;
    }

    // Java returns the available versionCode, 0 for no update, or -1 when the
    // query failed (logged on the Java side under HelixPlayUpdate).
    const jlong code = env->CallStaticLongMethod(cls, method, static_cast<jint>(timeout_sec));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        query.detail = "queryPlayUpdate threw";
        return query;
    }
    if (code > 0) {
        query.outcome = PlayUpdateQuery::Outcome::Available;
        query.available_version_code = static_cast<int64_t>(code);
    } else if (code == 0) {
        query.outcome = PlayUpdateQuery::Outcome::NotAvailable;
    } else {
        query.detail = "query failed or timed out, see logcat HelixPlayUpdate";
    }
    return query;
}

bool start_play_update() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) {
        return false;
    }
    jclass cls = helix_activity_class(env);
    if (!cls) {
        return false;
    }
    jmethodID method =
        env->GetStaticMethodID(cls, "startPlayUpdate", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (!method) {
        spdlog::error("[android_update_source] Failed to find startPlayUpdate method");
        env->ExceptionClear();
        return false;
    }
    jstring j_market = env->NewStringUTF(kPlayStoreMarketUrl);
    jstring j_web = env->NewStringUTF(kPlayStoreWebUrl);
    bool started = false;
    if (j_market && j_web) {
        started = env->CallStaticBooleanMethod(cls, method, j_market, j_web) == JNI_TRUE;
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        started = false;
    }
    if (j_market) {
        env->DeleteLocalRef(j_market);
    }
    if (j_web) {
        env->DeleteLocalRef(j_web);
    }
    return started;
}
#endif

} // namespace helix::android

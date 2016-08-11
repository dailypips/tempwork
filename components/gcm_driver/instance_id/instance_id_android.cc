// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/instance_id/instance_id_android.h"

#include <stdint.h>

#include <memory>

#include "base/android/context_utils.h"
#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "jni/InstanceIDBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;

namespace instance_id {

InstanceIDAndroid::ScopedBlockOnAsyncTasksForTesting::
    ScopedBlockOnAsyncTasksForTesting() {
  JNIEnv* env = AttachCurrentThread();
  previous_value_ =
      Java_InstanceIDBridge_setBlockOnAsyncTasksForTesting(env, true);
}

InstanceIDAndroid::ScopedBlockOnAsyncTasksForTesting::
    ~ScopedBlockOnAsyncTasksForTesting() {
  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_setBlockOnAsyncTasksForTesting(env, previous_value_);
}

// static
bool InstanceIDAndroid::RegisterJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

// static
std::unique_ptr<InstanceID> InstanceID::Create(const std::string& app_id,
                                               gcm::GCMDriver* gcm_driver) {
  return base::WrapUnique(new InstanceIDAndroid(app_id, gcm_driver));
}

InstanceIDAndroid::InstanceIDAndroid(const std::string& app_id,
                                     gcm::GCMDriver* gcm_driver)
    : InstanceID(app_id, gcm_driver) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK(!app_id.empty()) << "Empty app_id is not supported";
  // The |app_id| is stored in GCM's category field by the desktop InstanceID
  // implementation, but because the category is reserved for the app's package
  // name on Android the subtype field is used instead.
  std::string subtype = app_id;

  JNIEnv* env = AttachCurrentThread();
  java_ref_.Reset(Java_InstanceIDBridge_create(
      env, reinterpret_cast<intptr_t>(this),
      base::android::GetApplicationContext(),
      ConvertUTF8ToJavaString(env, subtype).obj()));
}

InstanceIDAndroid::~InstanceIDAndroid() {
  DCHECK(thread_checker_.CalledOnValidThread());

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_destroy(env, java_ref_.obj());
}

void InstanceIDAndroid::GetID(const GetIDCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t request_id = get_id_callbacks_.Add(new GetIDCallback(callback));

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_getId(env, java_ref_.obj(), request_id);
}

void InstanceIDAndroid::GetCreationTime(
    const GetCreationTimeCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t request_id =
      get_creation_time_callbacks_.Add(new GetCreationTimeCallback(callback));

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_getCreationTime(env, java_ref_.obj(), request_id);
}

void InstanceIDAndroid::GetToken(
    const std::string& authorized_entity,
    const std::string& scope,
    const std::map<std::string, std::string>& options,
    const GetTokenCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t request_id = get_token_callbacks_.Add(new GetTokenCallback(callback));

  std::vector<std::string> options_strings;
  for (const auto& entry : options) {
    options_strings.push_back(entry.first);
    options_strings.push_back(entry.second);
  }

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_getToken(
      env, java_ref_.obj(), request_id,
      ConvertUTF8ToJavaString(env, authorized_entity).obj(),
      ConvertUTF8ToJavaString(env, scope).obj(),
      base::android::ToJavaArrayOfStrings(env, options_strings).obj());
}

void InstanceIDAndroid::DeleteTokenImpl(const std::string& authorized_entity,
                                        const std::string& scope,
                                        const DeleteTokenCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t request_id =
      delete_token_callbacks_.Add(new DeleteTokenCallback(callback));

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_deleteToken(
      env, java_ref_.obj(), request_id,
      ConvertUTF8ToJavaString(env, authorized_entity).obj(),
      ConvertUTF8ToJavaString(env, scope).obj());
}

void InstanceIDAndroid::DeleteIDImpl(const DeleteIDCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  int32_t request_id = delete_id_callbacks_.Add(new DeleteIDCallback(callback));

  JNIEnv* env = AttachCurrentThread();
  Java_InstanceIDBridge_deleteInstanceID(env, java_ref_.obj(), request_id);
}

void InstanceIDAndroid::DidGetID(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jint request_id,
    const base::android::JavaParamRef<jstring>& jid) {
  DCHECK(thread_checker_.CalledOnValidThread());

  GetIDCallback* callback = get_id_callbacks_.Lookup(request_id);
  DCHECK(callback);
  callback->Run(ConvertJavaStringToUTF8(jid));
  get_id_callbacks_.Remove(request_id);
}

void InstanceIDAndroid::DidGetCreationTime(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jint request_id,
    jlong creation_time_unix_ms) {
  DCHECK(thread_checker_.CalledOnValidThread());

  base::Time creation_time;
  // If the InstanceID's getId, getToken and deleteToken methods have never been
  // called, or deleteInstanceID has cleared it since, creation time will be 0.
  if (creation_time_unix_ms) {
    creation_time = base::Time::UnixEpoch() +
                    base::TimeDelta::FromMilliseconds(creation_time_unix_ms);
  }

  GetCreationTimeCallback* callback =
      get_creation_time_callbacks_.Lookup(request_id);
  DCHECK(callback);
  callback->Run(creation_time);
  get_creation_time_callbacks_.Remove(request_id);
}

void InstanceIDAndroid::DidGetToken(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jint request_id,
    const base::android::JavaParamRef<jstring>& jtoken) {
  DCHECK(thread_checker_.CalledOnValidThread());

  GetTokenCallback* callback = get_token_callbacks_.Lookup(request_id);
  DCHECK(callback);
  std::string token = ConvertJavaStringToUTF8(jtoken);
  callback->Run(
      token, token.empty() ? InstanceID::UNKNOWN_ERROR : InstanceID::SUCCESS);
  get_token_callbacks_.Remove(request_id);
}

void InstanceIDAndroid::DidDeleteToken(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jint request_id,
    jboolean success) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DeleteTokenCallback* callback = delete_token_callbacks_.Lookup(request_id);
  DCHECK(callback);
  callback->Run(success ? InstanceID::SUCCESS : InstanceID::UNKNOWN_ERROR);
  delete_token_callbacks_.Remove(request_id);
}

void InstanceIDAndroid::DidDeleteID(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jint request_id,
    jboolean success) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DeleteIDCallback* callback = delete_id_callbacks_.Lookup(request_id);
  DCHECK(callback);
  callback->Run(success ? InstanceID::SUCCESS : InstanceID::UNKNOWN_ERROR);
  delete_id_callbacks_.Remove(request_id);
}

}  // namespace instance_id

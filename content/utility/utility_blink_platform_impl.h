// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_UTILITY_UTILITY_BLINK_PLATFORM_IMPL_H_
#define CONTENT_UTILITY_UTILITY_BLINK_PLATFORM_IMPL_H_

#include <memory>

#include "base/macros.h"
#include "content/child/blink_platform_impl.h"

namespace content {

class WebThreadImplForUtilityThread;

class UtilityBlinkPlatformImpl : public BlinkPlatformImpl {
 public:
  UtilityBlinkPlatformImpl();
  ~UtilityBlinkPlatformImpl() override;

  // BlinkPlatformImpl implementation.
  blink::WebThread* currentThread() override;

 private:
  std::unique_ptr<WebThreadImplForUtilityThread> main_thread_;

  DISALLOW_COPY_AND_ASSIGN(UtilityBlinkPlatformImpl);
};

}  // namespace content

#endif  // CONTENT_UTILITY_UTILITY_BLINK_PLATFORM_IMPL_H_

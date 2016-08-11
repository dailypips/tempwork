// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_NON_PERSISTENT_NOTIFICATION_HANDLER_H_
#define CHROME_BROWSER_NOTIFICATIONS_NON_PERSISTENT_NOTIFICATION_HANDLER_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/notifications/notification_handler.h"

class NotificationDelegate;

// NotificationHandler implementation for non persistent notifications.
class NonPersistentNotificationHandler : public NotificationHandler {
 public:
  NonPersistentNotificationHandler();
  ~NonPersistentNotificationHandler() override;

  // NotificationHandler implementation
  void OnClose(Profile* profile,
               const std::string& origin,
               const std::string& notification_id,
               bool by_user) override;

  void OnClick(Profile* profile,
               const std::string& origin,
               const std::string& notification_id,
               int action_index) override;

  void OpenSettings(Profile* profile) override;

  void RegisterNotification(const std::string& notification_id,
                            NotificationDelegate* delegate) override;

 private:
  // map of delegate objects keyed by notification id.
  std::unordered_map<std::string, scoped_refptr<NotificationDelegate>>
      notifications_;

  DISALLOW_COPY_AND_ASSIGN(NonPersistentNotificationHandler);
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_NON_PERSISTENT_NOTIFICATION_HANDLER_H_

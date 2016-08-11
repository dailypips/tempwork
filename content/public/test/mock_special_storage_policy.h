// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_MOCK_SPECIAL_STORAGE_POLICY_H_
#define CONTENT_PUBLIC_TEST_MOCK_SPECIAL_STORAGE_POLICY_H_

#include <set>
#include <string>

#include "storage/browser/quota/special_storage_policy.h"
#include "url/gurl.h"

using storage::SpecialStoragePolicy;

namespace content {

class MockSpecialStoragePolicy : public storage::SpecialStoragePolicy {
 public:
  MockSpecialStoragePolicy();

  bool IsStorageProtected(const GURL& origin) override;
  bool IsStorageUnlimited(const GURL& origin) override;
  bool IsStorageSessionOnly(const GURL& origin) override;
  bool CanQueryDiskSize(const GURL& origin) override;
  bool HasIsolatedStorage(const GURL& origin) override;
  bool HasSessionOnlyOrigins() override;
  bool IsStorageDurable(const GURL& origin) override;

  void AddProtected(const GURL& origin) {
    protected_.insert(origin);
  }

  void AddUnlimited(const GURL& origin) {
    unlimited_.insert(origin);
  }

  void RemoveUnlimited(const GURL& origin) {
    unlimited_.erase(origin);
  }

  void AddSessionOnly(const GURL& origin) {
    session_only_.insert(origin);
  }

  void GrantQueryDiskSize(const GURL& origin) {
    can_query_disk_size_.insert(origin);
  }

  void AddIsolated(const GURL& origin) {
    isolated_.insert(origin);
  }

  void RemoveIsolated(const GURL& origin) {
    isolated_.erase(origin);
  }

  void SetAllUnlimited(bool all_unlimited) {
    all_unlimited_ = all_unlimited;
  }

  void AddDurable(const GURL& origin) {
    durable_.insert(origin);
  }

  void Reset() {
    protected_.clear();
    unlimited_.clear();
    session_only_.clear();
    can_query_disk_size_.clear();
    file_handlers_.clear();
    isolated_.clear();
    all_unlimited_ = false;
  }

  void NotifyGranted(const GURL& origin, int change_flags) {
    SpecialStoragePolicy::NotifyGranted(origin, change_flags);
  }

  void NotifyRevoked(const GURL& origin, int change_flags) {
    SpecialStoragePolicy::NotifyRevoked(origin, change_flags);
  }

  void NotifyCleared() {
    SpecialStoragePolicy::NotifyCleared();
  }

 protected:
  ~MockSpecialStoragePolicy() override;

 private:
  std::set<GURL> protected_;
  std::set<GURL> unlimited_;
  std::set<GURL> session_only_;
  std::set<GURL> can_query_disk_size_;
  std::set<GURL> isolated_;
  std::set<GURL> durable_;
  std::set<std::string> file_handlers_;

  bool all_unlimited_;
};
}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_MOCK_SPECIAL_STORAGE_POLICY_H_

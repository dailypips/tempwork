// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_FACTORY_H_
#define CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_FACTORY_H_

#include <memory>
#include <string>

#include "base/macros.h"

class Profile;

namespace browsing_data {
class BrowsingDataCounter;
}

class BrowsingDataCounterFactory {
 public:
  // Creates a new instance of BrowsingDataCounter that is counting the data
  // for |profile|, related to a given deletion preference |pref_name|.
  static std::unique_ptr<browsing_data::BrowsingDataCounter>
  GetForProfileAndPref(Profile* profile, const std::string& pref_name);

 private:
  BrowsingDataCounterFactory();
  ~BrowsingDataCounterFactory();

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataCounterFactory);
};

#endif  // CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_COUNTER_FACTORY_H_

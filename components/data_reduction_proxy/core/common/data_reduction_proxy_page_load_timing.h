// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_REDUCTION_PROXY_CORE_COMMON_DATA_REDUCTION_PROXY_PAGE_LOAD_TIMING_H
#define COMPONENTS_DATA_REDUCTION_PROXY_CORE_COMMON_DATA_REDUCTION_PROXY_PAGE_LOAD_TIMING_H

#include "base/optional.h"
#include "base/time/time.h"

namespace data_reduction_proxy {

// The timing information that is relevant to the Pageload metrics pingback.
struct DataReductionProxyPageLoadTiming {
  DataReductionProxyPageLoadTiming(
      const base::Time& navigation_start,
      const base::Optional<base::TimeDelta>& response_start,
      const base::Optional<base::TimeDelta>& load_event_start,
      const base::Optional<base::TimeDelta>& first_image_paint,
      const base::Optional<base::TimeDelta>& first_contentful_paint);

  DataReductionProxyPageLoadTiming(
      const DataReductionProxyPageLoadTiming& other);

  // Time that the navigation for the associated page was initiated.
  const base::Time navigation_start;

  // All TimeDeltas are relative to navigation_start

  // Time that the first byte of the response is received.
  const base::Optional<base::TimeDelta> response_start;

  // Time immediately before the load event is fired.
  const base::Optional<base::TimeDelta> load_event_start;

  // Time when the first image is painted.
  const base::Optional<base::TimeDelta> first_image_paint;
  // Time when the first contentful thing (image, text, etc.) is painted.
  const base::Optional<base::TimeDelta> first_contentful_paint;
};

}  // namespace data_reduction_proxy

#endif  // COMPONENTS_DATA_REDUCTION_PROXY_CORE_COMMON_DATA_REDUCTION_PROXY_PAGE_LOAD_TIMING_H

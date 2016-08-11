// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_SURFACES_SURFACES_STATE_H_
#define SERVICES_UI_SURFACES_SURFACES_STATE_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/surfaces/surface_manager.h"

namespace cc {
class SurfaceHittest;
class SurfaceManager;
}  // namespace cc

namespace ui {

// The SurfacesState object is an object global to the Window Manager app that
// holds the SurfaceManager and allocates new Surfaces namespaces.
// This object lives on the main thread of the Window Manager.
// TODO(rjkroege, fsamuel): This object will need to change to support multiple
// displays.
class SurfacesState : public base::RefCounted<SurfacesState> {
 public:
  SurfacesState();

  uint32_t next_client_id() { return next_client_id_++; }

  cc::SurfaceManager* manager() { return &manager_; }

 private:
  friend class base::RefCounted<SurfacesState>;
  ~SurfacesState();

  // A Surface ID is an unsigned 64-bit int where the high 32-bits are generated
  // by the Surfaces service, and the low 32-bits are generated by the process
  // that requested the Surface.
  uint32_t next_client_id_;
  cc::SurfaceManager manager_;

  DISALLOW_COPY_AND_ASSIGN(SurfacesState);
};

}  // namespace ui

#endif  //  SERVICES_UI_SURFACES_SURFACES_STATE_H_

// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/gpu/drm_render_node_path_finder.h"

#include <fcntl.h>
#include <gbm.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/files/scoped_file.h"
#include "base/strings/stringprintf.h"
#include "ui/gfx/linux/scoped_gbm_device.h"  // nogncheck
#include "ui/gl/gl_display_manager.h"
#include "ui/gl/gpu_preference.h"

namespace ui {

namespace {

// Drm render node path template.
constexpr char kDriRenderNodeTemplate[] = "/dev/dri/renderD%u";

// Number of files to look for when discovering DRM devices.
constexpr uint32_t kDrmMajor = 226;
constexpr uint32_t kDrmMaxMinor = 15;
constexpr uint32_t kRenderNodeStart = 128;
constexpr uint32_t kRenderNodeEnd = kRenderNodeStart + kDrmMaxMinor + 1;

}  // namespace

DrmRenderNodePathFinder::DrmRenderNodePathFinder() {
  FindDrmRenderNodePath();
}

DrmRenderNodePathFinder::~DrmRenderNodePathFinder() = default;

base::FilePath DrmRenderNodePathFinder::GetDrmRenderNodePath() const {
  return drm_render_node_path_;
}

void DrmRenderNodePathFinder::FindDrmRenderNodePath() {
  dev_t system_device_id = static_cast<dev_t>(
      gl::GLDisplayManagerEGL::GetInstance()->GetSystemDeviceId(
          gl::GpuPreference::kDefault));
  if (system_device_id != 0) {
    // If GPU info discovery selected a specific render node, try using that
    // first.
    std::string dri_render_node(
        base::StringPrintf(kDriRenderNodeTemplate, minor(system_device_id)));
    base::ScopedFD drm_fd(open(dri_render_node.c_str(), O_RDWR));
    if (drm_fd.get() >= 0) {
      // Check if GBM actually supports the device.
      ScopedGbmDevice device(gbm_create_device(drm_fd.get()));
      if (device) {
        drm_render_node_path_ = base::FilePath(dri_render_node);
        return;
      }
    }
  }

  for (uint32_t i = kRenderNodeStart; i < kRenderNodeEnd; i++) {
    /* First,  look in sysfs and skip if this is the vgem render node. */
    std::string node_link(
        base::StringPrintf("/sys/dev/char/%d:%d/device", kDrmMajor, i));
    char device_link[256];
    ssize_t len = readlink(node_link.c_str(), device_link, sizeof(device_link));
    if (len < 0 || len == sizeof(device_link))
      continue;

    /* readlink does not place a nul byte at the end of the string. */
    if (len >= 4 && memcmp(device_link + len - 4, "vgem", 4) == 0)
      continue;

    std::string dri_render_node(base::StringPrintf(kDriRenderNodeTemplate, i));
    base::ScopedFD drm_fd(open(dri_render_node.c_str(), O_RDWR));
    if (drm_fd.get() < 0)
      continue;

    // In case the first node /dev/dri/renderD128 can be opened but fails to
    // create gbm device on certain driver (E.g. PowerVR). Skip such paths.
    ScopedGbmDevice device(gbm_create_device(drm_fd.get()));
    if (!device)
      continue;

    drm_render_node_path_ = base::FilePath(dri_render_node);
    break;
  }
}

}  // namespace ui

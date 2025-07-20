// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_wp_color_management_output.h"

#include "base/logging.h"
#include "ui/display/util/display_util.h"
#include "ui/gfx/display_color_spaces.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_output.h"
#include "ui/ozone/platform/wayland/host/wayland_wp_color_manager.h"

namespace ui {

WaylandWpColorManagementOutput::WaylandWpColorManagementOutput(
    wp_color_management_output_v1* color_management_output,
    WaylandOutput* wayland_output,
    WaylandConnection* connection)
    : color_management_output_(color_management_output),
      wayland_output_(wayland_output),
      connection_(connection) {
  DCHECK(color_management_output_);
  static constexpr wp_color_management_output_v1_listener kListener = {
      .image_description_changed = &OnImageDescriptionChanged,
  };
  wp_color_management_output_v1_add_listener(color_management_output_.get(),
                                             &kListener, this);

  // Get the initial color space.
  GetCurrentColorSpace();
}

WaylandWpColorManagementOutput::~WaylandWpColorManagementOutput() = default;

void WaylandWpColorManagementOutput::GetCurrentColorSpace() {
  auto image_description_object = wl::Object<wp_image_description_v1>(
      wp_color_management_output_v1_get_image_description(
          color_management_output_.get()));

  image_description_ = base::MakeRefCounted<WaylandWpImageDescription>(
      std::move(image_description_object), connection_, std::nullopt,
      base::BindOnce(&WaylandWpColorManagementOutput::OnImageDescription,
                     weak_factory_.GetWeakPtr()));
}

void WaylandWpColorManagementOutput::OnImageDescription(
    scoped_refptr<WaylandWpImageDescription> image_description) {
  if (!image_description) {
    LOG(ERROR) << "Failed to get output image description.";
    return;
  }
  CHECK_EQ(image_description, image_description_);

  if (display::HasForceDisplayColorProfile()) {
    display_color_spaces_ =
        gfx::DisplayColorSpaces(display::GetForcedDisplayColorProfile());
  } else if (connection_->wp_color_manager()->IsSupportedTransferFunction(
                 WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB)) {
    display_color_spaces_ = gfx::DisplayColorSpaces();
  } else {
    display_color_spaces_ = gfx::DisplayColorSpaces(
        gfx::ColorSpace(gfx::ColorSpace::PrimaryID::BT709,
                        gfx::ColorSpace::TransferID::GAMMA22));
  }

  // Set HDR metadata and derive luminance values.
  const auto& hdr_metadata = image_description->hdr_metadata();

  // GetContentMaxLuminance returns a default of 1000 if the metadata does
  // not contain a peak luminance. Avoid this by checking first.
  if ((hdr_metadata.cta_861_3 &&
       hdr_metadata.cta_861_3->max_content_light_level > 0) ||
      (hdr_metadata.smpte_st_2086 &&
       hdr_metadata.smpte_st_2086->luminance_max > 0)) {
    float peak_brightness =
        gfx::HDRMetadata::GetContentMaxLuminance(hdr_metadata);
    float sdr_nits = hdr_metadata.ndwl ? hdr_metadata.ndwl->nits
                                       : gfx::ColorSpace::kDefaultSDRWhiteLevel;
    if (sdr_nits > 0.f) {
      display_color_spaces_.SetHDRMaxLuminanceRelative(peak_brightness /
                                                       sdr_nits);
    }
  }

  if (!display::HasForceDisplayColorProfile()) {
    auto color_space = image_description->gfx_color_space();
    display_color_spaces_.SetPrimaries(color_space.GetPrimaries());
    if (color_space.IsWide()) {
      for (const bool needs_alpha : {false, true}) {
        auto buffer_format = display_color_spaces_.GetOutputBufferFormat(
            gfx::ContentColorUsage::kWideColorGamut, needs_alpha);
        display_color_spaces_.SetOutputColorSpaceAndBufferFormat(
            gfx::ContentColorUsage::kWideColorGamut, needs_alpha, color_space,
            buffer_format);
      }
    }
    auto hdr_color_space = color_space;
    if (display_color_spaces_.GetHDRMaxLuminanceRelative() > 1.f &&
        !hdr_color_space.IsHDR()) {
      if (connection_->wp_color_manager()->IsSupportedTransferFunction(
              WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ)) {
        hdr_color_space = gfx::ColorSpace::CreateHDR10();
      } else if (connection_->wp_color_manager()->IsSupportedTransferFunction(
                     WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG)) {
        hdr_color_space = gfx::ColorSpace::CreateHLG();
      }
    }
    if (hdr_color_space.IsWide() || hdr_color_space.IsHDR()) {
      for (const bool needs_alpha : {false, true}) {
        auto buffer_format = display_color_spaces_.GetOutputBufferFormat(
            gfx::ContentColorUsage::kHDR, needs_alpha);
        display_color_spaces_.SetOutputColorSpaceAndBufferFormat(
            gfx::ContentColorUsage::kHDR, needs_alpha, hdr_color_space,
            buffer_format);
      }
    }
  }

  wayland_output_->TriggerDelegateNotifications();
}

// static
void WaylandWpColorManagementOutput::OnImageDescriptionChanged(
    void* data,
    wp_color_management_output_v1* management_output) {
  auto* self = static_cast<WaylandWpColorManagementOutput*>(data);
  DCHECK(self);
  self->GetCurrentColorSpace();
}

}  // namespace ui

// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_wp_color_management_output.h"

#include "base/logging.h"
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

  auto color_space = image_description->gfx_color_space();
  display_color_spaces_ = gfx::DisplayColorSpaces(color_space);

  // Set HDR metadata and derive luminance values.
  const auto& hdr_metadata = image_description->hdr_metadata();

  if ((hdr_metadata.cta_861_3 &&
       hdr_metadata.cta_861_3->max_content_light_level > 0) ||
      (hdr_metadata.smpte_st_2086 &&
       hdr_metadata.smpte_st_2086->luminance_max > 0) ||
      display_color_spaces_.SupportsHDR()) {
    float peak_brightness =
        gfx::HDRMetadata::GetContentMaxLuminance(hdr_metadata);
    float sdr_nits = hdr_metadata.ndwl ? hdr_metadata.ndwl->nits
                                       : gfx::ColorSpace::kDefaultSDRWhiteLevel;
    if (sdr_nits > 0.f) {
      display_color_spaces_.SetHDRMaxLuminanceRelative(peak_brightness /
                                                       sdr_nits);
    }
  }

  if (color_space.IsHDR()) {
    gfx::ColorSpace::TransferID sdr_transfer =
        gfx::ColorSpace::TransferID::INVALID;
    if (connection_->wp_color_manager()->IsSupportedTransferFunction(
            WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB)) {
      sdr_transfer = gfx::ColorSpace::TransferID::SRGB;
    } else if (connection_->wp_color_manager()->IsSupportedFeature(
                   WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER) ||
               connection_->wp_color_manager()->IsSupportedTransferFunction(
                   WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22)) {
      sdr_transfer = gfx::ColorSpace::TransferID::GAMMA22;
    }

    gfx::ColorSpace::PrimaryID sdr_primaries =
        gfx::ColorSpace::PrimaryID::INVALID;
    if (connection_->wp_color_manager()->IsSupportedFeature(
            WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES) ||
        connection_->wp_color_manager()->IsSupportedPrimaries(
            WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3)) {
      sdr_primaries = gfx::ColorSpace::PrimaryID::P3;
    } else if (connection_->wp_color_manager()->IsSupportedPrimaries(
                   WP_COLOR_MANAGER_V1_PRIMARIES_SRGB)) {
      sdr_primaries = gfx::ColorSpace::PrimaryID::BT709;
    }

    gfx::ColorSpace sdr_color_space(sdr_primaries, sdr_transfer);
    if (sdr_color_space.IsValid()) {
      for (const auto color_usage : {gfx::ContentColorUsage::kSRGB,
                                     gfx::ContentColorUsage::kWideColorGamut}) {
        for (const bool needs_alpha : {false, true}) {
          auto buffer_format = display_color_spaces_.GetOutputBufferFormat(
              color_usage, needs_alpha);
          display_color_spaces_.SetOutputColorSpaceAndBufferFormat(
              color_usage, needs_alpha, sdr_color_space, buffer_format);
        }
      }
    }
  }

  if (display_color_spaces_.SupportsHDR() &&
      (connection_->wp_color_manager()->IsSupportedFeature(
           WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES) ||
       connection_->wp_color_manager()->IsSupportedPrimaries(
           WP_COLOR_MANAGER_V1_PRIMARIES_SRGB)) &&
      (connection_->wp_color_manager()->IsSupportedFeature(
           WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER) ||
       connection_->wp_color_manager()->IsSupportedTransferFunction(
           WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR))) {
    for (const bool needs_alpha : {false, true}) {
      auto buffer_format = display_color_spaces_.GetOutputBufferFormat(
          gfx::ContentColorUsage::kHDR, needs_alpha);
      display_color_spaces_.SetOutputColorSpaceAndBufferFormat(
          gfx::ContentColorUsage::kHDR, needs_alpha,
          gfx::ColorSpace::CreateSRGBLinear(), buffer_format);
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

// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/themes/theme_service_aura_linux.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/custom_theme_supplier.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "ui/color/system_theme.h"
#include "ui/gfx/image/image.h"
#include "ui/linux/linux_ui.h"
#include "ui/linux/linux_ui_factory.h"
#include "ui/native_theme/native_theme_aura.h"

ThemeServiceAuraLinux::~ThemeServiceAuraLinux() = default;

ui::SystemTheme ThemeServiceAuraLinux::GetDefaultSystemTheme() const {
  return GetSystemThemeForProfile(profile());
}

// static
ui::SystemTheme ThemeServiceAuraLinux::GetSystemThemeForProfile(
    const Profile* profile) {
  return ui::SystemTheme::kDefault;
}

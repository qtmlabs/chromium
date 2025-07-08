// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_LINUX_H_
#define COMPONENTS_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_LINUX_H_

#include <string>

#include "base/files/scoped_file.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/power_monitor/power_monitor_source.h"

namespace dbus {
class Bus;
class Signal;
class Response;
}  // namespace dbus

// A PowerMonitorSource that observes sleep/resume signals issued by systemd on
// Linux systems.
class PowerMonitorDeviceSourceLinux : public base::PowerMonitorSource {
 public:
  PowerMonitorDeviceSourceLinux();
  PowerMonitorDeviceSourceLinux(const PowerMonitorDeviceSourceLinux&) = delete;
  PowerMonitorDeviceSourceLinux& operator=(
      const PowerMonitorDeviceSourceLinux&) = delete;
  ~PowerMonitorDeviceSourceLinux() override;

  // base::PowerMonitorSource:
  base::PowerStateObserver::BatteryPowerStatus GetBatteryPowerStatus()
      const override;

 private:
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool connected);
  void OnPrepareForSleep(dbus::Signal* signal);
  void AcquireDelayInhibitor();
  void OnInhibitResponse(dbus::Response* response);

  scoped_refptr<dbus::Bus> bus_;
  base::ScopedFD inhibitor_fd_;
  base::WeakPtrFactory<PowerMonitorDeviceSourceLinux> weak_ptr_factory_{this};
};

#endif  // COMPONENTS_POWER_MONITOR_POWER_MONITOR_DEVICE_SOURCE_LINUX_H_

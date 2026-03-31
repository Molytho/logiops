/*
 * Copyright 2019-2023 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <system_error>

#include <libudev.h>

#include "backend/Error.h"
#include "backend/hidpp/Report.h"
#include "backend/raw/DeviceMonitor.h"
#include "backend/raw/IOMonitor.h"
#include "backend/raw/RawDevice.h"
#include "util/log.h"
#include "util/task.h"

using namespace logid;
using namespace logid::backend::raw;

namespace {
    std::unique_ptr<udev, UdevDelete> create_udev_context() {
        std::unique_ptr<udev, UdevDelete> res {udev_new()};
        if (!res) {
            throw std::runtime_error("udev_new failed");
        }
        return res;
    }

    std::unique_ptr<udev_monitor, UdevDelete> create_udev_monitor(udev *context) {
        std::unique_ptr<udev_monitor, UdevDelete> res {udev_monitor_new_from_netlink(context, "udev")};
        if (!res) {
            throw std::runtime_error("udev_monitor_new_from_netlink failed");
        }
        return res;
    }

    std::unique_ptr<udev_enumerate, UdevDelete> create_udev_enumerate(udev *context) {
        std::unique_ptr<udev_enumerate, UdevDelete> res {udev_enumerate_new(context)};
        if (!res) {
            throw std::runtime_error("udev_enumerate_new failed");
        }
        return res;
    }

    std::unique_ptr<udev_device, UdevDelete> receive_device_from_udev_monitor(udev_monitor *monitor) {
        std::unique_ptr<udev_device, UdevDelete> device {udev_monitor_receive_device(monitor)};
        if (!device) {
            throw std::runtime_error("udev_monitor_receive_device failed");
        }
        return device;
    }
} // namespace

void UdevDelete::operator()(udev *ptr) const noexcept {
    udev_unref(ptr);
}

void UdevDelete::operator()(udev_monitor *ptr) const noexcept {
    udev_monitor_unref(ptr);
}

void UdevDelete::operator()(udev_device *ptr) const noexcept {
    udev_device_unref(ptr);
}

void UdevDelete::operator()(udev_enumerate *ptr) const noexcept {
    udev_enumerate_unref(ptr);
}

DeviceMonitor::DeviceMonitor() :
        _io_monitor(std::make_shared<IOMonitor>()), _udev_context {create_udev_context()},
        _udev_monitor(create_udev_monitor(_udev_context.get())), _ready(false) {
    if (auto ret = udev_monitor_filter_add_match_subsystem_devtype(_udev_monitor.get(), "hidraw", nullptr);
        ret != 0) {
        throw std::system_error(-ret,
            std::system_category(),
            "udev_monitor_filter_add_match_subsystem_devtype");
    }

    if (auto ret = udev_monitor_enable_receiving(_udev_monitor.get()); ret != 0) {
        throw std::system_error(-ret, std::system_category(), "udev_monitor_enable_receiving");
    }
}

DeviceMonitor::~DeviceMonitor() {
    if (_ready) {
        _io_monitor->remove(getMonitorFd());
    }
}

void DeviceMonitor::ready() {
    if (_ready) {
        return;
    }
    _ready = true;

    _io_monitor->add(getMonitorFd(),
        {[self_weak = weak_from_this()]() {
             if (auto self = self_weak.lock()) {
                 auto device          = receive_device_from_udev_monitor(self->_udev_monitor.get());
                 std::string action   = udev_device_get_action(device.get());
                 std::string dev_node = udev_device_get_devnode(device.get());

                 if (action == "add") {
                     run_task([self_weak, dev_node]() {
                         if (auto self = self_weak.lock()) {
                             self->_addHandler(dev_node);
                         }
                     });
                 } else if (action == "remove") {
                     run_task([self_weak, dev_node]() {
                         if (auto self = self_weak.lock()) {
                             self->_removeHandler(dev_node);
                         }
                     });
                 }
             }
         },
            []() { throw std::runtime_error("udev hangup"); },
            []() { throw std::runtime_error("udev error"); }});
}

void DeviceMonitor::enumerate() {
    auto udev_enum = create_udev_enumerate(_udev_context.get());
    if (auto ret = udev_enumerate_add_match_subsystem(udev_enum.get(), "hidraw"); ret != 0) {
        throw std::system_error(-ret, std::system_category(), "udev_enumerate_add_match_subsystem");
    }

    if (auto ret = udev_enumerate_scan_devices(udev_enum.get()); ret != 0) {
        throw std::system_error(-ret, std::system_category(), "udev_enumerate_scan_devices");
    }

    udev_list_entry *udev_enum_entry;
    udev_list_entry_foreach(udev_enum_entry, udev_enumerate_get_list_entry(udev_enum.get())) {
        const char *name = udev_list_entry_get_name(udev_enum_entry);

        std::unique_ptr<udev_device, UdevDelete> device {udev_device_new_from_syspath(_udev_context.get(), name)};
        if (device) {
            std::string dev_node = udev_device_get_devnode(device.get());
            if (!dev_node.empty()) {
                _addHandler(dev_node);
            }
        }
    }
}

void DeviceMonitor::_addHandler(const std::string &device, int tries) {
    try {
        auto supported_reports = backend::hidpp::getSupportedReports(RawDevice::getReportDescriptor(device));
        if (supported_reports) {
            addDevice(device);
        } else {
            logPrintf(DEBUG, "Unsupported device %s ignored", device.c_str());
        }
    } catch (backend::DeviceNotReady &e) {
        if (tries == max_tries) {
            logPrintf(WARN, "Failed to add device %s after %d tries. Treating as failure.", device.c_str(), max_tries);
        } else {
            /* Do exponential backoff for 2^tries * backoff ms. */
            std::chrono::milliseconds wait((1 << tries) * ready_backoff);
            logPrintf(DEBUG,
                "Failed to add device %s on try %d, backing off for %dms",
                device.c_str(),
                tries + 1,
                wait.count());
            run_task_after(
                [self_weak = weak_from_this(), device, tries]() {
                    if (auto self = self_weak.lock()) {
                        self->_addHandler(device, tries + 1);
                    }
                },
                wait
            );
        }
    } catch (std::exception &e) {
        logPrintf(WARN, "Error adding device %s: %s", device.c_str(), e.what());
    }
}

void DeviceMonitor::_removeHandler(const std::string &device) {
    try {
        removeDevice(device);
    } catch (std::exception &e) {
        logPrintf(WARN, "Error removing device %s: %s", device.c_str(), e.what());
    }
}

int DeviceMonitor::getMonitorFd() const noexcept {
    return udev_monitor_get_fd(_udev_monitor.get());
}

std::shared_ptr<IOMonitor> DeviceMonitor::ioMonitor() const {
    return _io_monitor;
}

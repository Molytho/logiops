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

#ifndef LOGID_BACKEND_RAW_DEVICEMONITOR_H
#define LOGID_BACKEND_RAW_DEVICEMONITOR_H

#include <memory>
#include <string>

extern "C" {
    struct udev;
    struct udev_monitor;
    struct udev_device;
    struct udev_enumerate;
}

namespace logid::backend::raw {
    class IOMonitor;

    constexpr int max_tries     = 5;
    constexpr int ready_backoff = 500;

    struct UdevDelete {
        void operator()(udev *ptr) const noexcept;
        void operator()(udev_monitor *ptr) const noexcept;
        void operator()(udev_device *ptr) const noexcept;
        void operator()(udev_enumerate *ptr) const noexcept;
    };

    class DeviceMonitor : public std::enable_shared_from_this<DeviceMonitor> {
    public:
        virtual ~DeviceMonitor();

        void enumerate();

        [[nodiscard]] std::shared_ptr<IOMonitor> ioMonitor() const;

    protected:
        DeviceMonitor();

        // This should be run once the derived class is ready
        void ready();

        virtual void addDevice(std::string device) = 0;

        virtual void removeDevice(std::string device) = 0;

    private:
        void _addHandler(const std::string &device, int tries = 0);

        void _removeHandler(const std::string &device);

        int getMonitorFd() const noexcept;

        std::shared_ptr<IOMonitor> _io_monitor;

        std::unique_ptr<udev, UdevDelete> _udev_context;
        std::unique_ptr<udev_monitor, UdevDelete> _udev_monitor;
        bool _ready;
    };

    template<class T>
    struct DeviceMonitorImplHelper : public DeviceMonitor {
        template<class... Args>
        static std::shared_ptr<T> make(Args &&...args) {
            static_assert(std::derived_from<T, DeviceMonitor>);
            auto device_monitor = std::make_shared<T>(std::forward<Args>(args)...);
            device_monitor->ready();
            return device_monitor;
        }

        std::shared_ptr<T> shared_from_this() {
            return std::dynamic_pointer_cast<T>(
                static_cast<T *>(this)->std::template enable_shared_from_this<DeviceMonitor>::shared_from_this()
            );
        }

        std::shared_ptr<const T> shared_from_this() const {
            return std::dynamic_pointer_cast<T>(
                static_cast<const T *>(this)->std::template enable_shared_from_this<DeviceMonitor>::shared_from_this()
            );
        }
    };
} // namespace logid::backend::raw

#endif // LOGID_BACKEND_RAW_DEVICEMONITOR_H
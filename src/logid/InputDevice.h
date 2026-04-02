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

#ifndef LOGID_INPUTDEVICE_H
#define LOGID_INPUTDEVICE_H

#include <bitset>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

namespace logid {
    class InvalidEventCode : public std::runtime_error {
    public:
        explicit InvalidEventCode(const std::string &name);

        explicit InvalidEventCode(uint code);
    };

    struct EvdevDelete {
        void operator()(libevdev *ptr) const noexcept;
        void operator()(libevdev_uinput *ptr) const noexcept;
    };

    class InputDevice {
    public:
        explicit InputDevice(const char *name);

        void registerKey(uint code);

        void registerAxis(uint axis);

        void moveAxis(uint axis, int movement);

        void pressKey(uint code);

        void releaseKey(uint code);

        static std::string toKeyName(uint code);

        static uint toKeyCode(const std::string &name);

        static std::string toAxisName(uint code);

        static uint toAxisCode(const std::string &name);

        static int getLowResAxis(uint axis_code);

    private:
        void _sendEvent(uint type, uint code, int value);

        void _enableEvent(uint type, uint name);

        static std::string _toEventName(uint type, uint code);

        static uint _toEventCode(uint type, const std::string &name);

        std::bitset<KEY_CNT> registered_keys {};
        std::bitset<REL_CNT> registered_axis {};
        std::unique_ptr<libevdev, EvdevDelete> device;
        std::unique_ptr<libevdev_uinput, EvdevDelete> ui_device {};

        std::mutex _input_mutex;
    };
} // namespace logid

#endif // LOGID_INPUTDEVICE_H

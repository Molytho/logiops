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

#include <InputDevice.h>

#include <mutex>
#include <system_error>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include "util/log.h"

using namespace logid;

namespace {
    std::unique_ptr<libevdev, EvdevDelete> create_evdev() {
        std::unique_ptr<libevdev, EvdevDelete> res {libevdev_new()};
        if (!res) {
            throw std::runtime_error("libevdev_new failed");
        }
        return res;
    }

    std::unique_ptr<libevdev_uinput, EvdevDelete> create_evdev_uinput(libevdev *dev) {
        libevdev_uinput *uinput_device = nullptr;
        int res = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_device);
        if (res < 0) {
            throw std::system_error(-res, std::generic_category());
        }
        return std::unique_ptr<libevdev_uinput, EvdevDelete> {uinput_device};
    }
} // namespace

InvalidEventCode::InvalidEventCode(const std::string &name) :
        std::runtime_error::runtime_error("Invalid event code " + name) { }

InvalidEventCode::InvalidEventCode(uint code) : InvalidEventCode(std::to_string(code)) { }

void EvdevDelete::operator()(libevdev *ptr) const noexcept {
    libevdev_free(ptr);
}

void EvdevDelete::operator()(libevdev_uinput *ptr) const noexcept {
    libevdev_uinput_destroy(ptr);
}

InputDevice::InputDevice(const char *name) : device(create_evdev()) {
    libevdev_set_name(device.get(), name);
    libevdev_enable_event_type(device.get(), EV_KEY);
    // Enable some keys which a normal keyboard should have
    // by default, i.e. a-z, modifier keys and so on, see:
    // /usr/include/linux/input-event-codes.h
    for (unsigned int i = 0; i < 128; i++) {
        registered_keys.set(i);
        libevdev_enable_event_code(device.get(), EV_KEY, i, nullptr);
    }
    libevdev_enable_event_type(device.get(), EV_REL);

    ui_device = create_evdev_uinput(device.get());
}

void InputDevice::registerKey(uint code) {
    if (code >= KEY_CNT || registered_keys.test(code)) {
        logPrintf(WARN, "Tried to register invalid or occupied key code: %u", code);
        return;
    }

    _enableEvent(EV_KEY, code);

    registered_keys.set(code);
}

void InputDevice::registerAxis(uint axis) {
    if (axis >= REL_CNT || registered_axis.test(axis)) {
        logPrintf(WARN, "Tried to register invalid or occupied axis: %u", axis);
        return;
    }

    _enableEvent(EV_REL, axis);

    registered_axis.set(axis);
}

void InputDevice::moveAxis(uint axis, int movement) {
    _sendEvent(EV_REL, axis, movement);
}

void InputDevice::pressKey(uint code) {
    _sendEvent(EV_KEY, code, 1);
}

void InputDevice::releaseKey(uint code) {
    _sendEvent(EV_KEY, code, 0);
}

std::string InputDevice::toKeyName(uint code) {
    return _toEventName(EV_KEY, code);
}

uint InputDevice::toKeyCode(const std::string &name) {
    return _toEventCode(EV_KEY, name);
}

std::string InputDevice::toAxisName(uint code) {
    return _toEventName(EV_REL, code);
}

uint InputDevice::toAxisCode(const std::string &name) {
    return _toEventCode(EV_REL, name);
}

/* Returns -1 if axis_code is not hi-res */
int InputDevice::getLowResAxis(const uint axis_code) {
    /* Some systems don't have these hi-res axes */
#ifdef REL_WHEEL_HI_RES
    if (axis_code == REL_WHEEL_HI_RES) {
        return REL_WHEEL;
    }
#endif
#ifdef REL_HWHEEL_HI_RES
    if (axis_code == REL_HWHEEL_HI_RES) {
        return REL_HWHEEL;
    }
#endif

    return -1;
}

std::string InputDevice::_toEventName(uint type, uint code) {
    const char *ret = libevdev_event_code_get_name(type, code);

    if (!ret) {
        throw InvalidEventCode(code);
    }

    return {ret};
}

uint InputDevice::_toEventCode(uint type, const std::string &name) {
    int code = libevdev_event_code_from_name(type, name.c_str());

    if (code == -1) {
        throw InvalidEventCode(name);
    }

    return code;
}

void InputDevice::_enableEvent(const uint type, const uint code) {
    std::lock_guard lock(_input_mutex);
    libevdev_enable_event_code(device.get(), type, code, nullptr);
    ui_device = create_evdev_uinput(device.get());
}

void InputDevice::_sendEvent(uint type, uint code, int value) {
    std::lock_guard lock(_input_mutex);
    libevdev_uinput_write_event(ui_device.get(), type, code, value);
    libevdev_uinput_write_event(ui_device.get(), EV_SYN, SYN_REPORT, 0);
}

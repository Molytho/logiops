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

#include <actions/gesture/AxisGesture.h>
#include <actions/gesture/Gesture.h>
#include <actions/gesture/IntervalGesture.h>
#include <actions/gesture/NullGesture.h>
#include <actions/gesture/ReleaseGesture.h>
#include <actions/gesture/ThresholdGesture.h>
#include <ipc_defs.h>
#include <utility>

using namespace logid;
using namespace logid::actions;

Gesture::Gesture(Device *device, [[maybe_unused]] std::shared_ptr<ipcgull::node> node,
    [[maybe_unused]] const std::string &name) : _device(device) { }

namespace {
    template<typename T>
    struct gesture_type {
        typedef typename T::gesture type;
    };

    template<typename T>
    struct gesture_type<const T> : gesture_type<T> { };

    template<typename T>
    struct gesture_type<T &> : gesture_type<T> { };

    template<typename T>
    std::shared_ptr<Gesture> _makeGesture(Device *device, T &gesture) {
        return std::make_shared<typename gesture_type<T>::type>(device, std::forward<T &>(gesture));
    }
} // namespace

std::shared_ptr<Gesture> Gesture::makeGesture(Device *device, config::Gesture &gesture,
    const std::shared_ptr<ipcgull::node> &parent) {
    return std::visit([&device](auto &&x) { return _makeGesture(device, x); }, gesture);
}

std::shared_ptr<Gesture> Gesture::makeGesture(Device *device, const std::string &type,
    config::Gesture &config, const std::shared_ptr<ipcgull::node> &parent) {
    if (type == AxisGesture::interface_name) {
        config = config::AxisGesture();
    } else if (type == IntervalGesture::interface_name) {
        config = config::IntervalGesture();
    } else if (type == ReleaseGesture::interface_name) {
        config = config::ReleaseGesture();
    } else if (type == ThresholdGesture::interface_name) {
        config = config::ThresholdGesture();
    } else if (type == NullGesture::interface_name) {
        config = config::NoGesture();
    } else {
        throw InvalidGesture();
    }

    return makeGesture(device, config);
}

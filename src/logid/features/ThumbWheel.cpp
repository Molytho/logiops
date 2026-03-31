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

#include <features/ThumbWheel.h>
#include <actions/gesture/AxisGesture.h>
#include <Device.h>
#include <util/log.h>
#include <ipc_defs.h>

using namespace logid::features;
using namespace logid::backend;
using namespace logid;

#define FLAG_STR(b) (_wheel_info.capabilities & _thumb_wheel->b ? "YES" : "NO")

namespace {
    std::shared_ptr<actions::Action> _genAction(
            Device* dev, std::optional<config::BasicAction>& conf) {
        if (conf.has_value()) {
            try {
                return actions::Action::makeAction(dev, conf.value());
            } catch (actions::InvalidAction& e) {
                logPrintf(WARN, "Mapping thumb wheel to invalid action");
            }
        }

        return nullptr;
    }

    std::shared_ptr<actions::Gesture> _genGesture(
            Device* dev, std::optional<config::Gesture>& conf) {
        if (conf.has_value()) {
            try {
                auto result = actions::Gesture::makeGesture(dev, conf.value());
                if (!result->wheelCompatibility()) {
                    logPrintf(WARN, "Mapping thumb wheel to incompatible gesture");
                    return nullptr;
                } else {
                    return result;
                }
            } catch (actions::InvalidAction& e) {
                logPrintf(WARN, "Mapping thumb wheel to invalid gesture");
            }
        }

        return nullptr;
    }
}

ThumbWheel::ThumbWheel(Device* dev) : DeviceFeature(dev), _wheel_info(),
                                      _config(dev->activeProfile().thumbwheel) {

    try {
        _thumb_wheel = std::make_shared<hidpp20::ThumbWheel>(&dev->hidpp20());
    } catch (hidpp20::UnsupportedFeature& e) {
        throw UnsupportedFeature();
    }

    _makeConfig();

    _wheel_info = _thumb_wheel->getInfo();

    logPrintf(DEBUG, "Thumb wheel detected (0x2150), capabilities:");
    logPrintf(DEBUG, "timestamp | touch | proximity | single tap");
    logPrintf(DEBUG, "%-9s | %-5s | %-9s | %-10s", FLAG_STR(Timestamp),
              FLAG_STR(Touch), FLAG_STR(Proxy), FLAG_STR(SingleTap));
    logPrintf(DEBUG, "Thumb wheel resolution: native (%d), diverted (%d)",
              _wheel_info.nativeRes, _wheel_info.divertedRes);

    if (_left_gesture) {
        _fixGesture(_left_gesture);
    }

    if (_right_gesture) {
        _fixGesture(_right_gesture);
    }
}

void ThumbWheel::_makeConfig() {
    if (_config.get().has_value()) {
        auto& conf = _config.get().value();
        _left_gesture = _genGesture(_device, conf.left);
        _right_gesture = _genGesture(_device, conf.right);
        _touch_action = _genAction(_device, conf.touch);
        _tap_action = _genAction(_device, conf.tap);
        _proxy_action = _genAction(_device, conf.proxy);
    }
}

void ThumbWheel::configure() {
    std::shared_lock lock(_config_mutex);
    auto& config = _config.get();
    if (config.has_value()) {
        const auto& value = config.value();
        _thumb_wheel->setStatus(value.divert.value_or(false),
                                value.invert.value_or(false));
    }
}

void ThumbWheel::listen() {
    if (_ev_handler.empty()) {
        _ev_handler = _device->hidpp20().addEventHandler(
                {[index = _thumb_wheel->featureIndex()]
                         (const hidpp::Report& report) -> bool {
                    return (report.feature() == index) &&
                           (report.function() == hidpp20::ThumbWheel::Event);
                },
                 [self_weak = self<ThumbWheel>()](const hidpp::Report& report) -> void {
                     if (auto self = self_weak.lock())
                        self->_handleEvent(self->_thumb_wheel->thumbwheelEvent(report));
                 }
                });
    }
}

void ThumbWheel::setProfile(config::Profile& profile) {
    std::unique_lock lock(_config_mutex);
    _config = profile.thumbwheel;
    _left_gesture.reset();
    _right_gesture.reset();
    _touch_action.reset();
    _tap_action.reset();
    _proxy_action.reset();
    _makeConfig();
}

void ThumbWheel::_handleEvent(hidpp20::ThumbWheel::ThumbwheelEvent event) {
    std::shared_lock lock(_config_mutex);
    if (event.flags & hidpp20::ThumbWheel::SingleTap) {
        auto action = _tap_action;
        if (action) {
            action->press();
            action->release();
        }
    }

    if ((bool) (event.flags & hidpp20::ThumbWheel::Proxy) != _last_proxy) {
        _last_proxy = !_last_proxy;
        if (_proxy_action) {
            if (_last_proxy)
                _proxy_action->press();
            else
                _proxy_action->release();
        }
    }

    if ((bool) (event.flags & hidpp20::ThumbWheel::Touch) != _last_touch) {
        _last_touch = !_last_touch;
        if (_touch_action) {
            if (_last_touch)
                _touch_action->press();
            else
                _touch_action->release();
        }
    }

    if (event.rotationStatus != hidpp20::ThumbWheel::Inactive) {
        // Make right positive unless inverted
        event.rotation *= _wheel_info.defaultDirection;

        if (event.rotationStatus == hidpp20::ThumbWheel::Start) {
            if (_right_gesture)
                _right_gesture->press(true);
            if (_left_gesture)
                _left_gesture->press(true);
        }

        if (event.rotation) {
            int8_t direction = event.rotation > 0 ? 1 : -1;
            std::shared_ptr<actions::Gesture> scroll_action;

            if (direction > 0)
                scroll_action = _right_gesture;
            else
                scroll_action = _left_gesture;

            if (scroll_action) {
                scroll_action->press(true);
                scroll_action->move((int16_t) (direction * event.rotation));
            }
        }

        if (event.rotationStatus == hidpp20::ThumbWheel::Stop) {
            if (_right_gesture)
                _right_gesture->release(false);
            if (_left_gesture)
                _left_gesture->release(false);
        }
    }
}

void ThumbWheel::_fixGesture(const std::shared_ptr<actions::Gesture>& gesture) const {
    try {
        auto axis = std::dynamic_pointer_cast<actions::AxisGesture>(gesture);
        // TODO: How do hires multipliers work on 0x2150 thumbwheels?
        if (axis)
            axis->setHiresMultiplier(_wheel_info.divertedRes);
    } catch (std::bad_cast& e) {}

    if (gesture)
        gesture->press(true);
}
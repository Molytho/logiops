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
#include <backend/hidpp10/Error.h>
#include <backend/hidpp20/Error.h>
#include <system_error>
#include <util/ExceptionHandler.h>
#include <util/log.h>

using namespace logid;

void ExceptionHandler::Default(std::exception_ptr e) {
    try {
        if (e) {
            std::rethrow_exception(e);
        }
    } catch (backend::hidpp10::Error &e) {
        logPrintf(WARN, "HID++ 1.0 error ignored on task: %s", e.what());
    } catch (backend::hidpp20::Error &e) {
        logPrintf(WARN, "HID++ 2.0 error ignored on task: %s", e.what());
    } catch (std::system_error &e) {
        logPrintf(WARN, "System error ignored on task: %s", e.what());
    } catch (std::exception &e) {
        logPrintf(WARN, "Error ignored on task: %s", e.what());
    } catch (...) {
        logPrintf(ERROR, "Caught unknown error in tasks");
        std::abort();
    }
}
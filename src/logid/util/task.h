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
#ifndef LOGID_TASK_H
#define LOGID_TASK_H

#include <future>

namespace logid {
    using task_clock = std::chrono::system_clock;

    void init_workers(int worker_count);

    void run_task(std::packaged_task<void()> task,
        std::chrono::time_point<task_clock> at = task_clock::now());

    template<class F>
    void run_task(F &&func) {
        run_task(std::packaged_task<void()>(std::forward<F>(func)));
    }

    template<class F>
    void run_task_after(F &&func, std::chrono::milliseconds delay) {
        run_task(std::packaged_task<void()>(std::forward<F>(func)), task_clock::now() + delay);
    }
} // namespace logid

#endif // LOGID_TASK_H

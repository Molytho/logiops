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
#include <algorithm>
#include <cassert>
#include <vector>

#include <util/ExceptionHandler.h>
#include <util/task.h>

using namespace logid;
using namespace std::chrono;

namespace {
    struct task {
        std::packaged_task<void()> task;
        std::chrono::time_point<task_clock> at;
    };

    class thread_pool {
        std::mutex m_lock {};
        std::condition_variable m_cv {};

        // TODO: Wrap in class
        std::vector<task> m_queue {};

        bool m_running {true};

        std::vector<std::thread> m_threads {};

    public:
        thread_pool(size_t thread_count) {
            m_threads.reserve(thread_count);
            for (size_t i = 0; i < thread_count; i++) {
                m_threads.emplace_back([this]() { this->worker(); });
            }
        }

        ~thread_pool() {
            {
                std::lock_guard guard {m_lock};
                m_running = false;
            }

            m_cv.notify_all();

            for (std::thread &t : m_threads) {
                t.join();
            }
        }

        void push_task(task &&task) {
            push_queue(std::move(task));
            m_cv.notify_one();
        }

    private:
        void worker() noexcept {
            std::packaged_task<void()> task;
            while ((task = pop_task()).valid()) {
                try {
                    task();
                } catch (...) {
                    ExceptionHandler::Default(std::current_exception());
                }
            }
        }

        std::packaged_task<void()> pop_task() {
            std::unique_lock lock {m_lock};
            while (true) {
                while (m_running && !is_task_ready()) {
                    if (m_queue.empty()) {
                        m_cv.wait(lock);
                    } else {
                        m_cv.wait_until(lock, m_queue.front().at);
                    }
                }
                if (!m_running) {
                    return {};
                }
                return pop_queue().task;
            }
        }

        bool is_task_ready() const noexcept {
            return !m_queue.empty() && m_queue.front().at < task_clock::now();
        }

        void push_queue(task &&task) {
            m_queue.push_back(std::move(task));
            std::ranges::push_heap(m_queue, std::greater<> {}, [](const struct task &t) {
                return t.at;
            });
        }

        task pop_queue() noexcept {
            assert(!m_queue.empty());
            std::ranges::pop_heap(m_queue, std::greater<> {}, [](const struct task &t) {
                return t.at;
            });
            auto value = std::move(m_queue.back());
            m_queue.pop_back();
            return value;
        }
    };

    std::unique_ptr<thread_pool> worker_pool;
} // namespace

void logid::init_workers(int worker_count) {
    assert(!worker_pool);
    worker_pool = std::make_unique<thread_pool>(worker_count);
}

void logid::run_task(std::packaged_task<void()> task, std::chrono::time_point<task_clock> at) {
    assert(worker_pool);
    worker_pool->push_task({std::move(task), at});
}
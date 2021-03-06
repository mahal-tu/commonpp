/*
 * File: ThreadPool.hpp
 * Part of commonpp.
 *
 * Distributed under the 2-clause BSD licence (See LICENCE.TXT file at the
 * project root).
 *
 * Copyright (c) 2015 Thomas Sanchez.  All rights reserved.
 *
 */
#pragma once

#include <cstddef>
#include <vector>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <atomic>

#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <tbb/enumerable_thread_specific.h>

#include <commonpp/core/RandomValuePicker.hpp>
#include <commonpp/core/traits/is_duration.hpp>
#include <commonpp/core/traits/function_wrapper.hpp>

#include "Thread.hpp"

namespace commonpp
{
namespace thread
{

class ThreadPool
{
public:
    using ThreadInit = std::function<void()>;

public:
    ThreadPool(size_t nb_thread, std::string name = "", size_t nb_services = 1);
    ThreadPool(size_t nb_thread,
               boost::asio::io_service& service,
               std::string name = "");
    ~ThreadPool();

    ThreadPool(ThreadPool&&);
    ThreadPool& operator=(ThreadPool&&) = delete;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    enum
    {
        ROUND_ROBIN = -1,
        RANDOM_SERVICE = -2,
        CURRENT_SERVICE = -3,
    };

    enum class ThreadDispatchPolicy
    {
        Random,
        DispatchToPCore,
        DispatchToAllCore,
    };

    template <typename Callable>
    void post(Callable&& callable, int service_id = ROUND_ROBIN)
    {
        getService(service_id).post(std::forward<Callable>(callable));
    }

    template <typename Callable>
    void dispatch(Callable&& callable, int service_id = ROUND_ROBIN)
    {
        getService(service_id).dispatch(std::forward<Callable>(callable));
    }

    bool runningInPool() const noexcept;
    boost::asio::io_service& getCurrentIOService();

    void start(ThreadInit fct = ThreadInit(),
               ThreadDispatchPolicy policy = ThreadDispatchPolicy::Random);
    void stop();

    boost::asio::io_service& getService(int service_id = ROUND_ROBIN);

    using TimerPtr = std::shared_ptr<boost::asio::deadline_timer>;
    // if callable returns a boolean, if it returns true the timer will be
    // rescheduled automatically
    template <typename Duration, typename Callable>
    TimerPtr schedule(Duration delay,
                      Callable&& callable,
                      int service_id = ROUND_ROBIN);

    size_t threads() const noexcept;

    template <typename Callable>
    void postAll(Callable callable);

    template <typename Callable>
    void dispatchAll(Callable callable);

private:
    template <typename Duration, typename Callable>
    void schedule_timer(TimerPtr& timer, Duration, Callable&& callable);

    void run(boost::asio::io_service&, ThreadInit fct);

private:
    bool running_ = false;
    const size_t nb_thread_;
    const size_t nb_services_;
    std::string name_;

    std::atomic_uint current_service_{0};
    std::atomic_uint running_threads_{0};

    std::vector<std::thread> threads_;
    std::vector<std::shared_ptr<boost::asio::io_service>> services_;
    std::vector<std::unique_ptr<boost::asio::io_service::work>> works_;

    tbb::enumerable_thread_specific<decltype(createPicker(services_))> picker_;
};

template <typename Duration, typename Callable>
void ThreadPool::schedule_timer(TimerPtr& timer,
                                Duration delay,
                                Callable&& callable)
{
    timer->expires_from_now(boost::posix_time::milliseconds(
        std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()));
    timer->async_wait([this, delay, timer,
                       callable](const boost::system::error_code& error) mutable
                      {
                          if (error)
                          {
                              if (error != boost::asio::error::operation_aborted)
                              {
                                  throw std::runtime_error(error.message());
                              }

                              return;
                          }

                          if (traits::make_bool_functor(callable))
                          {
                              schedule_timer(timer, delay, callable);
                          }
                      });
}

template <typename Duration, typename Callable>
ThreadPool::TimerPtr ThreadPool::schedule(Duration delay,
                                          Callable&& callable,
                                          int service_id)
{
    static_assert(traits::is_duration<Duration>::value,
                  "A std::chrono::duration is expected here");
    auto timer =
        std::make_shared<boost::asio::deadline_timer>(getService(service_id));
    schedule_timer(timer, delay, std::forward<Callable>(callable));
    return timer;
}

template <typename Callable>
void ThreadPool::postAll(Callable callable)
{
    for (size_t i = 0; i < nb_thread_; ++i)
    {
        post(callable);
    }
}

template <typename Callable>
void ThreadPool::dispatchAll(Callable callable)
{
    for (size_t i = 0; i < nb_thread_; ++i)
    {
        post(callable);
    }
}

} // namespace thread
} // namespace commonpp

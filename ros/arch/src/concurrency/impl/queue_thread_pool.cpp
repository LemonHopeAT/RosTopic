/**
 * @file queue_thread_pool.cpp
 * @brief Thread pool implementation
 * @date 2025
 * @version 1.0.0
 */

#include "include/concurrency/impl/queue_thread_pool.h"

#include <cassert>
#include <chrono>
#include <stdexcept>

#include "arch/utils.h"
#include "arch/concurrency/atask.h"

namespace
{
    using std::chrono_literals::operator""ms;

    static constexpr auto wait_timeout = 500ms;    ///< thread waiting time for new task
    static constexpr auto sleep_time   = 1ms;      ///< thread sleep time between accesses to task queue
}    // namespace

namespace arch::impl
{

    QueueThreadPool::QueueThreadPool(size_t thread_num) : arch::IThreadPool()
    {
        assert(thread_num != 0);    // fail, if threads number is zero

        threads_.resize(thread_num);
        for (size_t i = 0; i < threads_.size(); ++i)
            threads_[i] = std::make_shared<std::thread>(&QueueThreadPool::threadRoutine, this);
    }

    QueueThreadPool::~QueueThreadPool()
    {
        is_run_   = false;
        is_pause_ = true;

        // Clear executing tasks
        std::unique_lock<std::mutex> lk(exec_task_mutex_);
        exec_tasks_.clear();
        lk.unlock();

        task_access_.notify_all();    // Wake all threads

        for (auto& thread : threads_)    // wait for stopping all threads
            if (thread->joinable())
                thread->join();
    }

    void QueueThreadPool::start()
    {
        if (is_pause_)
        {
            is_pause_ = false;
            task_access_.notify_all();    // Notify threads to take tasks
        }
    }

    void QueueThreadPool::pause()
    {
        is_pause_ = true;
    }

    void QueueThreadPool::pushTask(TaskPtr task)
    {
        if (!task)
            return;    // Ignore nullptr tasks

        // Wrap ATask::operator() via std::bind
        auto bound_func = std::bind(&arch::ATask::operator(), task.get());
        pushTaskFunction(std::move(bound_func));
    }

    void QueueThreadPool::pushTaskFunction(TaskFunction func)
    {
        if (!func)
            return;    // Ignore empty functions

        std::unique_lock<std::mutex> lk(task_mutex_);
        tasks_.push(std::move(func));
        lk.unlock();

        task_access_.notify_one();    // Notify any awaiting thread that new task is available
    }

    void QueueThreadPool::eraseTask(const TaskFunction& func)
    {
        // Remove from executing tasks if found
        std::unique_lock<std::mutex> lk(exec_task_mutex_);
        for (auto it = exec_tasks_.begin(); it != exec_tasks_.end();)
        {
            // Compare function targets (simplified - in real implementation might need more sophisticated comparison)
            if (it->second.target_type() == func.target_type())
            {
                it = exec_tasks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void QueueThreadPool::eraseTask(TaskPtr task)
    {
        if (!task)
            return;

        task->run_ = false;

        // Also try to remove from executing tasks if wrapped
        auto bound_func = std::bind(&arch::ATask::operator(), task.get());
        eraseTask(bound_func);
    }

    void QueueThreadPool::threadRoutine()
    {
        while (is_run_)
        {
            std::unique_lock<std::mutex> lk(task_mutex_);
            if (task_access_.wait_for(lk, wait_timeout,
                                      [this]() {
                                          return (!tasks_.empty() && !is_pause_) || !is_run_;
                                      }))    // Wait for new task or thread exit flag
            {
                if (!tasks_.empty() && !is_pause_)    // new task is available and distributing isn't stopped
                {
                    auto task_func = std::move(tasks_.front());
                    tasks_.pop();
                    lk.unlock();

                    // Generate task ID from thread ID
                    uint32_t task_id = static_cast<uint32_t>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()));

                    {
                        std::unique_lock<std::mutex> lk_exec(exec_task_mutex_);    // save exec task
                        exec_tasks_[task_id] = task_func;
                    }

                    // Execute task function
                    try
                    {
                        task_func();
                    }
                    catch (...)
                    {
                        // Ignore exceptions from tasks
                    }

                    {
                        std::unique_lock<std::mutex> lk_exec(exec_task_mutex_);    // delete exec task
                        exec_tasks_.erase(task_id);
                    }
                }
            }
            else
                lk.unlock();
            sleep_for(sleep_time);
        }
    }

}    // namespace arch::impl

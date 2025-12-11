/**
 * @file queue_thread_pool.h
 * @brief Thread pool implementation with task queue supporting both ATask and functors
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_ccr_impl
 */

#ifndef ARCH_CCR_IMPL_QUEUE_THREAD_POOL_H
#define ARCH_CCR_IMPL_QUEUE_THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "arch/concurrency/atask.h"
#include "arch/concurrency/ithread_pool.h"

namespace arch::impl
{

    /**
     * @brief Thread pool implementation with task queue
     * @ingroup arch_ccr_impl
     *
     * Manages threads using task queue. The number of threads does not change.
     * Threads try to access the task queue. If queue is not empty,
     * first thread that accesses it takes new task for processing.
     * After task is finished, thread is free and tries to access the queue again.
     *
     * Supports both ATask objects (with operator()) and simple functors (std::function<void()>).
     * Inherits from arch::IThreadPool for compatibility with arch library.
     */
    class QueueThreadPool : public arch::IThreadPool
    {
    public:
        using TaskFunction = std::function<void()>;    ///< Function type for tasks and functors

    private:
        using thread_type = std::shared_ptr<std::thread>;    ///< stored threads type alias

        std::unordered_map<uint32_t, TaskFunction> exec_tasks_;    ///< map for executing tasks (functions)
        std::queue<TaskFunction> tasks_;                           ///< task queue (functions)
        std::vector<thread_type> threads_;                         ///< available threads

        std::mutex exec_task_mutex_;             ///< mutex for exec task map
        std::mutex task_mutex_;                  ///< mutex for task queue
        std::condition_variable task_access_;    ///< conditional variable for access to task queue

        std::atomic<bool> is_run_{true};      ///< thread exit flag
        std::atomic<bool> is_pause_{true};    ///< distributing pause flag

    public:
        /**
         * @brief ThreadPool constructor
         * @param thread_num Number of threads in pool (must be > 0)
         */
        explicit QueueThreadPool(size_t thread_num);

        /**
         * @brief ThreadPool destructor
         */
        ~QueueThreadPool();

        /**
         * @brief Start thread pool (resume task distribution)
         * @copydoc arch::IThreadPool::start()
         */
        void start() override;

        /**
         * @brief Pause thread pool (stop task distribution)
         * @copydoc arch::IThreadPool::pause()
         */
        void pause() override;

        /**
         * @brief Push ATask to queue
         * @param task Task pointer (can be nullptr, will be ignored)
         * @copydoc arch::IThreadPool::pushTask(TaskPtr)
         */
        void pushTask(TaskPtr task) override;

        /**
         * @brief Push callable as task to queue (template version)
         * @tparam Callable Type of callable object (function, lambda, functor, etc.)
         * @param func Callable object to execute (wrapped via std::bind)
         *
         * Automatically wraps any callable object using std::bind into std::function<void()>.
         * Works with lambdas, functions, functors, std::function, etc.
         * Excludes TaskPtr to avoid recursion.
         */
        template <typename Callable>
        typename std::enable_if<
            !std::is_same<std::decay_t<Callable>, TaskPtr>::value &&
                !std::is_same<std::decay_t<Callable>, std::shared_ptr<arch::ATask>>::value,
            void>::type
        pushTask(Callable&& func)    // Additional overload for functors (not from IThreadPool)
        {
            // Use std::bind to create callable wrapper
            auto bound_func = std::bind(std::forward<Callable>(func));
            pushTaskFunction(std::move(bound_func));
        }

        /**
         * @brief Push function as task to queue
         * @param func Function to execute
         */
        void pushTaskFunction(TaskFunction func);

        /**
         * @brief Erase task from execution by function
         * @param func Task function to erase (will be compared and removed if found)
         */
        void eraseTask(const TaskFunction& func);

        /**
         * @brief Erase ATask from execution
         * @param task Task to erase
         * @copydoc arch::IThreadPool::eraseTask(TaskPtr)
         */
        void eraseTask(TaskPtr task) override;

        /**
         * @brief Get number of threads
         * @return Number of threads
         */
        size_t getThreadCount() const { return threads_.size(); }

        /**
         * @brief Check if pool is running
         * @return true if running
         */
        bool isRunning() const { return is_run_.load() && !is_pause_.load(); }

    private:
        /**
         * @brief Processes thread routine
         *
         * Thread tries to access to task queue. If success, task is executed in thread.
         * Otherwise thread sleeps and tries to get task again
         */
        void threadRoutine();
    };

}    // namespace arch::impl

#endif    // !ARCH_CCR_IMPL_QUEUE_THREAD_POOL_H

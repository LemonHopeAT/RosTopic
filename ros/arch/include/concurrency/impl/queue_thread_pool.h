/**
 * @file queue_thread_pool.h
 * @brief Thread pool implementation with task queue supporting both ATask and functors
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_ccr_impl
 */

#ifndef ARCH_CCR_IMPL_QUEUE_THREAD_POOL_H
#define ARCH_CCR_IMPL_QUEUE_THREAD_POOL_H

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>
#include <unordered_set>

#include "arch/concurrency/atask.h"
#include "arch/concurrency/ithread_pool.h"
#include "concurrency/impl/lock_free_MPMC_queue.h"
#include "communication/impl/wait_set.h"

namespace arch::impl
{
    // Forward declaration
    class QueueThreadPool;

    /**
     * @brief Wrapper for task function with cancellation support
     * @note Used internally by QueueThreadPool for task cancellation
     */
    class CancellableTaskWrapper
    {
    public:
        using TaskFunction = std::function<void()>;
        
        explicit CancellableTaskWrapper(TaskFunction func)
            : func_(std::move(func))
            , cancelled_(false)
        {
        }

        void operator()()
        {
            // Check cancellation flag before execution
            if (!cancelled_.load(std::memory_order_acquire))
            {
                func_();
            }
        }

        void cancel() noexcept
        {
            cancelled_.store(true, std::memory_order_release);
        }

        bool is_cancelled() const noexcept
        {
            return cancelled_.load(std::memory_order_acquire);
        }

        // Compare function targets for matching
        bool matches(const TaskFunction& other) const
        {
            return func_.target_type() == other.target_type();
        }

    private:
        TaskFunction func_;
        std::atomic<bool> cancelled_;
    };


    /**
     * @brief Lock-free thread pool implementation with task queue
     * @ingroup arch_ccr_impl
     *
     * Manages threads using lock-free task queue. The number of threads does not change.
     * Threads try to access the task queue. If queue is not empty,
     * first thread that accesses it takes new task for processing.
     * After task is finished, thread is free and tries to access the queue again.
     *
     * Supports both ATask objects (with operator()) and simple functors (std::function<void()>).
     * Inherits from arch::IThreadPool for compatibility with arch library.
     * 
     * Uses LockFreeMPMCQueue for task queue and WaitSet for thread synchronization.
     * All operations are lock-free and do not use mutexes.
     */
    class QueueThreadPool : public arch::IThreadPool
    {
    public:
        using TaskFunction = std::function<void()>;    ///< Function type for tasks and functors

    private:
        using thread_type = std::shared_ptr<std::thread>;    ///< stored threads type alias
        using CancellableTaskPtr = std::shared_ptr<CancellableTaskWrapper>;    ///< Pointer to cancellable task wrapper

        // Lock-free task queue - supports multiple producers and consumers
        // Store CancellableTaskWrapper for cancellation support
        arch::experimental::LockFreeMPMCQueue<TaskFunction> tasks_;
        
        std::vector<thread_type> threads_;                         ///< available threads

        arch::experimental::WaitSet wait_set_;    ///< Lock-free wait set for thread synchronization

        // Lock-free storage for active tasks (for cancellation)
        // Use weak_ptr to avoid keeping tasks alive unnecessarily
        struct TaskNode
        {
            std::weak_ptr<CancellableTaskWrapper> task;
            std::atomic<TaskNode*> next{nullptr};
            
            TaskNode(std::weak_ptr<CancellableTaskWrapper> t) : task(std::move(t)) {}
        };
        
        std::atomic<TaskNode*> active_tasks_head_{nullptr};    ///< Lock-free linked list head

        std::atomic<bool> is_run_{true};      ///< thread exit flag
        std::atomic<bool> is_pause_{true};    ///< distributing pause flag
        
        static constexpr size_t DEFAULT_QUEUE_CAPACITY = 1024;    ///< Default queue capacity
        
        /**
         * @brief Register task wrapper for cancellation tracking
         * @param task_ptr Shared pointer to cancellable task wrapper
         */
        void register_task_for_cancellation(std::shared_ptr<CancellableTaskWrapper> task_ptr);
        
        /**
         * @brief Cleanup expired task nodes from cancellation tracking
         */
        void cleanup_expired_tasks();

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
         * @brief Push function as task to queue (lock-free)
         * @param func Function to execute
         */
        void pushTaskFunction(TaskFunction func);

        /**
         * @brief Erase task from execution by function
         * @note Cancels matching tasks by setting cancellation flag
         * @param func Task function to erase (will be compared by target_type and cancelled if found)
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

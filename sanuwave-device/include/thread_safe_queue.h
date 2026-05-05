#ifndef THREAD_SAFE_QUEUE_
#define THREAD_SAFE_QUEUE_

#include <queue>
#include <mutex>
#include <condition_variable>

namespace sanuwave
{
    // Thread-safe frame queue
    template<typename T>
    class ThreadSafeQueue
    {
    public:

        void push(const T& data)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(data);
            cv_.notify_one();
        }

        T popWait(int timeout_ms = 1000)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this] { return !queue_.empty(); }))
            {
                T data = queue_.front();
                queue_.pop();
                return data;
            }
            
            return T{};
        }

        T popLatest()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (queue_.empty())
            {
                return T{};
            }
            
            // Get latest frame and discard older ones
            T data;
            while (!queue_.empty())
            {
                data = queue_.front();
                queue_.pop();
            }
            
            return data;
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty())
            {
                queue_.pop();
            }
        }

    private:
        std::queue<T> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
    };
}
#endif
  
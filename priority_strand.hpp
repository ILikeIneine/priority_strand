//
// Created by Hank_ on 2023/10/9.
//
#pragma once

#include "boost/asio/detail/work_dispatcher.hpp"
#include "boost/asio/executor.hpp"
#include "boost/asio/executor_work_guard.hpp"
#include <thread>

namespace boost::asio {

namespace detail{

class spin_lock{
public:
    void lock() noexcept
    {
        sleepy_head sleepy;
        while(locked_.test_and_set(std::memory_order_acquire))
        {
            sleepy.wait();
        }
    }

    void unlock() noexcept
    {
        locked_.clear(std::memory_order_release);
    }
private:
    class sleepy_head{
    public:
        constexpr explicit sleepy_head(): spin_count_(0)
        {}

        void wait() noexcept
        {
            if (spin_count_ < MAX_SPIN_ROUND)
            {
                ++spin_count_;
                pause();
            }
            else
            {
                sleep();
            }
        }

    private:
        static void sleep() noexcept
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(500'000));
        }

#if defined(__i386__) || defined(x86_64) || defined(__arm64__)
        static inline void pause() noexcept
        {
            asm volatile("pause");
        }
#else
#error "please implement system provided `pause`"
#endif
        BOOST_CONSTEXPR static uint32_t MAX_SPIN_ROUND = 4000;
        uint32_t spin_count_;
    };

    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
};

class priority_strand_executor_service
        : public execution_context_service_base<priority_strand_executor_service>
{
public:
    explicit priority_strand_executor_service(execution_context& ctx)
        : execution_context_service_base<priority_strand_executor_service>(ctx)
    {

    }

    ~priority_strand_executor_service()
    {
        assert(!impl_list_.empty());
    }

    class priority_strand_impl
    {
    public:
        explicit priority_strand_impl(priority_strand_executor_service* service) noexcept
            : service_(service)
        {

        }

        ~priority_strand_impl() noexcept
        {
            assert(service_);
            service_->impl_list_.erase(
                    std::remove( service_->impl_list_.begin(),
                                 service_->impl_list_.end(), this),
                                 service_->impl_list_.end());
        }

    private:
        friend priority_strand_executor_service;

        // Mutex for internal resource access
        spin_lock mutex_;

        //
        bool locked_{false};

        //
        bool shutdown_{false};

        //
        int64_t total_in_{0};
        int64_t total_out_{0};
        int64_t priority_total_in_{0};
        int64_t priority_total_out_{0};

        detail::op_queue<detail::scheduler_operation> queue;
        detail::op_queue<detail::scheduler_operation> priority_queue;

        priority_strand_executor_service* service_;

    };

    using implementation_type = std::shared_ptr<priority_strand_impl>;

    class invoker
    {

    };

    implementation_type create_implementation()
    {
        auto new_impl = std::make_shared<priority_strand_impl>(this);
        std::scoped_lock lock(mutex_);
        impl_list_.push_back(new_impl.get());
    }

private:
    // owned by service
    std::mutex mutex_;

    // Asio implementation:
    //    strand_impl* impl_list_; // head of impl list
    // Here we're just using std::vector
    std::vector<priority_strand_impl*> impl_list_;
};
} // namespace detail
} // namespace boost::asio
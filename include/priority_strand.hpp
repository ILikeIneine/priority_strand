//
// Created by Hank_ on 2023/10/9.
//
#pragma once

#include "boost/asio/detail/work_dispatcher.hpp"
#include "boost/asio/executor.hpp"
#include "boost/asio/executor_work_guard.hpp"
#include "boost/asio/dispatch.hpp"
#include "boost/asio/post.hpp"
#include "boost/asio/defer.hpp"
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

#if defined(__i386__) || defined(__x86_64__) || defined(__arm64__)
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
        assert(not impl_list_.empty());
        for(auto impl : impl_list_)
        {
            impl->service_ = nullptr;
        }
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
            std::scoped_lock lock(service_->mutex_);
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
        uint64_t total_in_{0};
        uint64_t total_out_{0};
        uint64_t priority_total_in_{0};
        uint64_t priority_total_out_{0};

        detail::op_queue<detail::scheduler_operation> queue_;
        detail::op_queue<detail::scheduler_operation> priority_queue_;

        priority_strand_executor_service* service_;

    };

    using implementation_type = std::shared_ptr<priority_strand_impl>;

    void shutdown() noexcept override
    {
        // Destroy the ops while leaving this field
        detail::op_queue<scheduler_operation> ops;

        std::scoped_lock lock(mutex_);

        for(auto&& impl : impl_list_)
        {
            impl->mutex_.lock();
            impl->shutdown_ = true;
            ops.push(impl->queue_);
            ops.push(impl->priority_queue_);
        }
    }

    auto create_implementation() -> implementation_type
    {
        auto new_impl = std::make_shared<priority_strand_impl>(this);
        std::scoped_lock lock(mutex_);
        impl_list_.push_back(new_impl.get());
        return new_impl;
    }

    static bool running_in_this_thread(const implementation_type& impl)
    {
        return !!call_stack<priority_strand_impl>::contains(impl.get());
    }

    static uint64_t normal_in(const implementation_type& impl) noexcept
    {
        impl->mutex_.lock();
        auto ret = impl->total_in_;
        impl->mutex_.unlock();
        return ret;
    }

    static uint64_t normal_out(const implementation_type& impl) noexcept
    {
        impl->mutex_.lock();
        auto ret = impl->total_out_;
        impl->mutex_.unlock();
        return ret;
    }

    static uint64_t priority_in(const implementation_type& impl) noexcept
    {
        impl->mutex_.lock();
        auto ret = impl->priority_total_in_;
        impl->mutex_.unlock();
        return ret;
    }

    static uint64_t priority_out(const implementation_type& impl) noexcept
    {
        impl->mutex_.lock();
        auto ret = impl->priority_total_out_;
        impl->mutex_.unlock();
        return ret;
    }



    template <typename Executor, typename Function, typename Allocator>
    static void dispatch(const implementation_type& impl, Executor& ex, Function&& function,
                         const Allocator& a, bool prioritized)
    {
        using function_type = std::decay_t<Function>;

        // If we are already running this strand in this thread
        // the function could run `immediately`.
        if (running_in_this_thread(impl))
        {
            // Make a local copy of the function
            function_type tmp(std::forward<Function>(function));

            fenced_block b(fenced_block::full);
            boost_asio_handler_invoke_helpers::invoke(tmp, tmp);
            return;
        }

        // Allocate and construct an operation to wrap the function
        using op = executor_op<function_type, Allocator>;
        // Allocator's ptr
        typename op::ptr p = {detail::addressof(a), op::ptr::allocate(a), 0};
        // construct executor_op
        p.p = new (p.v) op(std::forward<Function>(function), a);

        BOOST_ASIO_HANDLER_CREATION(
                (impl->service_->context(), *p.p, "priority_strand_executor", impl.get(), 0, "dispatch"));

        bool first = enqueue(impl, p.p, prioritized);
        p.v = p.p = 0;

        if (first)
        {
            boost::asio::dispatch(
                    ex, allocator_binder<invoker<Executor>, Allocator>
                        (invoker<Executor>(impl, ex), a));
        }

    }

    template <typename Executor, typename Function, typename Allocator>
    static void post(const implementation_type& impl, Executor& ex, Function&& function,
                     Allocator a, bool prioritized)
    {
        using function_type = std::decay_t<Function>;

        // Allocate and construct an operation to wrap the function
        using op = executor_op<function_type, Allocator>;
        // Allocator's ptr
        typename op::ptr p = {detail::addressof(a), op::ptr::allocate(a), 0};
        // construct executor_op
        p.p = new (p.v) op(std::forward<Function>(function), a);

        BOOST_ASIO_HANDLER_CREATION(
                (impl->service_->context(), *p.p, "priority_strand_executor", impl.get(), 0, "post"));

        bool first = enqueue(impl, p.p, prioritized);
        p.v = p.p = 0;

        if (first)
        {
            // Simplify so not using allocator binder
            boost::asio::post(
                    ex, allocator_binder<invoker<Executor>, Allocator>
                                                            (invoker<Executor>(impl, ex), a));
        }
    }

    template <typename Executor, typename Function, typename Allocator>
    static void defer(const implementation_type& impl, Executor& ex, Function&& function,
                      Allocator a, bool prioritized)
    {

        using function_type = std::decay_t<Function>;

        // Allocate and construct an operation to wrap the function
        using op = executor_op<function_type, Allocator>;
        // Allocator's ptr
        typename op::ptr p = {detail::addressof(a), op::ptr::allocate(a), 0};
        // construct executor_op
        p.p = new (p.v) op(std::forward<Function>(function), a);

        BOOST_ASIO_HANDLER_CREATION(
                (impl->service_->context(), *p.p, "priority_strand_executor", impl.get(), 0, "defer"));


        bool first = enqueue(impl, p.p, prioritized);
        p.v = p.p = 0;

        if (first)
        {
            // Simplify so not using allocator binder
            boost::asio::defer(
                    ex, allocator_binder<invoker<Executor>, Allocator>
                                                            (invoker<Executor>(impl, ex), a));
        }
    }
private:
    friend class priority_strand_impl;

    template <typename F, typename Allocator>
    class allocator_binder
    {
    public:
        using allocator_type = Allocator;

        allocator_binder(F&& f, const Allocator& a)
            : f_(f), allocator_(a)
        {
        }

        allocator_binder(const allocator_binder& other)
            : f_(other.f_), allocator_(other.allocator_)
        {
        }

        allocator_binder(allocator_binder&& other)
            : f_(std::move(other.f_)), allocator_(std::move(other.allocator_))
        {
        }

        auto get_allocator() const noexcept
        {
            return allocator_;
        }

        void operator()()
        {
            f_();
        }

    private:
        F f_;
        allocator_type allocator_;
    };

    template <typename Executor, typename = void> class invoker;

    template <typename Executor>
    class invoker<Executor, typename std::enable_if_t<execution::is_executor<Executor>::value>>
    {
    public:
        invoker(const implementation_type& impl, Executor& ex)
            : impl_(impl), work_(ex)
        {
        }

        invoker(const invoker& oth)
            : impl_(oth.impl_), work_(oth.work_)
        {
        }

        invoker(invoker&& oth)
                : impl_(std::move(oth.impl_)), work_(std::move(oth.work_))
        {
        }

        struct on_invoker_exit
        {
            invoker* this_;
            ~on_invoker_exit()
            {
                this_->impl_->mutex_.lock();
                const auto more_handlers = this_->impl_->locked_ =
                        !this_->impl_->queue_.empty() ||
                        !this_->impl_->priority_queue_.empty();
                this_->impl_->mutex_.unlock();

                if (more_handlers)
                {
                    Executor ex(this_->work_.get_executor());
                    detail::recycling_allocator<void> allocator;
                    ex.post(std::move(*this_), allocator);
                }
            }

        };

        void operator()()
        {
            detail::call_stack<priority_strand_impl>::context ctx(impl_.get());

            on_invoker_exit on_exit = {this};
            (void) on_exit;

            // run ready handlers
            constexpr size_t max_work_count = 30;

            boost::system::error_code ec;
            size_t work_count{0};
            bool empty{false};
            while(work_count < max_work_count && !empty)
            {
                // We start from the priority_queue since
                // it is a PRIORITY queue
                if(auto lock = std::unique_lock(impl_->mutex_);
                const auto o = impl_->priority_queue_.front())
                {
                    impl_->priority_queue_.pop();
                    ++impl_->priority_total_out_;
                    lock.unlock();
                    o->complete(impl_.get(), ec, 0);
                    ++work_count;
                    continue;
                }

                // Then normal queue
                if(auto lock = std::unique_lock(impl_->mutex_);
                const auto o = impl_->queue_.front())
                {
                    impl_->queue_.pop();
                    ++impl_->total_out_;
                    lock.unlock();
                    o->complete(impl_.get(), ec, 0);
                    ++work_count;
                } else {
                    empty = true;
                }
            }
        }

    private:
        implementation_type impl_;
        executor_work_guard<Executor> work_;
    };

    static bool enqueue(const implementation_type& impl, detail::scheduler_operation* op,
                        bool prioritized)
    {
        impl->mutex_.lock();
        if (impl->shutdown_)
        {
            impl->mutex_.unlock();
            op->destroy();
            return false;
        }
        else if (impl->locked_)
        {
            if(prioritized)
            {
                impl->priority_queue_.push(op);
                ++impl->priority_total_in_;
            }
            else
            {
                impl->queue_.push(op);
                ++impl->total_in_;
            }
            impl->mutex_.unlock();
            return false;
        }
        else
        {
            // Nobody else here, take it!
            impl->locked_ = true;
            if(prioritized)
            {
                impl->priority_queue_.push(op);
                ++impl->priority_total_in_;
            }
            else
            {
                impl->queue_.push(op);
                ++impl->total_in_;
            }
            impl->mutex_.unlock();
            return true;
        }
    }

    // owned by service
    std::mutex mutex_;

    // Asio implementation:
    //    strand_impl* impl_list_; // head of impl list
    // Here we're just using std::vector
    std::vector<priority_strand_impl*> impl_list_;
};
} // namespace detail

template <typename Executor>
class priority_strand
{
public:
    using inner_executor_type = Executor;
    priority_strand()
        : impl_(use_service<detail::priority_strand_executor_service>(executor_.context())
                .create_implementation())
    {
    }

    explicit priority_strand(const Executor& ex)
        : executor_(ex),
          impl_(use_service<detail::priority_strand_executor_service>(executor_.context())
                .create_implementation())
    {
    }

    execution_context& context() const noexcept
    {
        return executor_.context();
    }

    void on_work_started() const noexcept
    {
        executor_.on_work_started();
    }

    void on_work_finished() const noexcept
    {
        executor_.on_work_finished();
    }

    template <typename Function, typename Allocator>
    void dispatch(Function&& function, const Allocator& allocator) const noexcept
    {
        detail::priority_strand_executor_service::dispatch(impl_, executor_,
                                                           std::forward<Function>(function), allocator,
                                                           prioritized_);
    }

    template <typename Function, typename Allocator>
    void post(Function&& function, const Allocator& allocator) const noexcept
    {
        detail::priority_strand_executor_service::post(impl_, executor_,
                                                           std::forward<Function>(function), allocator,
                                                           prioritized_);
    }

    template <typename Function, typename Allocator>
    void defer(Function&& function, const Allocator& allocator) const noexcept
    {
        detail::priority_strand_executor_service::defer(impl_, executor_,
                                                       std::forward<Function>(function), allocator,
                                                       prioritized_);
    }

    auto get_inner_executor() const noexcept ->inner_executor_type
    {
        return executor_;
    }

    bool running_in_this_thread() const noexcept
    {
        return detail::priority_strand_executor_service::running_in_this_thread(impl_);
    }

    auto normal_in() const noexcept -> uint64_t
    {
        return detail::priority_strand_executor_service::normal_in(impl_);
    }

    auto priority_in() const noexcept -> uint64_t
    {
        return detail::priority_strand_executor_service::priority_in(impl_);
    }

    auto normal_out() const noexcept -> uint64_t
    {
        return detail::priority_strand_executor_service::normal_out(impl_);
    }

    auto priority_out() const noexcept -> uint64_t
    {
        return detail::priority_strand_executor_service::priority_out(impl_);
    }

    auto high_priority() const noexcept -> priority_strand
    {
        auto ret = *this;
        ret.prioritized_ = true;
        return ret;
    }

    auto normal_priority() const noexcept -> priority_strand
    {
        auto ret = *this;
        ret.prioritized_ = false;
        return ret;
    }

private:
    Executor executor_;
    using implementation_type =
            detail::priority_strand_executor_service::implementation_type;
    implementation_type impl_;
    bool prioritized_{false};

};
} // namespace boost::asio
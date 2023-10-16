#include <iostream>
#include <thread>

#include "boost/asio/io_context.hpp"
#include "boost/asio/strand.hpp"
#include "boost/asio/steady_timer.hpp"

#include "include/priority_strand.hpp"
#include "benchmark/benchmark.h"


void bm_asio_strand(benchmark::State& state)
{
    boost::asio::io_context io_context;
    boost::asio::strand<boost::asio::io_context::executor_type>
            s(io_context.get_executor());

    boost::asio::steady_timer timer(io_context, std::chrono::seconds(100));
    timer.async_wait([](auto&& ...){});

    std::thread t1([&io_context]{io_context.run();});
    std::thread t2([&io_context]{io_context.run();});

    for(auto _ : state)
    {
        std::atomic<size_t> async_count{};
        std::atomic<size_t> strand_count{};

        for (size_t i = 0; i < 10; ++i) {
            boost::asio::post(io_context, [&async_count] { ++async_count; });
            boost::asio::post(s, [&strand_count] { ++strand_count; });
            boost::asio::post(s, [&strand_count] { ++strand_count; });
        }

        while(async_count.load() != 10 || strand_count.load() != 20);
    }
    timer.cancel();
    t1.join();
    t2.join();
}

void bm_priority_strand(benchmark::State& state)
{
    boost::asio::io_context io_context;
    boost::asio::priority_strand<boost::asio::io_context::executor_type>
            priorityStrand(io_context.get_executor());

    boost::asio::steady_timer timer(io_context, std::chrono::seconds(100));
    timer.async_wait([](auto&&...){});

    std::thread t1([&io_context]{io_context.run();});
    std::thread t2([&io_context]{io_context.run();});

    for(auto _ : state)
    {
        std::atomic<size_t> async_count{};
        std::atomic<size_t> strand_count{};

        for(size_t i = 0; i < 10; i++)
        {
            boost::asio::post(io_context, [&async_count]{++async_count;});
            boost::asio::post(priorityStrand, [&strand_count]{++strand_count;});
            boost::asio::post(priorityStrand.high_priority(), [&strand_count]{++strand_count;});
        }

        while(async_count.load()!=10 || async_count.load()!=20);

        timer.cancel();
        t1.join();
        t2.join();
    }
}

BENCHMARK(bm_asio_strand);
BENCHMARK(bm_priority_strand);
BENCHMARK_MAIN();

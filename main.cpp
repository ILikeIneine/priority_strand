#include <iostream>
#include "call_stack.hpp"
#include <thread>
using namespace std::chrono_literals;

struct Foo
{
    template<typename Callable>
    void run(Callable f)
    {
        rt::call_stack<Foo>::Context ctx(this);
        f();
        std::this_thread::sleep_for(100s);
    }
};

Foo Globalfoo{};

void distinguish()
{
    std::cout << (rt::call_stack<Foo>::contains(&Globalfoo) ? "true" : "false") << std::endl;
}

int main()
{

    auto th1 = std::thread([&]
                           {
                                // run -> distinguish
                               Globalfoo.run(&distinguish);
                           });
    std::this_thread::sleep_for(2s);
    auto th2 = std::thread([&]
                           {
                                // distinguish
                               distinguish();
                               std::this_thread::sleep_for(5s);
                           });
    th1.join();
    th2.join();
    return 0;
}

//
// Created by 黄灰红 on 2023/10/7.
//
#pragma once

namespace rt
{

template<typename Key, typename Value = unsigned char>
class call_stack
{
public:
    class Iterator;

    class Context
    {
    public:
        constexpr Context(const Context&) = delete;
        constexpr Context& operator=(const Context&) = delete;
        constexpr explicit Context(Key* key) : key_(key), value_(reinterpret_cast<unsigned char*>(this)),
                                               next_(call_stack<Key, Value>::ms_top)
        {
            call_stack<Key, Value>::ms_top = this;
        }
        constexpr Context(Key* key, Value& val) : key_(key), value_(&val), next_(call_stack<Key, Value>::ms_top)
        {
            call_stack<Key, Value>::ms_top = this;
        }
        constexpr ~Context() { call_stack<Key, Value>::ms_top = next_; }
        constexpr Key* get_key() { return key_; }
        constexpr Value* get_value() { return value_; }

    private:
        friend class call_stack<Key, Value>;

        friend class call_stack<Key, Value>::Iterator;

        Key* key_;
        Value* value_;
        Context* next_;
    };

    class Iterator
    {
    public:
        constexpr explicit Iterator(Context* ctx): ctx_(ctx){}

        Iterator& operator++()
        {
            if(ctx_) {
                ctx_ = ctx_->next_;
            }
            return *this;
        }
        bool operator!=(const Iterator& oth){
            return oth.ctx_ != this->ctx_;
        }

        Context& operator*()
        {
            return *ctx_;
        }

    private:
        Context* ctx_;
    };

    static Value* contains(Key* key)
    {
        auto* elem = ms_top;
        while(elem){
           if(elem->key_ == key)
               return elem->value_;
           elem = elem->next_;
        }
        return nullptr;
    }

    static Iterator begin() { return {ms_top}; }

    static Iterator end() { return {nullptr}; }

private:
    inline static thread_local Context* ms_top = nullptr;
};

}

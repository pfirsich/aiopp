#pragma once

#include <system_error>
#include <variant>

namespace aiopp {
template <typename E>
struct ErrorWrapper {
    E value;
};

template <typename E>
ErrorWrapper<E> error(E&& e)
{
    return ErrorWrapper<E> { std::forward<E>(e) };
}

inline ErrorWrapper<std::error_code> errnoError()
{
    return error(std::make_error_code(static_cast<std::errc>(errno)));
}

template <typename T, typename E = std::error_code>
class Result {
public:
    Result()
        : value_(T {})
    {
    }

    Result(const T& t)
        : value_(t)
    {
    }

    template <typename U>
    Result(ErrorWrapper<U>&& e)
        : value_(E { e.value })
    {
    }

    bool hasValue() const { return value_.index() == 0; }
    explicit operator bool() const { return hasValue(); }

    const T& value() const { return std::get<0>(value_); }
    T& value() { return std::get<0>(value_); }

    const T& operator*() const { return value(); }
    T& operator*() { return value(); }

    const T* operator->() const { return &value(); }

    const E& error() const { return std::get<1>(value_); }

private:
    std::variant<T, E> value_;
};
}

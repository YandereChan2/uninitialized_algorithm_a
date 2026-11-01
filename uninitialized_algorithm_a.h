#pragma once
// 带分配器参数的未初始化内存算法。
// 异常安全统一由 details::scope_guard 保证，不再使用 try - catch(...) - throw；
// 所有循环的边界条件一律使用不等号，计数循环写成 n != 0 的倒计数形式；
// 计数版本（*_n）沿用标准的前置条件：计数必须非负，负数属于未定义行为；
// 分配器是否定制了 construct/destroy 由 allocator_has_construct_v /
// allocator_has_destroy_v 判断，未定制时直接就地构造/析构，于是按迭代器构造的
// 那几个算法（uninitialized_copy/move）对纯右值来源可以启用保证的复制消除。
#include <memory>
#include <type_traits>
#include <utility>

namespace Yc
{
    namespace details
    {
        // ------------------------------------------------------------------
        // scope guard：析构时执行清理动作，release() 之后不再执行。
        // 用它取代 try - catch(...) - throw 的写法：算法正常结束前 release()，
        // 一旦构造过程抛出异常，栈展开时就会销毁已经构造好的那段区间。
        // ------------------------------------------------------------------
        template<class F>
        class scope_guard
        {
        public:
            explicit scope_guard(F f)noexcept(std::is_nothrow_move_constructible_v<F>)
                : cleanup_(std::move(f))
            {
            }

            scope_guard(const scope_guard&) = delete;
            scope_guard& operator=(const scope_guard&) = delete;
            scope_guard(scope_guard&&) = delete;
            scope_guard& operator=(scope_guard&&) = delete;

            // 清理动作必须不抛异常，因此析构函数显式 noexcept
            ~scope_guard()noexcept
            {
                if (armed_)
                {
                    cleanup_();
                }
            }

            // 撤销清理动作，通常在算法正常返回前调用
            void release()noexcept
            {
                armed_ = false;
            }

        private:
            F cleanup_;
            bool armed_ = true;
        };

        // ------------------------------------------------------------------
        // 分配器自身是否定制了 construct / destroy。
        // 判定规则与 allocator_traits 一致：只有当 a.construct(p, args...) 良构时，
        // allocator_traits 才会转调分配器的成员；否则退回内建的 placement new
        // 和析构调用。也就是说这里为 false 时，绕过 allocator_traits 直接就地
        // 构造/析构与经 allocator_traits 的效果完全相同。
        // ------------------------------------------------------------------
        template<class Alloc, class T, class... Args>
        concept has_allocator_construct = requires(std::remove_cvref_t<Alloc>& a, T* p, Args&&... args)
        {
            a.construct(p, std::forward<Args>(args)...);
        };

        template<class Alloc, class T>
        concept has_allocator_destroy = requires(std::remove_cvref_t<Alloc>& a, T* p)
        {
            a.destroy(p);
        };
    }

    // 类型元函数：分配器本身是否定制了 construct
    // 未定制时构造等价于就地 placement new，算法因此可以直接就地构造，
    // 对纯右值来源（例如 transform_view 的迭代器）还能启用保证的复制消除。
    template<class Alloc, class T, class... Args>
    struct allocator_has_construct : std::bool_constant<details::has_allocator_construct<Alloc, T, Args...>>
    {
    };

    // std::allocator 的 construct 就是内建行为，不算定制
    template<class T, class U, class... Args>
    struct allocator_has_construct<std::allocator<T>, U, Args...> : std::false_type
    {
    };

    template<class Alloc, class T, class... Args>
    inline constexpr bool allocator_has_construct_v = allocator_has_construct<Alloc, T, Args...>::value;

    // 类型元函数：分配器本身是否定制了 destroy
    template<class Alloc, class T>
    struct allocator_has_destroy : std::bool_constant<details::has_allocator_destroy<Alloc, T>>
    {
    };

    template<class T, class U>
    struct allocator_has_destroy<std::allocator<T>, U> : std::false_type
    {
    };

    template<class Alloc, class T>
    inline constexpr bool allocator_has_destroy_v = allocator_has_destroy<Alloc, T>::value;

    namespace details
    {
        // 分配器未定制 destroy 时直接析构，省去 allocator_traits 的分派
        template<class Alloc, class T>
        void destroy_one(Alloc& a, T* p)
        {
            if constexpr (allocator_has_destroy_v<Alloc, T>)
            {
                std::allocator_traits<Alloc>::destroy(a, p);
            }
            else
            {
                std::destroy_at(p);
            }
        }

        // 分配器未定制 construct 时直接就地构造，参数原样转发
        template<class Alloc, class T, class... Args>
        void construct_with(Alloc& a, T* p, Args&&... args)
        {
            if constexpr (allocator_has_construct_v<Alloc, T, Args...>)
            {
                std::allocator_traits<Alloc>::construct(a, p, std::forward<Args>(args)...);
            }
            else
            {
                std::construct_at(p, std::forward<Args>(args)...);
            }
        }

        // 由 *it 就地构造。这里传入迭代器而不是解引用结果：只有当 *it 直接出现在
        // new 表达式的初始化器里，纯右值来源才会发生保证的复制消除；如果先绑到
        // 转发形参上，就会退化成一次移动构造。
        template<class Alloc, class T, class It>
        void construct_from(Alloc& a, T* p, It& it)
        {
            if constexpr (allocator_has_construct_v<Alloc, T, decltype(*it)>)
            {
                std::allocator_traits<Alloc>::construct(a, p, *it);
            }
            else
            {
                ::new (static_cast<void*>(p)) T(*it);
            }
        }

        // 同上，但把 *it 当作右值搬移
        template<class Alloc, class T, class It>
        void construct_from_moved(Alloc& a, T* p, It& it)
        {
            if constexpr (allocator_has_construct_v<Alloc, T, decltype(std::move(*it))>)
            {
                std::allocator_traits<Alloc>::construct(a, p, std::move(*it));
            }
            else
            {
                ::new (static_cast<void*>(p)) T(std::move(*it));
            }
        }
    }

    template<class ForwardIt, class Alloc>
    void destroy_a(ForwardIt first, ForwardIt last, Alloc& a)
    {
        for (; first != last; ++first)
        {
            details::destroy_one(a, std::addressof(*first));
        }
    }

    template<class ForwardIt, class Size, class Alloc>
    ForwardIt destroy_n_a(ForwardIt first, Size n, Alloc& a)
    {
        for (; n != 0; (void) ++first, --n)
        {
            details::destroy_one(a, std::addressof(*first));
        }
        return first;
    }

    template<class InputIt, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_copy_a(InputIt first, InputIt last, NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        // 构造中途抛出异常时销毁 [d_first, current)，随后让异常继续传播
        auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
        for (; first != last; ++first, (void) ++current)
        {
            details::construct_from(a, std::addressof(*current), first);
        }
        guard.release();
        return current;
    }

    template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_copy_n_a(InputIt first, Size count,
        NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
        for (; count != 0; ++first, (void) ++current, --count)
        {
            details::construct_from(a, std::addressof(*current), first);
        }
        guard.release();
        return current;
    }

    template<class NoThrowForwardIt, class T, class Alloc>
    void uninitialized_fill_a(NoThrowForwardIt first, NoThrowForwardIt last, const T& value, Alloc& a)
    {
        NoThrowForwardIt current = first;
        auto guard = details::scope_guard{ [&]() { destroy_a(first, current, a); } };
        for (; current != last; ++current)
        {
            details::construct_with(a, std::addressof(*current), value);
        }
        guard.release();
    }

    template<class NoThrowForwardIt, class Size, class T, class Alloc>
    NoThrowForwardIt uninitialized_fill_n_a(NoThrowForwardIt first,
        Size count, const T& value, Alloc& a)
    {
        NoThrowForwardIt current = first;
        auto guard = details::scope_guard{ [&]() { destroy_a(first, current, a); } };
        for (; count != 0; (void) ++current, --count)
        {
            details::construct_with(a, std::addressof(*current), value);
        }
        guard.release();
        return current;
    }

    template<class InputIt, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_move_a(InputIt first, InputIt last,
        NoThrowForwardIt d_first, Alloc& a)
    {
        auto current = d_first;
        auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
        for (; first != last; ++first, (void) ++current)
        {
            auto addr = std::addressof(*current);
            // 源是左值时按右值搬移；源本身已是右值（例如 transform_view 迭代器
            // 返回的纯右值）时原样传进去，从而发生保证的复制消除
            if constexpr (std::is_lvalue_reference_v<decltype(*first)>)
            {
                details::construct_from_moved(a, addr, first);
            }
            else
            {
                details::construct_from(a, addr, first);
            }
        }
        guard.release();
        return current;
    }

    template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
    std::pair<InputIt, NoThrowForwardIt> uninitialized_move_n_a(InputIt first, Size count,
        NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
        for (; count != 0; ++first, (void) ++current, --count)
        {
            auto addr = std::addressof(*current);
            if constexpr (std::is_lvalue_reference_v<decltype(*first)>)
            {
                details::construct_from_moved(a, addr, first);
            }
            else
            {
                details::construct_from(a, addr, first);
            }
        }
        guard.release();
        return { first, current };
    }

    template<class NoThrowForwardIt, class Alloc>
    void uninitialized_default_construct_a(NoThrowForwardIt first, NoThrowForwardIt last, Alloc& a)
    {
        NoThrowForwardIt current = first;
        auto guard = details::scope_guard{ [&]() { destroy_a(first, current, a); } };
        for (; current != last; ++current)
        {
            details::construct_with(a, std::addressof(*current));
        }
        guard.release();
    }

    template<class NoThrowForwardIt, class Size, class Alloc>
    NoThrowForwardIt uninitialized_default_construct_n_a(NoThrowForwardIt first, Size count, Alloc& a)
    {
        NoThrowForwardIt current = first;
        auto guard = details::scope_guard{ [&]() { destroy_a(first, current, a); } };
        for (; count != 0; (void) ++current, --count)
        {
            details::construct_with(a, std::addressof(*current));
        }
        guard.release();
        return current;
    }
}

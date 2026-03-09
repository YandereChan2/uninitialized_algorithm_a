#pragma once
#include <memory>
#include <iterator>
namespace Yc
{
    template<class ForwardIt, class Alloc>
    void destroy_a(ForwardIt first, ForwardIt last, Alloc& a)
    {
        for (; first != last; ++first)
        {
            std::allocator_traits<Alloc>::destroy(a, std::addressof(*first));
        }
    }
    template<class ForwardIt, class Size, class Alloc>
    ForwardIt destroy_n_a(ForwardIt first, Size n, Alloc& a)
    {
        for (; n > 0; (void) ++first, --n)
        {
            std::allocator_traits<Alloc>::destroy(a, std::addressof(*first));
        }
        return first;
    }
    template<class InputIt, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_copy_a(InputIt first, InputIt last, NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        try
        {
            for (; first != last; ++first, (void) ++current)
            {
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current), *first);
            }
                
            return current;
        }
        catch (...)
        {
            for (; d_first != current; ++d_first)
            {
                std::allocator_traits<Alloc>::destroy(a, std::addressof(*d_first));
            }
            throw;
        }
    }
    template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_copy_n_a(InputIt first, Size count,
        NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        try
        {
            for (; count > 0; ++first, (void) ++current, --count)
            {
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current), *first);
            }
        }
        catch (...)
        {
            for (; d_first != current; ++d_first)
            {
                std::allocator_traits<Alloc>::destroy(a, std::addressof(*d_first));
            }
            throw;
        }
        return current;
    }
    template<class NoThrowForwardIt, class T, class Alloc>
    void uninitialized_fill_a(NoThrowForwardIt first, NoThrowForwardIt last, const T& value, Alloc& a)
    {
        NoThrowForwardIt current = first;
        try
        {
            for (; current != last; ++current)
            {
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current), value);
            }
        }
        catch (...)
        {
            for (; first != current; ++first)
            {
                std::allocator_traits<Alloc>::destroy(a, std::addressof(*first));
            }
            throw;
        }
    }
    template<class NoThrowForwardIt, class Size, class T, class Alloc>
    NoThrowForwardIt uninitialized_fill_n_a(NoThrowForwardIt first,
        Size count, const T& value)
    {
        NoThrowForwardIt current = first;
        try
        {
            for (; count > 0; ++current, (void) --count)
            {
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current), value);
            }
            return current;
        }
        catch (...)
        {
            for (; first != current; ++first)
            {
                std::allocator_traits<Alloc>::destroy(a, std::addressof(*first));
            }
            throw;
        }
    }
    template<class InputIt, class NoThrowForwardIt, class Alloc>
    NoThrowForwardIt uninitialized_move_a(InputIt first, InputIt last,
        NoThrowForwardIt d_first, Alloc& a)
    {
        auto current = d_first;
        try
        {
            for (; first != last; ++first, (void) ++current)
            {
                auto addr = std::addressof(*current);
                if constexpr (std::is_lvalue_reference_v<decltype(*first)>)
                {
                    std::allocator_traits<Alloc>::construct(a, addr, std::move(*first));
                }
                else
                {
                    std::allocator_traits<Alloc>::construct(a, addr, *first);
                }
            }
            return current;
        }
        catch (...)
        {
            std::destroy_a(d_first, current, a);
            throw;
        }
    }
    template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
    std::pair<InputIt, NoThrowForwardIt>uninitialized_move_n_a(InputIt first, Size count, NoThrowForwardIt d_first, Alloc& a)
    {
        NoThrowForwardIt current = d_first;
        try
        {
            for (; count > 0; ++first, (void) ++current, --count) {
                auto addr = std::addressof(*current);
                if constexpr (std::is_lvalue_reference_v<decltype(*first)>)
                {
                    std::allocator_traits<Alloc>::construct(a, addr, std::move(*first));
                }
                else
                {
                    std::allocator_traits<Alloc>::construct(a, addr, *first);
                }
            }
        }
        catch (...)
        {
            std::destroy_a(d_first, current, a);
            throw;
        }
        return { first, current };
    }

    template<class NoThrowForwardIt, class Alloc>
    void uninitialized_default_construct_a(NoThrowForwardIt first, NoThrowForwardIt last, Alloc& a)
    {
        NoThrowForwardIt current = first;
        try
        {
            for (; current != last; ++current)
            {
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current));
            }
        }
        catch (...)
        {
            std::destroy_a(first, current, a);
            throw;
        }
    }

    template<class NoThrowForwardIt, class Size, class Alloc>
    NoThrowForwardIt uninitialized_default_construct_n_a(NoThrowForwardIt first, Size count, Alloc& a)
    {
        NoThrowForwardIt current = first;
        try
        {
            for (; countn > 0; (void) ++current, --count)
                std::allocator_traits<Alloc>::construct(a, std::addressof(*current));
            return current;
        }
        catch (...)
        {
            std::destroy_a(first, current, a);
            throw;
        }
    }

}
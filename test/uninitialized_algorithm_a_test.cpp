// uninitialized_algorithm_a_test.cpp
//
// 自包含测试，不依赖任何测试框架。覆盖：
//   1. 分配器定制检测（allocator_has_construct_v / allocator_has_destroy_v）
//   2. 8 个构造算法的异常安全与返回值
//   3. 复制消除的实测计数
//   4. 与标准语义的对照（值初始化、值类型不同、空区间）
//   5. destroy / destroy_n 是否尊重定制的 destroy
//
// 编译（MSVC）：
//   cl /nologo /std:c++latest /EHsc /W4 /utf-8 /I. test\uninitialized_algorithm_a_test.cpp
// 编译（GCC / Clang）：
//   g++ -std=c++20 -Wall -Wextra -I. test/uninitialized_algorithm_a_test.cpp -o test_uninitialized
//
// 全部通过时打印 "ALL CHECKS PASSED" 并以 0 退出。

#include "uninitialized_algorithm_a.h"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <utility>

namespace
{
    int failures = 0;

    void check(bool ok, const char* expr, int line)
    {
        if (!ok)
        {
            std::printf("FAIL (line %d): %s\n", line, expr);
            ++failures;
        }
    }

#define CHECK(expr) check((expr), #expr, __LINE__)

    // ------------------------------------------------------------------
    // 计数类型：统计构造/析构次数，并可在第 N 次构造尝试时抛异常
    // ------------------------------------------------------------------
    struct probe
    {
        static inline int attempts = 0;   // 构造尝试次数
        static inline int live = 0;       // 存活对象数
        static inline int copies = 0;
        static inline int moves = 0;
        static inline int throw_at = 0;   // 非 0：第 throw_at 次构造尝试抛异常

        int v = 0;

        probe() { reg(); }
        explicit probe(int x) : v(x) { reg(); }
        probe(const probe& o) : v(o.v) { reg(); ++copies; }
        probe(probe&& o) : v(o.v) { reg(); ++moves; o.v = -1; }
        ~probe() { --live; }

        static void reg()
        {
            ++attempts;
            if (throw_at != 0 && attempts == throw_at)
            {
                throw 42;
            }
            ++live;
        }
        static void reset_events()
        {
            attempts = 0;
            copies = 0;
            moves = 0;
            throw_at = 0;
        }
        static void reset_all()
        {
            reset_events();
            live = 0;
        }
    };

    // ------------------------------------------------------------------
    // 各种分配器
    // ------------------------------------------------------------------

    // 只做内存管理，不定制 construct / destroy
    template<class T>
    struct plain_alloc
    {
        using value_type = T;

        plain_alloc() = default;
        template<class U>
        plain_alloc(const plain_alloc<U>&)noexcept
        {
        }

        T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
        void deallocate(T* p, std::size_t n)noexcept { std::allocator<T>{}.deallocate(p, n); }
    };

    // 定制 construct / destroy，并记录调用次数
    template<class T>
    struct custom_alloc
    {
        using value_type = T;
        static inline int constructs = 0;
        static inline int destroys = 0;

        custom_alloc() = default;
        template<class U>
        custom_alloc(const custom_alloc<U>&)noexcept
        {
        }

        T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
        void deallocate(T* p, std::size_t n)noexcept { std::allocator<T>{}.deallocate(p, n); }

        template<class U, class... Args>
        void construct(U* p, Args&&... args)
        {
            ++constructs;
            std::construct_at(p, std::forward<Args>(args)...);
        }
        template<class U>
        void destroy(U* p)noexcept
        {
            ++destroys;
            std::destroy_at(p);
        }
    };

    // 只定制 destroy，不定制 construct
    template<class T>
    struct only_destroy_alloc
    {
        using value_type = T;
        static inline int destroys = 0;

        only_destroy_alloc() = default;
        template<class U>
        only_destroy_alloc(const only_destroy_alloc<U>&)noexcept
        {
        }

        T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
        void deallocate(T* p, std::size_t n)noexcept { std::allocator<T>{}.deallocate(p, n); }
        void destroy(T* p)noexcept
        {
            ++destroys;
            std::destroy_at(p);
        }
    };

    // 只定制带参 construct（没有无参重载）
    template<class T>
    struct selective_alloc
    {
        using value_type = T;
        static inline int constructs = 0;

        selective_alloc() = default;
        template<class U>
        selective_alloc(const selective_alloc<U>&)noexcept
        {
        }

        T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
        void deallocate(T* p, std::size_t n)noexcept { std::allocator<T>{}.deallocate(p, n); }
        void construct(T* p, const T& v)
        {
            ++constructs;
            std::construct_at(p, v);
        }
    };

    // 解引用返回纯右值的输入迭代器（模拟 transform_view 的迭代器）
    struct prvalue_it
    {
        using value_type = probe;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;

        int i = 0;

        probe operator*() const { return probe{ i }; }
        prvalue_it& operator++()
        {
            ++i;
            return *this;
        }
        prvalue_it operator++(int)
        {
            prvalue_it t = *this;
            ++i;
            return t;
        }
        friend bool operator==(const prvalue_it&, const prvalue_it&) = default;
    };

    constexpr int N = 4;

    struct alignas(probe) storage
    {
        unsigned char bytes[sizeof(probe) * N];
    };

    probe* dst_of(storage& s) { return reinterpret_cast<probe*>(s.bytes); }

    // ------------------------------------------------------------------
    // 1. 分配器定制检测
    // ------------------------------------------------------------------
    void test_traits()
    {
        std::puts("[1] 分配器定制检测");

        constexpr bool std_construct = Yc::allocator_has_construct_v<std::allocator<probe>, probe, probe>;
        constexpr bool std_destroy = Yc::allocator_has_destroy_v<std::allocator<probe>, probe>;
        constexpr bool plain_construct = Yc::allocator_has_construct_v<plain_alloc<probe>, probe, probe>;
        constexpr bool plain_destroy = Yc::allocator_has_destroy_v<plain_alloc<probe>, probe>;
        constexpr bool custom_construct = Yc::allocator_has_construct_v<custom_alloc<probe>, probe, probe>;
        constexpr bool custom_destroy = Yc::allocator_has_destroy_v<custom_alloc<probe>, probe>;
        constexpr bool only_destroy_destroy = Yc::allocator_has_destroy_v<only_destroy_alloc<probe>, probe>;
        constexpr bool only_destroy_construct = Yc::allocator_has_construct_v<only_destroy_alloc<probe>, probe, probe>;
        constexpr bool selective_with_arg = Yc::allocator_has_construct_v<selective_alloc<probe>, probe, const probe&>;
        constexpr bool selective_without_arg = Yc::allocator_has_construct_v<selective_alloc<probe>, probe>;

        std::printf("    std::allocator   : construct=%d destroy=%d\n", (int)std_construct, (int)std_destroy);
        std::printf("    plain_alloc      : construct=%d destroy=%d\n", (int)plain_construct, (int)plain_destroy);
        std::printf("    custom_alloc     : construct=%d destroy=%d\n", (int)custom_construct, (int)custom_destroy);
        std::printf("    only_destroy     : construct=%d destroy=%d\n", (int)only_destroy_construct, (int)only_destroy_destroy);
        std::printf("    selective(带参/无参): construct=%d/%d\n", (int)selective_with_arg, (int)selective_without_arg);

        CHECK(!std_construct);
        CHECK(!std_destroy);
        CHECK(!plain_construct);
        CHECK(!plain_destroy);
        CHECK(custom_construct);
        CHECK(custom_destroy);
        CHECK(!only_destroy_construct);
        CHECK(only_destroy_destroy);
        CHECK(selective_with_arg);      // 只按实参良构性判定
        CHECK(!selective_without_arg);
    }

    // ------------------------------------------------------------------
    // 2. 异常安全与返回值
    // ------------------------------------------------------------------
    template<class Alloc>
    void test_copy(Alloc& a)
    {
        probe::reset_all();
        {
            probe src[N];
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 3;                      // 第 3 个目标元素构造失败
            bool caught = false;
            try
            {
                Yc::uninitialized_copy_a(src, src + N, dst, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == N);                  // 目标区间已全部回滚
            CHECK(probe::copies == 2);                // 成功构造了 2 个

            probe::reset_events();
            probe* e = Yc::uninitialized_copy_a(src, src + N, dst, a);
            CHECK(e == dst + N);
            CHECK(probe::copies == N);
            Yc::destroy_a(dst, e, a);
            CHECK(probe::live == N);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_copy_n(Alloc& a)
    {
        probe::reset_all();
        {
            probe src[N];
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 4;
            bool caught = false;
            try
            {
                Yc::uninitialized_copy_n_a(src, N, dst, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == N);
            CHECK(probe::copies == 3);

            probe::reset_events();
            probe* e = Yc::uninitialized_copy_n_a(src, N, dst, a);
            CHECK(e == dst + N);
            Yc::destroy_n_a(dst, N, a);
            CHECK(probe::live == N);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_fill(Alloc& a)
    {
        probe::reset_all();
        {
            storage s;
            probe* dst = dst_of(s);
            probe value;

            probe::reset_events();
            probe::throw_at = 3;
            bool caught = false;
            try
            {
                Yc::uninitialized_fill_a(dst, dst + N, value, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == 1);                  // 只剩 value
            CHECK(probe::copies == 2);

            probe::reset_events();
            Yc::uninitialized_fill_a(dst, dst + N, value, a);
            CHECK(probe::live == 1 + N);
            Yc::destroy_a(dst, dst + N, a);
            CHECK(probe::live == 1);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_fill_n(Alloc& a)
    {
        probe::reset_all();
        {
            storage s;
            probe* dst = dst_of(s);
            probe value;

            probe::reset_events();
            probe::throw_at = 2;
            bool caught = false;
            try
            {
                Yc::uninitialized_fill_n_a(dst, N, value, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == 1);
            CHECK(probe::copies == 1);

            probe::reset_events();
            probe* e = Yc::uninitialized_fill_n_a(dst, N, value, a);
            CHECK(e == dst + N);
            Yc::destroy_a(dst, e, a);
            CHECK(probe::live == 1);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_move(Alloc& a)
    {
        probe::reset_all();
        {
            probe src[N];
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 3;
            bool caught = false;
            try
            {
                Yc::uninitialized_move_a(std::make_move_iterator(src), std::make_move_iterator(src + N), dst, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == N);
            CHECK(probe::moves == 2);

            probe::reset_events();
            probe* e = Yc::uninitialized_move_a(std::make_move_iterator(src), std::make_move_iterator(src + N), dst, a);
            CHECK(e == dst + N);
            CHECK(probe::moves == N);
            Yc::destroy_a(dst, e, a);
            CHECK(probe::live == N);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_move_n(Alloc& a)
    {
        probe::reset_all();
        {
            probe src[N];
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 2;
            bool caught = false;
            try
            {
                Yc::uninitialized_move_n_a(std::make_move_iterator(src), N, dst, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == N);
            CHECK(probe::moves == 1);

            probe::reset_events();
            auto [si, di] = Yc::uninitialized_move_n_a(std::make_move_iterator(src), N, dst, a);
            CHECK(di == dst + N);
            CHECK(si.base() == src + N);
            Yc::destroy_a(dst, di, a);
            CHECK(probe::live == N);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_default_construct(Alloc& a)
    {
        probe::reset_all();
        {
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 3;
            bool caught = false;
            try
            {
                Yc::uninitialized_default_construct_a(dst, dst + N, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == 0);

            probe::reset_events();
            Yc::uninitialized_default_construct_a(dst, dst + N, a);
            CHECK(probe::live == N);
            Yc::destroy_a(dst, dst + N, a);
            CHECK(probe::live == 0);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_default_construct_n(Alloc& a)
    {
        probe::reset_all();
        {
            storage s;
            probe* dst = dst_of(s);

            probe::reset_events();
            probe::throw_at = 2;
            bool caught = false;
            try
            {
                Yc::uninitialized_default_construct_n_a(dst, N, a);
            }
            catch (int)
            {
                caught = true;
            }
            CHECK(caught);
            CHECK(probe::live == 0);

            probe::reset_events();
            probe* e = Yc::uninitialized_default_construct_n_a(dst, N, a);
            CHECK(e == dst + N);
            CHECK(probe::live == N);
            probe* d = Yc::destroy_n_a(dst, N, a);
            CHECK(d == dst + N);
            CHECK(probe::live == 0);
        }
        CHECK(probe::live == 0);
    }

    template<class Alloc>
    void test_all_algorithms(Alloc& a)
    {
        test_copy(a);
        test_copy_n(a);
        test_fill(a);
        test_fill_n(a);
        test_move(a);
        test_move_n(a);
        test_default_construct(a);
        test_default_construct_n(a);
    }

    // ------------------------------------------------------------------
    // 3. 复制消除
    // ------------------------------------------------------------------
    void test_elision()
    {
        std::puts("[3] 复制消除（纯右值来源）");

        plain_alloc<probe> plain;
        std::allocator<probe> standard;
        custom_alloc<probe> custom;

        {
            probe::reset_all();
            storage s;
            probe* dst = dst_of(s);
            probe::reset_events();
            Yc::uninitialized_move_a(prvalue_it{ 0 }, prvalue_it{ N }, dst, plain);
            std::printf("    plain_alloc   move_a : copy=%d move=%d\n", probe::copies, probe::moves);
            CHECK(probe::copies == 0);      // 保证复制消除
            CHECK(probe::moves == 0);
            probe::reset_events();
            Yc::uninitialized_copy_a(prvalue_it{ 0 }, prvalue_it{ N }, dst, plain);
            std::printf("    plain_alloc   copy_a : copy=%d move=%d\n", probe::copies, probe::moves);
            CHECK(probe::copies == 0);
            CHECK(probe::moves == 0);
            Yc::destroy_a(dst, dst + N, plain);
        }
        {
            probe::reset_all();
            storage s;
            probe* dst = dst_of(s);
            probe::reset_events();
            Yc::uninitialized_move_a(prvalue_it{ 0 }, prvalue_it{ N }, dst, standard);
            std::printf("    std::allocator move_a: copy=%d move=%d\n", probe::copies, probe::moves);
            CHECK(probe::copies == 0);
            CHECK(probe::moves == 0);
            Yc::destroy_a(dst, dst + N, standard);
        }
        {
            probe::reset_all();
            storage s;
            probe* dst = dst_of(s);
            probe::reset_events();
            Yc::uninitialized_move_a(prvalue_it{ 0 }, prvalue_it{ N }, dst, custom);
            std::printf("    custom_alloc  move_a : copy=%d move=%d\n", probe::copies, probe::moves);
            CHECK(probe::copies == 0);
            CHECK(probe::moves == N);       // 走了分配器，纯右值退化成移动构造
            Yc::destroy_a(dst, dst + N, custom);
        }
        CHECK(probe::live == 0);
    }

    // ------------------------------------------------------------------
    // 4. 与标准语义对照
    // ------------------------------------------------------------------
    void test_semantics()
    {
        std::puts("[4] 语义对照");

        std::allocator<int> ia;
        alignas(int) unsigned char ibuf[sizeof(int) * 3];
        int* ip = reinterpret_cast<int*>(ibuf);

        // default_construct 是值初始化：int 应被清零
        for (int i = 0; i != 3; ++i)
        {
            ip[i] = 0x7f7f7f7f;
        }
        int* ie = Yc::uninitialized_default_construct_n_a(ip, 3, ia);
        CHECK(ie == ip + 3);
        CHECK(ip[0] == 0 && ip[1] == 0 && ip[2] == 0);
        Yc::destroy_a(ip, ie, ia);

        // 计数为 0：不构造任何元素
        int zero_src[1] = { 1 };
        CHECK(Yc::uninitialized_copy_n_a(zero_src, 0, ip, ia) == ip);
        CHECK(Yc::uninitialized_default_construct_n_a(ip, std::size_t{ 0 }, ia) == ip);
        CHECK(Yc::destroy_n_a(ip, 0u, ia) == ip);

        // fill 的值类型（long）与元素类型（int）不同
        long value = 7;
        int* fe = Yc::uninitialized_fill_n_a(ip, 3, value, ia);
        CHECK(fe == ip + 3);
        CHECK(ip[0] == 7 && ip[1] == 7 && ip[2] == 7);
        Yc::destroy_a(ip, fe, ia);
    }

    // ------------------------------------------------------------------
    // 5. 只定制 destroy / 只定制带参 construct 的分配器
    // ------------------------------------------------------------------
    void test_partial_customization()
    {
        std::puts("[5] 部分定制的分配器");

        // 只定制 destroy：绑定式算法仍然直接就地构造（未定制 construct）
        only_destroy_alloc<int> a;
        alignas(int) unsigned char buf[sizeof(int) * 4];
        int* dst = reinterpret_cast<int*>(buf);
        int src[4] = { 1, 2, 3, 4 };

        int* e = Yc::uninitialized_copy_n_a(src, 4, dst, a);
        CHECK(e == dst + 4);
        CHECK(only_destroy_alloc<int>::destroys == 0);
        Yc::destroy_n_a(dst, 4, a);
        CHECK(only_destroy_alloc<int>::destroys == 4);

        // const 元素：a.destroy(const int*) 不良构，退回内建析构（与 allocator_traits 一致）
        const int carr[2] = { 5, 6 };
        Yc::destroy_a(carr, carr + 2, a);
        CHECK(only_destroy_alloc<int>::destroys == 4);
        std::allocator_traits<only_destroy_alloc<int>>::destroy(a, carr);   // 对照
        CHECK(only_destroy_alloc<int>::destroys == 4);

        // 只定制带参 construct：有参构造走分配器，无参构造退回值初始化
        selective_alloc<int> s;
        alignas(int) unsigned char sbuf[sizeof(int) * 3];
        int* sd = reinterpret_cast<int*>(sbuf);
        int seven = 7;
        int* se = Yc::uninitialized_fill_n_a(sd, 3, seven, s);
        CHECK(selective_alloc<int>::constructs == 3);
        CHECK(sd[0] == 7 && sd[2] == 7);
        Yc::destroy_a(sd, se, s);

        for (int i = 0; i != 3; ++i)
        {
            sd[i] = 0x7f7f7f7f;
        }
        Yc::uninitialized_default_construct_n_a(sd, 3, s);
        CHECK(selective_alloc<int>::constructs == 3);       // 没有走定制版本
        CHECK(sd[0] == 0 && sd[2] == 0);                    // 值初始化
        Yc::destroy_n_a(sd, 3, s);
    }
}

int main()
{
    test_traits();                    // [1]

    std::puts("[2] 异常安全与返回值");
    {
        custom_alloc<probe> a;
        custom_alloc<probe>::constructs = 0;
        custom_alloc<probe>::destroys = 0;
        test_all_algorithms(a);
        std::printf("    定制分配器被调用: construct=%d destroy=%d\n",
            custom_alloc<probe>::constructs, custom_alloc<probe>::destroys);
        CHECK(custom_alloc<probe>::constructs > 0);
        CHECK(custom_alloc<probe>::destroys > 0);
    }
    {
        plain_alloc<probe> a;
        test_all_algorithms(a);
    }
    {
        std::allocator<probe> a;
        test_all_algorithms(a);
    }

    test_elision();                   // [3]
    test_semantics();                 // [4]
    test_partial_customization();     // [5]

    if (failures == 0)
    {
        std::puts("\nALL CHECKS PASSED");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", failures);
    return 1;
}

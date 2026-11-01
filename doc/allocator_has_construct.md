# `Yc::allocator_has_construct` / `Yc::allocator_has_destroy`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class Alloc, class T, class... Args>
struct allocator_has_construct;

template<class T, class U, class... Args>
struct allocator_has_construct<std::allocator<T>, U, Args...>;   // 特化

template<class Alloc, class T, class... Args>
inline constexpr bool allocator_has_construct_v;                 // (1)

template<class Alloc, class T>
struct allocator_has_destroy;

template<class T, class U>
struct allocator_has_destroy<std::allocator<T>, U>;              // 特化

template<class Alloc, class T>
inline constexpr bool allocator_has_destroy_v;                   // (2)
```

两个类型元函数，用于在编译期回答同一个问题：**这个分配器自己是否定制了 `construct` / `destroy`？**

```C++
template<class Alloc, class T, class... Args>
inline constexpr bool allocator_has_construct_v =
    allocator_has_construct<Alloc, T, Args...>::value;

template<class Alloc, class T>
inline constexpr bool allocator_has_destroy_v =
    allocator_has_destroy<Alloc, T>::value;
```

`(1)` 判断 `std::allocator_traits<Alloc>::construct` 会不会转调 `Alloc` 自己的成员；参数 `T` 是目标对象的类型，`Args...` 是即将传给 `construct` 的实参类型。`(2)` 判断 `std::allocator_traits<Alloc>::destroy` 会不会转调 `Alloc` 自己的成员。

## 判定规则

判定标准与 `std::allocator_traits` 的规范完全一致——**不是"有没有这个名字"，而是"这个调用表达式是否良构"**：

| 元函数 | 为 `true` 的条件 |
|:-|:-|
| `allocator_has_construct_v<Alloc, T, Args...>` | `std::declval<Alloc&>().construct(std::declval<T*>(), std::declval<Args>()...)` 良构 |
| `allocator_has_destroy_v<Alloc, T>` | `std::declval<Alloc&>().destroy(std::declval<T*>())` 良构 |

这一点很重要，因为标准对 `allocator_traits` 的规定正是：

- `allocator_traits<A>::construct(a, p, args...)`：若 `a.construct(p, args...)` 良构则调用它，否则执行 `::new (static_cast<void*>(p)) T(std::forward<Args>(args)...)`；
- `allocator_traits<A>::destroy(a, p)`：若 `a.destroy(p)` 良构则调用它，否则执行 `p->~T()`。

所以当元函数为 `false` 时，**绕过 `allocator_traits` 直接就地构造/析构，与经 `allocator_traits` 的效果完全相同**，只是省掉一层分派，并让编译器看见真正的构造/析构表达式（这正是[复制消除](uninitialized_move_a.md#复制消除)能生效的前提）。当元函数为 `true` 时，算法老老实实走 `allocator_traits`，分配器定制的行为（例如注入 cookie、走池分配、uses-allocator 构造）不会被绕过。

由于判定按"调用良构性"进行，同一个分配器可以**对不同的实参给出不同答案**：例如只定义了 `construct(T*, const T&)` 的分配器，`allocator_has_construct_v<A, T, const T&>` 为 `true`，而 `allocator_has_construct_v<A, T>`（无参，默认构造）为 `false`——后者在 `allocator_traits` 那里同样会退回内建的值初始化。

## 为什么 `std::allocator` 是特化的 `false`

`std::allocator<T>` 在 C++17 中曾带有 `construct` / `destroy` 成员（C++20 起被移除），而它们的实现就是 `::new (void*p) T(args...)` 和 `p->~T()`，与内建行为一字不差。若不加特化：

- 在还保留这些成员的实现上（C++17 模式、旧版标准库），元函数会误报 `true`，算法于是退回经 `allocator_traits` 的路径；
- 而"经 `allocator_traits`"意味着实参先绑到转发形参上再进 `new` 表达式，纯右值来源会**退化成一次移动构造**，[复制消除](uninitialized_move_a.md#复制消除)失效——偏偏 `std::allocator` 是最常见的分配器。

因此对 `std::allocator<T>` 显式特化为 `std::false_type`，保证默认分配器永远走就地构造的快路径。

## 参数

|||
|-:|:-|
|`Alloc`|分配器类型（不带引用；算法内部以 `Alloc&` 持有它）|
|`T`|目标对象类型，即 `construct`/`destroy` 的第一个参数所指向的类型|
|`Args...`|构造实参类型；空包表示默认构造（`construct(p)`）|

## 备注

- 两个元函数都只是编译期计算，无运行期开销，可用于 `if constexpr`、`static_assert`、概念约束等任何常量表达式上下文。
- 判定会剥掉 `Alloc` 的 cv / 引用修饰（内部用 `std::remove_cvref_t<Alloc>`）。
- 该判定只看**分配器自己**（含其基类）的成员，不追踪 `std::allocator_traits` 的其它定制点（如 `select_on_container_copy_construction`、`max_size`）；后者与本设施无关。
- 若分配器的 `construct` 是成员模板，则对任意实参都良构，元函数恒为 `true`——这与 `allocator_traits` 的判定一致。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>
#include <utility>

// 只做内存管理，不定制 construct / destroy
template<class T>
struct plain_alloc
{
    using value_type = T;
    T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }
};

// 定制了 construct / destroy
template<class T>
struct custom_alloc : plain_alloc<T>
{
    using value_type = T;
    template<class U, class... Args>
    void construct(U* p, Args&&... args)
    {
        std::puts("custom construct");
        std::construct_at(p, std::forward<Args>(args)...);
    }
    template<class U>
    void destroy(U* p) { std::destroy_at(p); }
};

int main()
{
    static_assert(!Yc::allocator_has_construct_v<std::allocator<int>, int, int>);
    static_assert(!Yc::allocator_has_destroy_v<std::allocator<int>, int>);
    static_assert(!Yc::allocator_has_construct_v<plain_alloc<int>, int, int>);
    static_assert(Yc::allocator_has_construct_v<custom_alloc<int>, int, int>);
    static_assert(Yc::allocator_has_destroy_v<custom_alloc<int>, int>);

    custom_alloc<int> a;
    int* p = a.allocate(1);
    if constexpr (Yc::allocator_has_construct_v<custom_alloc<int>, int, int>)
    {
        std::allocator_traits<custom_alloc<int>>::construct(a, p, 42);   // 走定制版本
    }
    std::printf("%d\n", *p);
    if constexpr (Yc::allocator_has_destroy_v<custom_alloc<int>, int>)
    {
        std::allocator_traits<custom_alloc<int>>::destroy(a, p);
    }
    a.deallocate(p, 1);
}
```

### 输出

```plain text
custom construct
42
```

---

## 另请参阅

- [`uninitialized_copy_a`](uninitialized_copy_a.md) / [`uninitialized_move_a`](uninitialized_move_a.md) —— 使用这两个元函数启用复制消除的算法
- [`destroy_a`](destroy_a.md) —— 使用 `allocator_has_destroy_v` 的算法
- [`std::allocator_traits`](https://zh.cppreference.com/w/cpp/memory/allocator_traits)
- [`std::allocator::construct`](https://zh.cppreference.com/w/cpp/memory/allocator/construct)（C++17 弃用，C++20 移除）

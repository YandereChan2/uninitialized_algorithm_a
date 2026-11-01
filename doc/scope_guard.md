# `Yc::details::scope_guard`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class F>
class scope_guard
{
public:
    explicit scope_guard(F f) noexcept(std::is_nothrow_move_constructible_v<F>);

    scope_guard(const scope_guard&) = delete;
    scope_guard& operator=(const scope_guard&) = delete;
    scope_guard(scope_guard&&) = delete;
    scope_guard& operator=(scope_guard&&) = delete;

    ~scope_guard() noexcept;

    void release() noexcept;
};
```

一个最小的 **scope guard**（作用域守卫）：构造时接管一个清理动作 `f`，析构时若尚未 `release()` 就执行它。用来取代下面这种每个算法都要写一遍的样板代码：

```C++
// 旧写法：try - catch(...) - throw
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
    destroy_a(d_first, current, a);
    throw;
}
```

改写成：

```C++
// 新写法：scope guard
auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
for (; first != last; ++first, (void) ++current)
{
    details::construct_from(a, std::addressof(*current), first);
}
guard.release();
return current;
```

两种写法语义相同：正常结束前 `release()` 撤销清理；构造过程中抛出异常时，栈展开会销毁 `guard`，执行清理，随后异常继续向外传播。差别在于新写法不需要 `catch (...)`，不会吞掉或改变异常的类型，也不会把清理逻辑和正常返回逻辑搅在同一个函数里（`release()` 这一行本身就把"成功路径到此为止"写在代码里了）。

## 参数

|||
|-:|:-|
|`F`|清理动作的类型；通常是一个 lambda，需要可移动构造、可调用且不抛异常|

## 成员函数

|||
|-:|:-|
|`(1)` 构造函数|`explicit scope_guard(F f)`，以 `std::move(f)` 初始化内部存储；`noexcept` 条件取决于 `F` 的移动构造|
|`(2)` 析构函数|若未 `release()`，调用清理动作；**始终 `noexcept`**|
|`(3)` `release()`|撤销清理动作；此后析构不再做任何事|

拷贝构造、拷贝赋值、移动构造、移动赋值全部被删除：守卫一旦创建就固定绑定到当前作用域，不允许复制（否则清理会执行两次）或移动（否则被移出的那个对象析构时会误执行清理）。

## 为什么析构函数是 `noexcept`

scope guard 的价值恰恰在于栈展开期间执行清理，而"栈展开期间抛出第二个异常"会直接 `std::terminate`。既然清理动作本来就不允许抛出（`destroy`/析构函数按分配器要求都是 `noexcept`），就把这条约定写进类型系统：清理动作抛出 => `std::terminate`，而不是让程序带着两个在途异常继续跑。

## 备注

- 位于 `Yc::details`，是本头文件的实现细节，不属于公开 API；文档收录它是因为读算法实现时会遇到。
- 不可移动是刻意的：这保证了"每个守卫恰好对应一个作用域"。如果确实需要把守卫传出去，请显式地用 `std::unique_ptr` 之类的所有权工具重新设计，而不是放宽这里的删除函数。
- 守卫只负责"做什么"，不负责"什么时候释放资源"——`release()` 的调用时机完全由算法自己掌握，因此可以精确地在返回值计算完毕之后、`return` 之前撤销。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

struct noisy
{
    int id;
    ~noisy() { std::printf("destroy %d\n", id); }
};

int main()
{
    std::allocator<noisy> a;
    noisy* p = std::allocator_traits<decltype(a)>::allocate(a, 3);

    // (1) 正常路径：release() 之后不执行清理
    {
        auto guard = Yc::details::scope_guard{ [&]() { std::puts("cleanup (1)"); } };
        std::puts("body (1)");
        guard.release();
    }

    // (2) 异常路径：栈展开时执行清理
    try
    {
        auto guard = Yc::details::scope_guard{ [&]() { std::puts("cleanup (2)"); } };
        std::puts("body (2)");
        throw std::runtime_error{ "boom" };
    }
    catch (const std::exception& e)
    {
        std::printf("caught: %s\n", e.what());
    }

    std::allocator_traits<decltype(a)>::deallocate(a, p, 3);
}
```

### 输出

```plain text
body (1)
body (2)
cleanup (2)
caught: boom
```

---

## 另请参阅

- [`destroy_a`](destroy_a.md) —— 清理动作通常就是它
- [`std::experimental::scope_exit`](https://zh.cppreference.com/w/cpp/experimental/scope_exit) —— 标准化提案中的同类设施（C++ 库基础 TS）

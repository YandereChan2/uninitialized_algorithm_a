# `uninitialized_algorithm_a`

带有**分配器参数**的未初始化内存算法：`std::uninitialized_copy / move / fill / default_construct / destroy` 系列在"构造与析构经由 `std::allocator_traits` 分派"时的对应物。单头文件，只依赖标准库。

容器扩容、`std::vector` 式的 `reallocate`、对象池、自定义内存资源等场景都需要"先在裸存储上构造元素，出错时把已构造的那一段撤回"这套动作。标准库只提供了不带分配器的版本，于是每个实现这种设施的库都要重写一遍；本头文件就是干净的那一份，并且额外处理了两件容易被忽略的事：**异常安全**与**按分配器定制情况选择构造路径**。

## 功能一览

| 函数 | 对应的标准设施 | 说明 |
|:-|:-|:-|
| [`destroy_a` / `destroy_n_a`](doc/destroy_a.md) | `std::destroy` / `std::destroy_n` | 经由分配器销毁已构造的元素 |
| [`uninitialized_copy_a` / `uninitialized_copy_n_a`](doc/uninitialized_copy_a.md) | `std::uninitialized_copy` / `_n` | 复制构造到未初始化存储 |
| [`uninitialized_move_a` / `uninitialized_move_n_a`](doc/uninitialized_move_a.md) | `std::uninitialized_move` / `_n` | 移动构造到未初始化存储，纯右值来源可复制消除 |
| [`uninitialized_fill_a` / `uninitialized_fill_n_a`](doc/uninitialized_fill_a.md) | `std::uninitialized_fill` / `_n` | 用同一个值填充未初始化存储 |
| [`uninitialized_default_construct_a` / `_n_a`](doc/uninitialized_default_construct_a.md) | `std::uninitialized_default_construct` / `_n` | 默认构造（本库是**值初始化**） |
| [`allocator_has_construct` / `allocator_has_destroy`](doc/allocator_has_construct.md) | —— | 编译期判定分配器是否定制了 `construct` / `destroy` |
| [`details::scope_guard`](doc/scope_guard.md) | —— | 实现内部使用的异常安全工具 |

## 设计要点

### 1. 异常安全不用 `try-catch`

旧式的写法是每个算法都包一层：

```C++
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

本实现改用 [`details::scope_guard`](doc/scope_guard.md)：

```C++
auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
for (; first != last; ++first, (void) ++current)
{
    details::construct_from(a, std::addressof(*current), first);
}
guard.release();
return current;
```

清理动作只在"没有正常走完"时执行，异常类型、异常对象都不需要被捕获再重抛；析构函数是 `noexcept`，把"析构不得抛出"这条约定写进了类型系统。整个头文件里没有任何 `try` / `catch` / `throw`。

### 2. 循环边界一律使用不等号

所有 `for` 的边界条件都是 `!=`，计数循环写成倒计数形式：

```C++
for (; first != last; ++first, (void) ++current) { ... }   // 区间循环
for (; count != 0; (void) ++current, --count) { ... }      // 计数循环
```

只对 `n != 0` 做判断的写法天然贴合"还剩几个"的语义，也避免 `i < n` 这类混合符号比较（当 `n` 的符号性与 `i` 不同时还会引入隐式转换）。代价是：`*_n` 系列沿用标准的前置条件，**计数必须非负**，负数属于未定义行为，不会被当作空区间。

### 3. 分配器定制检测与复制消除

[`allocator_has_construct_v` / `allocator_has_destroy_v`](doc/allocator_has_construct.md) 回答"分配器自己是否定制了 `construct` / `destroy`"，判定规则与 `std::allocator_traits` 完全一致（按调用表达式是否良构，而非按名字是否存在），并对 `std::allocator` 特化为"未定制"。于是：

- **未定制**时，算法绕过 `allocator_traits`，直接 `::new (...) T(args...)` 与 `std::destroy_at`。这不改变可观察行为（`allocator_traits` 在这个分支上本来就是这么做的），但把真正的构造表达式暴露给了编译器；
- **已定制**时，老老实实走分配器，定制行为（注入 cookie、池化、uses-allocator 构造等）不会被绕过。

绕过的那一层恰好也是复制消除的障碍：`allocator_traits::construct` 的实参先绑到转发形参上，纯右值来源只能退化成一次**移动构造**。而把迭代器本身传进 `::new` 表达式后：

```C++
::new (static_cast<void*>(p)) T(*it);     // *it 是纯右值 → 保证复制消除
```

对于 `std::views::transform` 这类"解引用返回纯右值"的迭代器，一次拷贝/移动都不会发生：

```C++
auto view = src | std::views::transform([](int x) { return element{ x }; });
Yc::uninitialized_move_a(view.begin(), view.end(), dst, a);
// 拷贝=0，移动=0；而经 allocator_traits 的写法会产生 3 次移动构造
```

## 快速开始

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>
#include <string>

int main()
{
    std::allocator<std::string> a;
    using traits = std::allocator_traits<decltype(a)>;

    std::string src[2] = { "hello", "world" };
    std::string* dst = traits::allocate(a, 2);

    std::string* end = Yc::uninitialized_copy_a(src, src + 2, dst, a);
    std::printf("%s %s\n", dst[0].c_str(), dst[1].c_str());

    Yc::destroy_a(dst, end, a);        // 构造与销毁都用同一个分配器
    traits::deallocate(a, dst, 2);
}
```

### 输出

```plain text
hello world
```

## API 文档

| 文档 | 内容 |
|:-|:-|
| [`doc/uninitialized_copy_a.md`](doc/uninitialized_copy_a.md) | 复制构造、异常安全、复制消除 |
| [`doc/uninitialized_move_a.md`](doc/uninitialized_move_a.md) | 移动构造、源侧取值规则、复制消除实测 |
| [`doc/uninitialized_fill_a.md`](doc/uninitialized_fill_a.md) | 用同一个值填充 |
| [`doc/uninitialized_default_construct_a.md`](doc/uninitialized_default_construct_a.md) | 默认构造（值初始化的差异说明） |
| [`doc/destroy_a.md`](doc/destroy_a.md) | 经由分配器销毁 |
| [`doc/allocator_has_construct.md`](doc/allocator_has_construct.md) | 分配器定制检测的两个类型元函数 |
| [`doc/scope_guard.md`](doc/scope_guard.md) | 异常安全用的作用域守卫 |

每个文档都包含：调用签名、语义、参数、前置条件、返回值、异常、复杂度、备注与可直接编译的示例。

## 编译要求

- **C++20 或更高**（使用 concept、`std::construct_at`、`std::destroy_at`、`std::remove_cvref_t`）。
- 头文件本身只包含 `<memory>`、`<type_traits>`、`<utility>`。
- 已在 MSVC 19.44（`/std:c++20` 与 `/std:c++latest`，`/permissive- /W4`）上验证；未使用编译器专有扩展。

## 测试

`test/uninitialized_algorithm_a_test.cpp` 是一个不依赖任何测试框架的自包含程序，覆盖：

- 分配器定制检测的取值（`std::allocator`、普通分配器、定制分配器、只定制 `destroy`、只定制带参 `construct`）；
- 8 个构造算法在"构造到第 k 个时抛异常"时的行为：已构造元素是否全部销毁、异常是否继续传播、返回值是否正确；
- 复制消除的实测计数（纯右值来源下降为 0 次拷贝/移动，定制分配器则仍走分配器）；
- 与标准语义的对照：`default_construct` 的值初始化、`fill` 的值类型与元素类型不同、计数为 0 的空区间。

MSVC：

```bat
cl /nologo /std:c++latest /EHsc /W4 /utf-8 /I. test\uninitialized_algorithm_a_test.cpp
uninitialized_algorithm_a_test.exe
```

GCC / Clang：

```sh
g++ -std=c++20 -Wall -Wextra -I. test/uninitialized_algorithm_a_test.cpp -o test_uninitialized && ./test_uninitialized
```

全部通过时打印 `ALL CHECKS PASSED` 并以 0 退出。

## 目录结构

```plain text
uninitialized_algorithm_a.h       单头文件实现
doc/                              API 文档
test/                             自包含测试
LICENSE                           MIT
```

## 许可证

[MIT](LICENSE)，Copyright (c) 2026 YandereChan2。

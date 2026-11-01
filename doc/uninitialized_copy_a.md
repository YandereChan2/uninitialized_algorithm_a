# `Yc::uninitialized_copy_a` / `Yc::uninitialized_copy_n_a`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class InputIt, class NoThrowForwardIt, class Alloc>
NoThrowForwardIt
    uninitialized_copy_a(InputIt first, InputIt last,
                         NoThrowForwardIt d_first, Alloc& a);        //(1)

template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
NoThrowForwardIt
    uninitialized_copy_n_a(InputIt first, Size count,
                           NoThrowForwardIt d_first, Alloc& a);      //(2)
```

把源区间里的元素**复制构造**到未初始化的存储上，构造动作经由分配器分派。

(1) 对 `[first, last)` 中的每个元素，以 `*first` 为实参在 `d_first` 起的连续位置上就地构造，等价于逐元素执行

```C++
std::allocator_traits<Alloc>::construct(a, std::addressof(*current), *first);
```

并返回越过最后一个被构造元素的迭代器。

(2) 同 (1)，但源与目标都只前进 `count` 次；返回值同样是越过末尾的目标迭代器。

与 [`std::uninitialized_copy`](https://zh.cppreference.com/w/cpp/memory/uninitialized_copy) 的区别只有一个：构造动作走 `allocator_traits`，因此分配器定制的 `construct` 会被调用。若分配器没有定制 `construct`（见 [`allocator_has_construct_v`](allocator_has_construct.md)），实现直接执行 `::new (static_cast<void*>(p)) T(*first)`，并因此在源为纯右值时获得[复制消除](#复制消除)。

## 异常安全

构造途中任一元素抛出异常时，**已构造好的那一段** `[d_first, current)` 会被全部销毁，随后异常继续向外传播；抛出点对应的那个元素没有构造完成，因此不会被销毁。这保证函数具有"要么全部构造、要么退回原状"的强保证（针对目标区间而言）。

实现上不再使用 `try { ... } catch (...) { destroy; throw; }`，而是

```C++
auto guard = details::scope_guard{ [&]() { destroy_a(d_first, current, a); } };
// ... 逐个构造 ...
guard.release();          // 只有正常走完才撤销清理
```

细节见 [`scope_guard`](scope_guard.md)。

## 参数

|||
|-:|:-|
|`first, last`|源元素区间|
|`count`|要复制的元素个数|
|`d_first`|目标区间的起始位置，其指向的存储必须**未构造**|
|`a`|分配器，以引用传入|

## 前置条件

- `[d_first, d_first + N)`（`N` 为源区间长度或 `count`）必须是**未构造**的、足够大的存储。
- 目标区间不得与源区间重叠。
- `*first` 必须能作为 `construct` 的实参构造目标元素类型。
- 对 `(2)`，`count` 必须非负；负数属于未定义行为。

## 返回值

越过最后一个被构造元素的目标迭代器，即 `d_first + N`。

## 异常

目标元素的构造函数（或分配器定制的 `construct`）可以抛出任意异常；抛出时按上文销毁已构造部分并原样传播该异常。目标的迭代器操作（自增、解引用）不应抛出——这是模板参数名 `NoThrowForwardIt` 的含义。

## 复杂度

恰好 N 次构造、N 次自增；不发生分配。

## 复制消除

这是本实现与"朴素的 `allocator_traits::construct` 循环"的一个实际差异。考虑源迭代器的 `operator*` 返回**纯右值**的情形（典型例子：`std::views::transform` 的迭代器）：

```C++
auto view = src | std::views::transform([](int x) { return element{ x }; });
```

此时 `*first` 是一个纯右值 `element`。如果先把它绑到转发形参上再进 `new` 表达式（即经 `allocator_traits::construct`），标准只保证它被**移动构造**到目标位置；而本实现把迭代器本身传进去，让 `*first` 直接出现在 `new` 表达式的初始化器里：

```C++
::new (static_cast<void*>(p)) T(*it);
```

于是发生 [C++17 起的保证复制消除](https://zh.cppreference.com/w/cpp/language/copy_elision)：纯右值直接在目标存储上构造，一次拷贝构造或移动构造都不会发生。对于不可移动、不可拷贝，或移动构造有可观察副作用（计数、日志）的类型，这是可测量的差别。

该快路径只在分配器未定制 `construct` 时启用；一旦分配器定制了 `construct`，算法必须走分配器的版本，复制消除自然也就不适用了。

## 备注

- 分配器以引用传入，有状态分配器的状态在调用后保留。
- 循环边界一律使用不等号（`first != last`、`count != 0` 的倒计数形式）。
- 目标迭代器可以是裸指针或任何前向迭代器；算法只要求它可以解引用并自增。
- 与标准设施一样，本算法**不分配**存储：`d_first` 指向的内存必须已经就绪。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>
#include <string>

int main()
{
    std::allocator<std::string> a;
    using traits = std::allocator_traits<decltype(a)>;

    std::string src[3] = { "alpha", "beta", "gamma" };
    std::string* dst = traits::allocate(a, 3);

    std::string* end = Yc::uninitialized_copy_a(src, src + 3, dst, a);
    for (std::string* p = dst; p != end; ++p)
    {
        std::printf("%s ", p->c_str());
    }
    std::printf("(%td 个元素)\n", end - dst);

    Yc::destroy_a(dst, end, a);
    traits::deallocate(a, dst, 3);
}
```

### 输出

```plain text
alpha beta gamma (3 个元素)
```

---

## 另请参阅

- [`uninitialized_move_a`](uninitialized_move_a.md) —— 同一套构造语义，源按右值使用
- [`uninitialized_fill_a`](uninitialized_fill_a.md)
- [`uninitialized_default_construct_a`](uninitialized_default_construct_a.md)
- [`destroy_a`](destroy_a.md) —— 异常路径上调用的清理函数
- [`allocator_has_construct`](allocator_has_construct.md) —— 决定走快路径还是分配器路径
- [`std::uninitialized_copy`](https://zh.cppreference.com/w/cpp/memory/uninitialized_copy) / [`std::uninitialized_copy_n`](https://zh.cppreference.com/w/cpp/memory/uninitialized_copy_n)

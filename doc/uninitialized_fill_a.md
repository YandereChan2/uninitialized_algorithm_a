# `Yc::uninitialized_fill_a` / `Yc::uninitialized_fill_n_a`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class NoThrowForwardIt, class T, class Alloc>
void uninitialized_fill_a(NoThrowForwardIt first, NoThrowForwardIt last,
                          const T& value, Alloc& a);                //(1)

template<class NoThrowForwardIt, class Size, class T, class Alloc>
NoThrowForwardIt uninitialized_fill_n_a(NoThrowForwardIt first, Size count,
                                        const T& value, Alloc& a);  //(2)
```

把同一个值**复制构造**到一段未初始化的存储上，构造动作经由分配器分派。

(1) 对 `[first, last)` 中的每个位置，以 `value` 为实参就地构造，等价于逐元素执行

```C++
std::allocator_traits<Alloc>::construct(a, std::addressof(*current), value);
```

(2) 同 (1)，但只处理 `count` 个位置，返回越过最后一个被构造元素的迭代器。

注意 `value` 的类型 `T` 与元素类型**不必相同**：只要目标元素类型能由 `const T&` 直接初始化即可（例如用 `long` 填充 `int` 数组）。

## 异常安全

构造途中抛出异常时，已构造好的那一段 `[first, current)` 会被全部销毁，随后异常继续向外传播；抛出点对应的元素没有构造完成，不会被销毁。实现用 [`scope_guard`](scope_guard.md) 而非 `try-catch` 完成清理。

## 参数

|||
|-:|:-|
|`first, last`|目标元素区间，其指向的存储必须**未构造**|
|`count`|要构造的元素个数|
|`value`|用来填充的值，以 `const T&` 传入|
|`a`|分配器，以引用传入|

## 前置条件

- `[first, last)`（或 `first` 起的 `count` 个位置）必须是**未构造**的、足够大的存储。
- 元素类型必须能由 `value` 直接初始化。
- 对 `(2)`，`count` 必须非负；负数属于未定义行为。

## 返回值

- `(1)` 无。
- `(2)` 越过最后一个被构造元素的迭代器，即 `first + count`。

## 异常

元素构造函数（或分配器定制的 `construct`）可以抛出任意异常；抛出时销毁已构造部分并原样传播。

## 复杂度

- `(1)` 恰好 `last - first` 次构造。
- `(2)` 恰好 `count` 次构造。

不发生分配。

## 备注

- 分配器未定制 `construct` 时（见 [`allocator_has_construct_v`](allocator_has_construct.md)），实现直接执行 `::new (static_cast<void*>(p)) T(value)`，与 `allocator_traits` 的内建分支完全等价。
- 填充不会发生"复制消除"：`value` 是左值引用，每个目标元素本来就要拷贝构造一次。
- 分配器以引用传入；循环边界一律使用不等号（`first != last`、`count != 0` 的倒计数形式）。

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

    std::string* p = traits::allocate(a, 3);

    Yc::uninitialized_fill_a(p, p + 2, std::string{ "hi" }, a);        // (1)
    std::string* e = Yc::uninitialized_fill_n_a(p + 2, 1, "there", a); // (2)

    for (std::string* it = p; it != e; ++it)
    {
        std::printf("%s%s", it == p ? "" : " ", it->c_str());
    }
    std::printf("\n");

    Yc::destroy_a(p, e, a);
    traits::deallocate(a, p, 3);
}
```

### 输出

```plain text
hi hi there
```

---

## 另请参阅

- [`uninitialized_copy_a`](uninitialized_copy_a.md) / [`uninitialized_move_a`](uninitialized_move_a.md)
- [`uninitialized_default_construct_a`](uninitialized_default_construct_a.md)
- [`destroy_a`](destroy_a.md) —— 异常路径上调用的清理函数
- [`std::uninitialized_fill`](https://zh.cppreference.com/w/cpp/memory/uninitialized_fill) / [`std::uninitialized_fill_n`](https://zh.cppreference.com/w/cpp/memory/uninitialized_fill_n)

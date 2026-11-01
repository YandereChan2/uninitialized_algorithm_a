# `Yc::uninitialized_default_construct_a` / `Yc::uninitialized_default_construct_n_a`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class NoThrowForwardIt, class Alloc>
void uninitialized_default_construct_a(NoThrowForwardIt first,
                                       NoThrowForwardIt last, Alloc& a);   //(1)

template<class NoThrowForwardIt, class Size, class Alloc>
NoThrowForwardIt
    uninitialized_default_construct_n_a(NoThrowForwardIt first,
                                        Size count, Alloc& a);            //(2)
```

在一段未初始化的存储上**默认构造**元素，构造动作经由分配器分派。

(1) 对 `[first, last)` 中的每个位置执行

```C++
std::allocator_traits<Alloc>::construct(a, std::addressof(*current));
```

（即不带构造实参）。

(2) 同 (1)，但只处理 `count` 个位置，返回越过最后一个被构造元素的迭代器。

## 是值初始化，不是默认初始化

这是本设施与 [`std::uninitialized_default_construct`](https://zh.cppreference.com/w/cpp/memory/uninitialized_default_construct) 的一个**有意差异**，也是使用时最容易踩坑的地方：

- `std::uninitialized_default_construct` 的效果等价于 `::new (p) T`——**默认初始化**，对 `int` 这类平凡类型**不写内存**；
- 本设施经由 `allocator_traits::construct(a, p)`，其内建分支是 `::new (p) T()`——**值初始化**，对 `int` 会写 0。

因此本设施的行为与"用分配器构造元素"的容器语义一致（容器 `resize` 出来的 `int` 是 0），比标准算法更安全。若分配器定制了无参的 `construct`，则完全以分配器的行为为准。

## 异常安全

构造途中抛出异常时，已构造好的那一段 `[first, current)` 会被全部销毁，随后异常继续向外传播；抛出点对应的元素没有构造完成，不会被销毁。

## 参数

|||
|-:|:-|
|`first, last`|目标元素区间，其指向的存储必须**未构造**|
|`count`|要构造的元素个数|
|`a`|分配器，以引用传入|

## 前置条件

- `[first, last)`（或 `first` 起的 `count` 个位置）必须是**未构造**的、足够大的存储。
- 元素类型必须能无参默认构造（可被 `std::construct_at(p)` 构造）。
- 对 `(2)`，`count` 必须非负；负数属于未定义行为。

## 返回值

- `(1)` 无。
- `(2)` 越过最后一个被构造元素的迭代器，即 `first + count`。

## 异常

元素默认构造函数（或分配器定制的无参 `construct`）可以抛出任意异常；抛出时销毁已构造部分并原样传播。

## 复杂度

- `(1)` 恰好 `last - first` 次构造。
- `(2)` 恰好 `count` 次构造。

不发生分配。

## 备注

- 分配器未定制无参 `construct` 时（见 [`allocator_has_construct_v`](allocator_has_construct.md)），实现直接执行 `std::construct_at(p)`，其效果正是 `::new (void*p) T()`。
- 对具有用户提供的默认构造函数的类类型，值初始化与默认初始化没有区别；差异只出现在平凡类型和聚合类型上。
- 分配器以引用传入；循环边界一律使用不等号（`first != last`、`count != 0` 的倒计数形式）。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>
#include <string>

int main()
{
    std::allocator<int> ia;
    int* ip = std::allocator_traits<decltype(ia)>::allocate(ia, 3);

    for (int* p = ip; p != ip + 3; ++p)
    {
        *p = 0x7f7f7f7f;                    // 先把存储写成垃圾值
    }
    int* ie = Yc::uninitialized_default_construct_n_a(ip, 3, ia);
    std::printf("int: %d %d %d\n", ip[0], ip[1], ip[2]);   // 值初始化 → 全 0
    Yc::destroy_a(ip, ie, ia);
    std::allocator_traits<decltype(ia)>::deallocate(ia, ip, 3);

    std::allocator<std::string> sa;
    std::string* sp = std::allocator_traits<decltype(sa)>::allocate(sa, 2);
    Yc::uninitialized_default_construct_a(sp, sp + 2, sa);
    std::printf("string: \"%s\" \"%s\"\n", sp[0].c_str(), sp[1].c_str());
    Yc::destroy_a(sp, sp + 2, sa);
    std::allocator_traits<decltype(sa)>::deallocate(sa, sp, 2);
}
```

### 输出

```plain text
int: 0 0 0
string: "" ""
```

---

## 另请参阅

- [`uninitialized_fill_a`](uninitialized_fill_a.md) —— 给定初值的版本
- [`uninitialized_copy_a`](uninitialized_copy_a.md) / [`uninitialized_move_a`](uninitialized_move_a.md)
- [`destroy_a`](destroy_a.md) —— 异常路径上调用的清理函数
- [`std::uninitialized_default_construct`](https://zh.cppreference.com/w/cpp/memory/uninitialized_default_construct) / [`std::uninitialized_default_construct_n`](https://zh.cppreference.com/w/cpp/memory/uninitialized_default_construct_n) —— 注意它们是默认初始化
- [值初始化](https://zh.cppreference.com/w/cpp/language/value_initialization)

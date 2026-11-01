# `Yc::uninitialized_move_a` / `Yc::uninitialized_move_n_a`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class InputIt, class NoThrowForwardIt, class Alloc>
NoThrowForwardIt
    uninitialized_move_a(InputIt first, InputIt last,
                         NoThrowForwardIt d_first, Alloc& a);        //(1)

template<class InputIt, class Size, class NoThrowForwardIt, class Alloc>
std::pair<InputIt, NoThrowForwardIt>
    uninitialized_move_n_a(InputIt first, Size count,
                           NoThrowForwardIt d_first, Alloc& a);      //(2)
```

把源区间里的元素**移动构造**到未初始化的存储上，构造动作经由分配器分派。

(1) 对 `[first, last)` 中的每个元素就地构造目标对象，返回越过最后一个被构造元素的目标迭代器。
(2) 同 (1)，但源与目标都只前进 `count` 次；返回 `std::pair`，`first` 为越过最后一个被搬移源元素的源迭代器，`second` 为越过末尾的目标迭代器。

源侧的取值规则与 [`std::uninitialized_move`](https://zh.cppreference.com/w/cpp/memory/uninitialized_move) 一致：

- 若 `*first` 是**左值**（普通迭代器），按右值使用：构造实参为 `std::move(*first)`；
- 若 `*first` 本身已经是**右值**（例如 [`std::move_iterator`](https://zh.cppreference.com/w/cpp/iterator/move_iterator) 的解引用），原样传递，不再追加一次 `std::move`；
- 若 `*first` 是**纯右值**（例如 [`std::views::transform`](https://zh.cppreference.com/w/cpp/ranges/transform_view) 的迭代器），则进一步触发[复制消除](#复制消除)。

## 异常安全

与 [`uninitialized_copy_a`](uninitialized_copy_a.md) 相同：构造途中抛出异常时，已构造好的那一段 `[d_first, current)` 会被全部销毁，随后异常继续向外传播。抛出点对应的元素没有构造完成，不会被销毁。

## 参数

|||
|-:|:-|
|`first, last`|源元素区间|
|`count`|要搬移的元素个数|
|`d_first`|目标区间的起始位置，其指向的存储必须**未构造**|
|`a`|分配器，以引用传入|

## 前置条件

- `[d_first, d_first + N)` 必须是**未构造**的、足够大的存储。
- 目标区间不得与源区间重叠。
- 对 `(2)`，`count` 必须非负；负数属于未定义行为。

## 返回值

- `(1)` 越过最后一个被构造元素的目标迭代器。
- `(2)` `{ 越过最后一个被搬移元素的源迭代器, 越过末尾的目标迭代器 }`。

## 异常

目标元素的移动构造函数（或分配器定制的 `construct`）可以抛出任意异常；抛出时销毁已构造部分并原样传播。注意：如果目标元素的移动构造函数不是 `noexcept`，源对象在异常之后处于什么状态取决于该构造函数本身——这与标准设施一致。

## 复杂度

恰好 N 次构造、N 次自增；不发生分配。

## 复制消除

本算法按迭代器而不是按"解引用结果"来构造，因此纯右值来源可以直接就地初始化：

```C++
// 分配器未定制 construct 时，实现直接这么做：
::new (static_cast<void*>(p)) T(*it);     // *it 是纯右值 → 保证复制消除
```

若改写成经 `allocator_traits::construct(a, p, *it)`（或任何"先绑到转发形参"的辅助函数），标准只保证 `*it` 被**移动构造**到目标位置。对于移动构造函数不可用、或带有可观察副作用的类型，差别是实打实的。

启用条件是分配器**没有**定制 `construct`（见 [`allocator_has_construct_v`](allocator_has_construct.md)）：一旦分配器定制了 `construct`，就必须走分配器的版本，复制消除也就不适用。

## 备注

- 源区间在成功后处于"被移动过的有效但未指定状态"，是否清空、是否可继续使用由元素类型自己决定；本算法不会主动重置源对象。
- 分配器以引用传入；循环边界一律使用不等号。
- `(2)` 返回源迭代器，是为了让"按个数搬移一段前缀"可以在同一次调用里同时拿到两端的进度。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <array>
#include <cstdio>
#include <memory>
#include <ranges>
#include <utility>

struct element
{
    int v;
    static inline int copies = 0;
    static inline int moves = 0;

    explicit element(int x) : v(x) {}
    element(const element& o) : v(o.v) { ++copies; }
    element(element&& o) noexcept : v(o.v) { ++moves; }
};

int main()
{
    std::allocator<element> a;
    using traits = std::allocator_traits<decltype(a)>;
    std::array<int, 3> src = { 1, 2, 3 };
    element* dst = traits::allocate(a, 3);

    // (1) 源迭代器返回纯右值：不发生任何拷贝/移动
    auto view = src | std::views::transform([](int x) { return element{ x }; });
    element* end = Yc::uninitialized_move_a(view.begin(), view.end(), dst, a);
    std::printf("纯右值来源: 拷贝=%d 移动=%d 值=%d %d %d\n", element::copies, element::moves,
                dst[0].v, dst[1].v, dst[2].v);
    Yc::destroy_a(dst, end, a);
    traits::deallocate(a, dst, 3);

    // (2) 源是左值：逐元素移动构造，move_n 返回两端的进度
    dst = traits::allocate(a, 3);
    std::array<element, 3> owned = { element{ 4 }, element{ 5 }, element{ 6 } };
    element::copies = 0;
    element::moves = 0;
    auto [s, d] = Yc::uninitialized_move_n_a(owned.begin(), 3, dst, a);
    std::printf("左值来源: 拷贝=%d 移动=%d 源前进=%td 目标前进=%td\n", element::copies, element::moves,
                s - owned.begin(), d - dst);
    Yc::destroy_a(dst, d, a);
    traits::deallocate(a, dst, 3);
}
```

### 输出

```plain text
纯右值来源: 拷贝=0 移动=0 值=1 2 3
左值来源: 拷贝=0 移动=3 源前进=3 目标前进=3
```

---

## 另请参阅

- [`uninitialized_copy_a`](uninitialized_copy_a.md) —— 同一套构造语义，源按左值使用
- [`uninitialized_fill_a`](uninitialized_fill_a.md) / [`uninitialized_default_construct_a`](uninitialized_default_construct_a.md)
- [`destroy_a`](destroy_a.md) —— 异常路径上调用的清理函数
- [`allocator_has_construct`](allocator_has_construct.md) —— 决定是否启用复制消除快路径
- [`std::uninitialized_move`](https://zh.cppreference.com/w/cpp/memory/uninitialized_move) / [`std::uninitialized_move_n`](https://zh.cppreference.com/w/cpp/memory/uninitialized_move_n)
- [复制消除](https://zh.cppreference.com/w/cpp/language/copy_elision)

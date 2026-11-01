# `Yc::destroy_a` / `Yc::destroy_n_a`

在文件 `"uninitialized_algorithm_a.h"` 定义

```C++
template<class ForwardIt, class Alloc>
void destroy_a(ForwardIt first, ForwardIt last, Alloc& a);          //(1)

template<class ForwardIt, class Size, class Alloc>
ForwardIt destroy_n_a(ForwardIt first, Size n, Alloc& a);           //(2)
```

销毁一段**已经构造好**的元素区间，并把分配器传下去。

(1) 对 `[first, last)` 中的每个元素 `e` 执行 `std::allocator_traits<Alloc>::destroy(a, std::addressof(e))`。
(2) 对 `first` 起的 `n` 个元素做同样的事，返回越过最后一个被销毁元素的迭代器。

这两者就是 [`std::destroy`](https://zh.cppreference.com/w/cpp/memory/destroy) / [`std::destroy_n`](https://zh.cppreference.com/w/cpp/memory/destroy_n) 的分配器版本：差别仅在于销毁动作经由 `allocator_traits` 分派，因此分配器定制的 `destroy`（例如先记 cookie、或做对象池回收）会被正确调用。

当分配器**没有**定制 `destroy` 时（见 [`allocator_has_destroy_v`](allocator_has_construct.md)），实现直接调用 [`std::destroy_at`](https://zh.cppreference.com/w/cpp/memory/destroy_at)，即 `p->~T()`，省掉一层分派。两者的可观察行为完全相同——`allocator_traits` 在这一分支上本来就是这么做的。

## 参数

|||
|-:|:-|
|`first, last`|要销毁的元素区间|
|`n`|要销毁的元素个数|
|`a`|分配器，以引用传入|

## 前置条件

- `[first, last)`（或 `first` 起的 `n` 个元素）中的每个元素**均已构造**；对未构造的存储调用 `destroy` 是未定义行为。
- 对 `(2)`，`n` 必须非负（与标准库的 `*_n` 算法一致）。负数不会被视为空区间，而是未定义行为。

## 返回值

- `(1)` 无。
- `(2)` 越过最后一个被销毁元素的迭代器，即 `first + n`。

## 异常

销毁动作按分配器要求不应抛出异常（`destroy` / 析构函数应满足 `noexcept`）。若某个析构函数真的抛出，异常会直接从本算法传播出去，剩余元素不再销毁。

## 复杂度

- `(1)` 恰好 `last - first` 次销毁。
- `(2)` 恰好 `n` 次销毁。

## 备注

- `ForwardIt` 与前缀 `NoThrowForwardIt` 的命名沿用标准库的表述：算法只做自增与解引用，不要求随机访问；`destroy_n_a` 的 `Size` 可以是任意整数类型。
- 循环边界一律使用不等号（`first != last`、`n != 0` 的倒计数形式），这是本库所有算法的统一风格。
- 分配器以**引用**传入，因此有状态分配器的状态在调用后仍然保留；算法本身不拷贝分配器。

## 使用示例

```C++
#include "uninitialized_algorithm_a.h"
#include <cstdio>
#include <memory>

struct noisy
{
    int id;
    explicit noisy(int i) : id(i) {}
    ~noisy() { std::printf("destroy %d\n", id); }
};

int main()
{
    std::allocator<noisy> a;
    using traits = std::allocator_traits<decltype(a)>;

    noisy* p = traits::allocate(a, 3);
    for (int i = 0; i != 3; ++i)
    {
        std::construct_at(p + i, i);
    }

    Yc::destroy_a(p, p + 2, a);                 // 按区间销毁前两个
    noisy* e = Yc::destroy_n_a(p + 2, 1, a);    // 按个数销毁剩下的一个
    std::printf("destroy_n_a 停在 p + %td\n", e - p);

    traits::deallocate(a, p, 3);
}
```

### 输出

```plain text
destroy 0
destroy 1
destroy 2
destroy_n_a 停在 p + 3
```

---

## 另请参阅

- [`uninitialized_copy_a`](uninitialized_copy_a.md) / [`uninitialized_move_a`](uninitialized_move_a.md) / [`uninitialized_fill_a`](uninitialized_fill_a.md) / [`uninitialized_default_construct_a`](uninitialized_default_construct_a.md) —— 负责构造的另一半，异常安全依赖本组函数
- [`allocator_has_destroy`](allocator_has_construct.md) —— 判定分配器是否定制了 `destroy`
- [`std::destroy`](https://zh.cppreference.com/w/cpp/memory/destroy) / [`std::destroy_n`](https://zh.cppreference.com/w/cpp/memory/destroy_n)

#pragma once

// ---------------------------------------------------------------------------
// Qt 5.15 + 新版 MSVC 兼容补丁
//
// Qt 5.15 的 qcompilerdetection.h 中：
//     #define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) \
//         stdext::make_checked_array_iterator(x, size_t(N))
//
// 而 Visual Studio 2022 较新的 MSVC STL 已经删除了 namespace stdext 下的
// checked_array_iterator 系列（VS2019 / v142 工具集仍然保留）。
// 于是编译 QVector/QList 的 operator==、QVarLengthArray 时会出现：
//     error C3861: “stdext”: 找不到标识符
//
// 本文件只在 MSVC 且 STL 不再提供 stdext 时被 CMake 强制包含（/FI），
// 提供等价的“裸指针”实现。Qt 的用法都是 std::equal/std::copy 的输入/输出
// 迭代器，裸指针完全满足语义，只是少了调试期边界检查。
//
// 注意：Windows 7 发布版仍建议使用 v142（MSVC 14.29）工具集编译。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <iterator>

namespace stdext {

template <typename T>
inline T* make_checked_array_iterator(T* pointer, std::size_t /*size*/)
{
    return pointer;
}

template <typename T>
inline T* make_unchecked_array_iterator(T* pointer)
{
    return pointer;
}

} // namespace stdext

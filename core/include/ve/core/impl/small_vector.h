// ----------------------------------------------------------------------------
// ve/core/impl/small_vector.h — SmallVector with inline storage (SBO)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// SmallVectorImpl<T, N>: a vector that stores up to N elements inline (no heap).
// When size exceeds N, it falls back to heap allocation like std::vector.
//
// Design references:
//   - LLVM SmallVector  (SBO + type-aware memcpy optimisation)
//   - Godot LocalVector (simple contiguous vector with typed memory ops)
//   - folly small_vector
//
// Key properties:
//   - For trivially copyable T: memcpy/memmove for bulk operations
//   - For non-trivial T: placement new + explicit destructor calls
//   - Pointer-based iterators (T*) for zero-overhead random access
//   - insert(pos, value) for positional insertion (needed by VE mixin)
//   - Wrapped as ve::SmallVector in base.h with PrivateTContainerBase mixin
// ----------------------------------------------------------------------------

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace ve::impl {

template<typename T, uint32_t N = 1>
class SmallVectorImpl
{
    static_assert(N > 0, "SmallVector inline capacity must be > 0");

public:
    // --- type aliases (STL compatible) ---
    using value_type      = T;
    using size_type       = uint32_t;
    using difference_type = std::ptrdiff_t;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using iterator        = T*;
    using const_iterator  = const T*;

    // --- constructors / destructor ---

    SmallVectorImpl() noexcept
        : _data(_inline_ptr()), _size(0), _capacity(N) {}

    explicit SmallVectorImpl(uint32_t count, const T& value = T())
        : SmallVectorImpl()
    {
        resize(count, value);
    }

    SmallVectorImpl(std::initializer_list<T> init) : SmallVectorImpl()
    {
        reserve(static_cast<uint32_t>(init.size()));
        for (const auto& v : init) push_back(v);
    }

    ~SmallVectorImpl()
    {
        _destroy_range(0, _size);
        _free_heap();
    }

    SmallVectorImpl(const SmallVectorImpl& o) : SmallVectorImpl()
    {
        reserve(o._size);
        _copy_construct(o._data, o._size, _data);
        _size = o._size;
    }

    SmallVectorImpl(SmallVectorImpl&& o) noexcept : SmallVectorImpl()
    {
        _steal_from(std::move(o));
    }

    SmallVectorImpl& operator=(const SmallVectorImpl& o)
    {
        if (this != &o) {
            clear();
            reserve(o._size);
            _copy_construct(o._data, o._size, _data);
            _size = o._size;
        }
        return *this;
    }

    SmallVectorImpl& operator=(SmallVectorImpl&& o) noexcept
    {
        if (this != &o) {
            _destroy_range(0, _size);
            _free_heap();
            _data = _inline_ptr();
            _size = 0;
            _capacity = N;
            _steal_from(std::move(o));
        }
        return *this;
    }

    // --- element access ---

    T* data() noexcept { return _data; }
    const T* data() const noexcept { return _data; }

    T& operator[](uint32_t i) { assert(i < _size); return _data[i]; }
    const T& operator[](uint32_t i) const { assert(i < _size); return _data[i]; }

    T& at(uint32_t i)
    {
        if (i >= _size) throw std::out_of_range("<ve> SmallVector::at out of range");
        return _data[i];
    }
    const T& at(uint32_t i) const
    {
        if (i >= _size) throw std::out_of_range("<ve> SmallVector::at out of range");
        return _data[i];
    }

    T& front() { assert(_size > 0); return _data[0]; }
    const T& front() const { assert(_size > 0); return _data[0]; }

    T& back() { assert(_size > 0); return _data[_size - 1]; }
    const T& back() const { assert(_size > 0); return _data[_size - 1]; }

    // --- size / capacity ---

    uint32_t size() const noexcept { return _size; }
    uint32_t capacity() const noexcept { return _capacity; }
    bool empty() const noexcept { return _size == 0; }

    /// True if currently using inline (stack) storage
    bool is_inline() const noexcept { return _is_inline(); }

    // --- iterators ---

    T* begin() noexcept { return _data; }
    T* end() noexcept { return _data + _size; }
    const T* begin() const noexcept { return _data; }
    const T* end() const noexcept { return _data + _size; }
    const T* cbegin() const noexcept { return _data; }
    const T* cend() const noexcept { return _data + _size; }

    // --- append ---

    void push_back(const T& v)
    {
        _ensure_capacity(_size + 1);
        new (_data + _size) T(v);
        ++_size;
    }

    void push_back(T&& v)
    {
        _ensure_capacity(_size + 1);
        new (_data + _size) T(std::move(v));
        ++_size;
    }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        _ensure_capacity(_size + 1);
        T* p = new (_data + _size) T(std::forward<Args>(args)...);
        ++_size;
        return *p;
    }

    // --- positional insert ---
    // Required by PrivateTContainerBase::insertOne / prepend

    T* insert(T* pos, const T& value)
    {
        const uint32_t index = _index_from_ptr(pos);
        _insert_at(index);
        new (_data + index) T(value);
        return _data + index;
    }

    T* insert(T* pos, T&& value)
    {
        const uint32_t index = _index_from_ptr(pos);
        _insert_at(index);
        new (_data + index) T(std::move(value));
        return _data + index;
    }

    // --- erase ---

    /// Erase element at index, shifting trailing elements left. O(n).
    void erase(uint32_t index)
    {
        assert(index < _size);
        const uint32_t trailing = _size - index - 1;
        if constexpr (std::is_trivially_copyable_v<T>) {
            _data[index].~T();
            if (trailing > 0)
                std::memmove(_data + index, _data + index + 1, trailing * sizeof(T));
        } else {
            _data[index].~T();
            if (trailing > 0) {
                // construct at index from index+1 (slot was destroyed)
                new (_data + index) T(std::move(_data[index + 1]));
                // move-assign the rest (cheaper than destroy+construct)
                for (uint32_t i = index + 1; i < _size - 1; ++i)
                    _data[i] = std::move(_data[i + 1]);
                // destroy the vacated last element
                _data[_size - 1].~T();
            }
        }
        --_size;
    }

    /// Erase element at iterator position
    T* erase(T* pos)
    {
        const uint32_t index = _index_from_ptr(pos);
        erase(index);
        return _data + index;
    }

    /// Erase by swapping with the last element. O(1), breaks insertion order.
    void erase_swap(uint32_t index)
    {
        assert(index < _size);
        --_size;
        if (index != _size) {
            _data[index].~T();
            new (_data + index) T(std::move(_data[_size]));
        }
        _data[_size].~T();
    }

    void pop_back()
    {
        assert(_size > 0);
        --_size;
        _data[_size].~T();
    }

    // --- bulk operations ---

    void clear()
    {
        _destroy_range(0, _size);
        _size = 0;
    }

    void reserve(uint32_t new_cap)
    {
        if (new_cap > _capacity) _grow(new_cap);
    }

    void resize(uint32_t new_size, const T& fill = T())
    {
        if (new_size < _size) {
            _destroy_range(new_size, _size);
        } else if (new_size > _size) {
            reserve(new_size);
            for (uint32_t i = _size; i < new_size; ++i)
                new (_data + i) T(fill);
        }
        _size = new_size;
    }

    void shrink_to_fit()
    {
        if (_is_inline() || _size == _capacity) return;
        if (_size <= N) {
            // move back to inline storage
            T* heap = _data;
            _data = _inline_ptr();
            _move_construct(heap, _size, _data);
            ::operator delete(static_cast<void*>(heap));
            _capacity = N;
        } else {
            // realloc to exact size
            T* new_data = static_cast<T*>(::operator new(_size * sizeof(T)));
            _move_construct(_data, _size, new_data);
            ::operator delete(static_cast<void*>(_data));
            _data = new_data;
            _capacity = _size;
        }
    }

    // --- comparison ---

    bool operator==(const SmallVectorImpl& o) const
    {
        if (_size != o._size) return false;
        for (uint32_t i = 0; i < _size; ++i) {
            if (!(_data[i] == o._data[i])) return false;
        }
        return true;
    }
    bool operator!=(const SmallVectorImpl& o) const { return !(*this == o); }

private:
    // --- inline storage helpers ---

    T* _inline_ptr() noexcept
    { return reinterpret_cast<T*>(&_storage); }

    const T* _inline_ptr() const noexcept
    { return reinterpret_cast<const T*>(&_storage); }

    bool _is_inline() const noexcept
    { return _data == reinterpret_cast<const T*>(&_storage); }

    uint32_t _index_from_ptr(const T* pos) const
    {
        assert(pos >= _data && pos <= _data + _size);
        return static_cast<uint32_t>(pos - _data);
    }

    // --- lifetime helpers ---

    void _destroy_range(uint32_t from, uint32_t to)
    {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (uint32_t i = from; i < to; ++i) _data[i].~T();
        }
    }

    void _free_heap()
    {
        if (!_is_inline()) {
            ::operator delete(static_cast<void*>(_data));
            _data = _inline_ptr();
            _capacity = N;
        }
    }

    static void _copy_construct(const T* src, uint32_t count, T* dst)
    {
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (count > 0) std::memcpy(dst, src, count * sizeof(T));
        } else {
            for (uint32_t i = 0; i < count; ++i)
                new (dst + i) T(src[i]);
        }
    }

    static void _move_construct(T* src, uint32_t count, T* dst)
    {
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (count > 0) std::memcpy(dst, src, count * sizeof(T));
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                new (dst + i) T(std::move(src[i]));
                src[i].~T();
            }
        }
    }

    // --- growth ---

    void _ensure_capacity(uint32_t needed)
    {
        if (needed > _capacity) _grow(needed);
    }

    void _grow(uint32_t min_cap)
    {
        uint32_t new_cap = _capacity;
        while (new_cap < min_cap)
            new_cap = (new_cap < 4) ? 4 : new_cap * 2;

        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));
        _move_construct(_data, _size, new_data);

        if (!_is_inline())
            ::operator delete(static_cast<void*>(_data));

        _data = new_data;
        _capacity = new_cap;
    }

    // --- positional insert helper ---
    // Opens a gap at index, incrementing _size. Caller must construct at _data[index].

    void _insert_at(uint32_t index)
    {
        assert(index <= _size);
        _ensure_capacity(_size + 1);
        // NOTE: _data may have changed after _ensure_capacity

        if (index < _size) {
            if constexpr (std::is_trivially_copyable_v<T>) {
                std::memmove(_data + index + 1, _data + index,
                             (_size - index) * sizeof(T));
            } else {
                // move-construct last element into the new slot
                new (_data + _size) T(std::move(_data[_size - 1]));
                // move-assign backwards through the middle
                for (uint32_t i = _size - 1; i > index; --i)
                    _data[i] = std::move(_data[i - 1]);
                // destroy the element at index (caller will reconstruct)
                _data[index].~T();
            }
        }
        ++_size;
    }

    // --- move steal ---

    void _steal_from(SmallVectorImpl&& o) noexcept
    {
        if (o._is_inline()) {
            // can't steal pointer — move elements into our inline storage
            if constexpr (std::is_trivially_copyable_v<T>) {
                if (o._size > 0) std::memcpy(_data, o._data, o._size * sizeof(T));
            } else {
                for (uint32_t i = 0; i < o._size; ++i) {
                    new (_data + i) T(std::move(o._data[i]));
                    o._data[i].~T();
                }
            }
            _size = o._size;
            o._size = 0;
        } else {
            // steal heap pointer directly
            _data = o._data;
            _size = o._size;
            _capacity = o._capacity;
            o._data = o._inline_ptr();
            o._size = 0;
            o._capacity = N;
        }
    }

    // --- data members ---

    alignas(T) unsigned char _storage[sizeof(T) * N];
    T* _data;
    uint32_t _size;
    uint32_t _capacity;
};

} // namespace ve::impl

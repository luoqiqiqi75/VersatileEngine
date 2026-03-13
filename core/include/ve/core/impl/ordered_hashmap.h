// ----------------------------------------------------------------------------
// ve/core/impl/ordered_hashmap.h — Insertion-ordered HashMap (Robin Hood)
// ----------------------------------------------------------------------------
// Adapted from Godot Engine (https://godotengine.org)
// Original files: core/templates/hash_map.h
//
// Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#pragma once

#include "hashfuncs.h"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <cassert>

#ifndef VE_SWAP
#define VE_SWAP(m_x, m_y) std::swap((m_x), (m_y))
#endif

namespace ve::impl {

/**
 * KeyValue pair for InsertionOrderedHashMap
 */
template <typename K, typename V>
struct KeyValue {
    const K key{};
    V value{};

    KeyValue &operator=(const KeyValue &) = delete;
    KeyValue &operator=(KeyValue &&) = delete;

    constexpr KeyValue(const KeyValue &) = default;
    constexpr KeyValue(KeyValue &&) = default;
    constexpr KeyValue(const K &p_key, const V &p_value) : key(p_key), value(p_value) {}

    constexpr bool operator==(const KeyValue &o) const { return key == o.key && value == o.value; }
    constexpr bool operator!=(const KeyValue &o) const { return key != o.key || value != o.value; }
};

template <typename TKey, typename TValue>
struct HashMapElement {
    HashMapElement *next = nullptr;
    HashMapElement *prev = nullptr;
    KeyValue<TKey, TValue> data;
    HashMapElement() {}
    HashMapElement(const TKey &p_key, const TValue &p_value) : data(p_key, p_value) {}
};

/**
 * A HashMap implementation that uses open addressing with Robin Hood hashing.
 * Robin Hood hashing swaps out entries that have a smaller probing distance
 * than the to-be-inserted entry, that evens out the average probing distance
 * and enables faster lookups. Backward shift deletion is employed to further
 * improve the performance and to avoid infinite loops in rare cases.
 *
 * Keys and values are stored in a double linked list by insertion order. This
 * has a slight performance overhead on lookup, which can be mostly compensated
 * using a paged allocator if required.
 */
template <typename TKey, typename TValue,
        typename Hasher = HashMapHasherDefault,
        typename Comparator = HashMapComparatorDefault<TKey>>
class InsertionOrderedHashMap {
public:
    static constexpr uint32_t MIN_CAPACITY_INDEX = 2;
    static constexpr float MAX_OCCUPANCY = 0.75;
    static constexpr uint32_t EMPTY_HASH = 0;
    using KV = KeyValue<TKey, TValue>;

private:
    HashMapElement<TKey, TValue> **_elements = nullptr;
    uint32_t *_hashes = nullptr;
    HashMapElement<TKey, TValue> *_head_element = nullptr;
    HashMapElement<TKey, TValue> *_tail_element = nullptr;

    uint32_t _capacity_idx = 0;
    uint32_t _size = 0;

    VE_FORCE_INLINE static uint32_t _hash(const TKey &p_key) {
        uint32_t hash = Hasher::hash(p_key);
        if (VE_UNLIKELY(hash == EMPTY_HASH)) {
            hash = EMPTY_HASH + 1;
        }
        return hash;
    }

    VE_FORCE_INLINE static constexpr void _increment_mod(uint32_t &r_idx, const uint32_t p_capacity) {
        r_idx++;
        if (VE_UNLIKELY(r_idx == p_capacity)) {
            r_idx = 0;
        }
    }

    static VE_FORCE_INLINE uint32_t _get_probe_length(const uint32_t p_idx, const uint32_t p_hash, const uint32_t p_capacity, const uint64_t p_capacity_inv) {
        const uint32_t original_idx = fastmod(p_hash, p_capacity_inv, p_capacity);
        const uint32_t distance_idx = p_idx - original_idx + p_capacity;
        return distance_idx >= p_capacity ? distance_idx - p_capacity : distance_idx;
    }

    bool _lookup_idx(const TKey &p_key, uint32_t &r_idx) const {
        return _elements != nullptr && _size > 0 && _lookup_idx_unchecked(p_key, _hash(p_key), r_idx);
    }

    bool _lookup_idx_unchecked(const TKey &p_key, uint32_t p_hash, uint32_t &r_idx) const {
        const uint32_t capacity = hash_table_size_primes[_capacity_idx];
        const uint64_t capacity_inv = hash_table_size_primes_inv[_capacity_idx];
        uint32_t idx = fastmod(p_hash, capacity_inv, capacity);
        uint32_t distance = 0;

        while (true) {
            if (_hashes[idx] == EMPTY_HASH) return false;
            if (distance > _get_probe_length(idx, _hashes[idx], capacity, capacity_inv)) return false;
            if (_hashes[idx] == p_hash && Comparator::compare(_elements[idx]->data.key, p_key)) {
                r_idx = idx;
                return true;
            }
            _increment_mod(idx, capacity);
            distance++;
        }
    }

    void _insert_element(uint32_t p_hash, HashMapElement<TKey, TValue> *p_value) {
        const uint32_t capacity = hash_table_size_primes[_capacity_idx];
        const uint64_t capacity_inv = hash_table_size_primes_inv[_capacity_idx];
        uint32_t hash = p_hash;
        HashMapElement<TKey, TValue> *value = p_value;
        uint32_t distance = 0;
        uint32_t idx = fastmod(hash, capacity_inv, capacity);

        while (true) {
            if (_hashes[idx] == EMPTY_HASH) {
                _elements[idx] = value;
                _hashes[idx] = hash;
                _size++;
                return;
            }
            uint32_t existing_probe_len = _get_probe_length(idx, _hashes[idx], capacity, capacity_inv);
            if (existing_probe_len < distance) {
                VE_SWAP(hash, _hashes[idx]);
                VE_SWAP(value, _elements[idx]);
                distance = existing_probe_len;
            }
            _increment_mod(idx, capacity);
            distance++;
        }
    }

    void _resize_and_rehash(uint32_t p_new_capacity_idx) {
        uint32_t old_capacity = hash_table_size_primes[_capacity_idx];
        _capacity_idx = (p_new_capacity_idx > MIN_CAPACITY_INDEX) ? p_new_capacity_idx : MIN_CAPACITY_INDEX;
        uint32_t capacity = hash_table_size_primes[_capacity_idx];

        HashMapElement<TKey, TValue> **old_elements = _elements;
        uint32_t *old_hashes = _hashes;

        _size = 0;
        _hashes = (uint32_t *)std::calloc(capacity, sizeof(uint32_t));
        _elements = (HashMapElement<TKey, TValue> **)std::malloc(sizeof(HashMapElement<TKey, TValue> *) * capacity);

        if (old_capacity == 0) return;

        for (uint32_t i = 0; i < old_capacity; i++) {
            if (old_hashes[i] == EMPTY_HASH) continue;
            _insert_element(old_hashes[i], old_elements[i]);
        }

        std::free(old_elements);
        std::free(old_hashes);
    }

    VE_FORCE_INLINE HashMapElement<TKey, TValue> *_insert(const TKey &p_key, const TValue &p_value, uint32_t p_hash, bool p_front_insert = false) {
        uint32_t capacity = hash_table_size_primes[_capacity_idx];
        if (VE_UNLIKELY(_elements == nullptr)) {
            _hashes = (uint32_t *)std::calloc(capacity, sizeof(uint32_t));
            _elements = (HashMapElement<TKey, TValue> **)std::malloc(sizeof(HashMapElement<TKey, TValue> *) * capacity);
        }

        if (_size + 1 > MAX_OCCUPANCY * capacity) {
            assert(_capacity_idx + 1 < HASH_TABLE_SIZE_MAX && "Hash table maximum capacity reached");
            _resize_and_rehash(_capacity_idx + 1);
        }

        HashMapElement<TKey, TValue> *elem = new HashMapElement<TKey, TValue>(p_key, p_value);

        if (_tail_element == nullptr) {
            _head_element = elem;
            _tail_element = elem;
        } else if (p_front_insert) {
            _head_element->prev = elem;
            elem->next = _head_element;
            _head_element = elem;
        } else {
            _tail_element->next = elem;
            elem->prev = _tail_element;
            _tail_element = elem;
        }

        _insert_element(p_hash, elem);
        return elem;
    }

    void _clear_data() {
        HashMapElement<TKey, TValue> *current = _tail_element;
        while (current != nullptr) {
            HashMapElement<TKey, TValue> *prev = current->prev;
            delete current;
            current = prev;
        }
    }

public:
    VE_FORCE_INLINE uint32_t get_capacity() const { return hash_table_size_primes[_capacity_idx]; }
    VE_FORCE_INLINE uint32_t size() const { return _size; }

    bool is_empty() const { return _size == 0; }

    void clear() {
        if (_elements == nullptr || _size == 0) return;
        _clear_data();
        std::memset(_hashes, EMPTY_HASH, get_capacity() * sizeof(uint32_t));
        _tail_element = nullptr;
        _head_element = nullptr;
        _size = 0;
    }

    TValue &get(const TKey &p_key) {
        uint32_t idx = 0;
        bool exists = _lookup_idx(p_key, idx);
        assert(exists && "HashMap key not found");
        return _elements[idx]->data.value;
    }

    const TValue &get(const TKey &p_key) const {
        uint32_t idx = 0;
        bool exists = _lookup_idx(p_key, idx);
        assert(exists && "HashMap key not found");
        return _elements[idx]->data.value;
    }

    VE_FORCE_INLINE bool has(const TKey &p_key) const {
        uint32_t _idx = 0;
        return _lookup_idx(p_key, _idx);
    }

    bool erase(const TKey &p_key) {
        uint32_t idx = 0;
        if (!_lookup_idx(p_key, idx)) return false;

        const uint32_t capacity = hash_table_size_primes[_capacity_idx];
        const uint64_t capacity_inv = hash_table_size_primes_inv[_capacity_idx];
        uint32_t next_idx = fastmod((idx + 1), capacity_inv, capacity);
        while (_hashes[next_idx] != EMPTY_HASH && _get_probe_length(next_idx, _hashes[next_idx], capacity, capacity_inv) != 0) {
            VE_SWAP(_hashes[next_idx], _hashes[idx]);
            VE_SWAP(_elements[next_idx], _elements[idx]);
            idx = next_idx;
            _increment_mod(next_idx, capacity);
        }

        _hashes[idx] = EMPTY_HASH;
        if (_head_element == _elements[idx]) _head_element = _elements[idx]->next;
        if (_tail_element == _elements[idx]) _tail_element = _elements[idx]->prev;
        if (_elements[idx]->prev) _elements[idx]->prev->next = _elements[idx]->next;
        if (_elements[idx]->next) _elements[idx]->next->prev = _elements[idx]->prev;
        delete _elements[idx];
        _size--;
        return true;
    }

    void reserve(uint32_t p_new_capacity) {
        uint32_t new_idx = _capacity_idx;
        while (hash_table_size_primes[new_idx] < p_new_capacity) {
            assert(new_idx + 1 < (uint32_t)HASH_TABLE_SIZE_MAX && "Hash table maximum capacity reached");
            new_idx++;
        }
        if (new_idx == _capacity_idx) return;
        if (_elements == nullptr) { _capacity_idx = new_idx; return; }
        _resize_and_rehash(new_idx);
    }

    // --- Iterator API ---

    struct ConstIterator {
        VE_FORCE_INLINE const KeyValue<TKey, TValue> &operator*() const { return E->data; }
        VE_FORCE_INLINE const KeyValue<TKey, TValue> *operator->() const { return &E->data; }
        VE_FORCE_INLINE ConstIterator &operator++() { if (E) E = E->next; return *this; }
        VE_FORCE_INLINE ConstIterator &operator--() { if (E) E = E->prev; return *this; }
        VE_FORCE_INLINE bool operator==(const ConstIterator &b) const { return E == b.E; }
        VE_FORCE_INLINE bool operator!=(const ConstIterator &b) const { return E != b.E; }
        VE_FORCE_INLINE explicit operator bool() const { return E != nullptr; }
        VE_FORCE_INLINE const HashMapElement<TKey, TValue> *element() const { return E; }
        VE_FORCE_INLINE ConstIterator(const HashMapElement<TKey, TValue> *p_E) : E(p_E) {}
        VE_FORCE_INLINE ConstIterator() {}
        VE_FORCE_INLINE ConstIterator(const ConstIterator &p_it) : E(p_it.E) {}
        VE_FORCE_INLINE void operator=(const ConstIterator &p_it) { E = p_it.E; }
    private:
        const HashMapElement<TKey, TValue> *E = nullptr;
    };

    struct Iterator {
        VE_FORCE_INLINE KeyValue<TKey, TValue> &operator*() const { return E->data; }
        VE_FORCE_INLINE KeyValue<TKey, TValue> *operator->() const { return &E->data; }
        VE_FORCE_INLINE Iterator &operator++() { if (E) E = E->next; return *this; }
        VE_FORCE_INLINE Iterator &operator--() { if (E) E = E->prev; return *this; }
        VE_FORCE_INLINE bool operator==(const Iterator &b) const { return E == b.E; }
        VE_FORCE_INLINE bool operator!=(const Iterator &b) const { return E != b.E; }
        VE_FORCE_INLINE explicit operator bool() const { return E != nullptr; }
        VE_FORCE_INLINE HashMapElement<TKey, TValue> *element() const { return E; }
        VE_FORCE_INLINE Iterator(HashMapElement<TKey, TValue> *p_E) : E(p_E) {}
        VE_FORCE_INLINE Iterator() {}
        VE_FORCE_INLINE Iterator(const Iterator &p_it) : E(p_it.E) {}
        VE_FORCE_INLINE void operator=(const Iterator &p_it) { E = p_it.E; }
        operator ConstIterator() const { return ConstIterator(E); }
    private:
        HashMapElement<TKey, TValue> *E = nullptr;
    };

    VE_FORCE_INLINE Iterator begin() { return Iterator(_head_element); }
    VE_FORCE_INLINE Iterator end()   { return Iterator(nullptr); }
    VE_FORCE_INLINE Iterator last()  { return Iterator(_tail_element); }

    VE_FORCE_INLINE Iterator find(const TKey &p_key) {
        uint32_t idx = 0;
        return _lookup_idx(p_key, idx) ? Iterator(_elements[idx]) : end();
    }

    VE_FORCE_INLINE void remove(const Iterator &p_iter) {
        if (p_iter) erase(p_iter->key);
    }

    VE_FORCE_INLINE ConstIterator begin() const { return ConstIterator(_head_element); }
    VE_FORCE_INLINE ConstIterator end()   const { return ConstIterator(nullptr); }
    VE_FORCE_INLINE ConstIterator last()  const { return ConstIterator(_tail_element); }

    VE_FORCE_INLINE ConstIterator find(const TKey &p_key) const {
        uint32_t idx = 0;
        return _lookup_idx(p_key, idx) ? ConstIterator(_elements[idx]) : end();
    }

    // --- Indexing ---

    const TValue &operator[](const TKey &p_key) const {
        uint32_t idx = 0;
        bool exists = _lookup_idx(p_key, idx);
        assert(exists && "HashMap key not found");
        return _elements[idx]->data.value;
    }

    TValue &operator[](const TKey &p_key) {
        const uint32_t hash = _hash(p_key);
        uint32_t idx = 0;
        bool exists = _elements && _size > 0 && _lookup_idx_unchecked(p_key, hash, idx);
        if (!exists) {
            return _insert(p_key, TValue(), hash)->data.value;
        } else {
            return _elements[idx]->data.value;
        }
    }

    // --- Insert ---

    Iterator insert(const TKey &p_key, const TValue &p_value, bool p_front_insert = false) {
        const uint32_t hash = _hash(p_key);
        uint32_t idx = 0;
        bool exists = _elements && _size > 0 && _lookup_idx_unchecked(p_key, hash, idx);
        if (!exists) {
            return Iterator(_insert(p_key, p_value, hash, p_front_insert));
        } else {
            _elements[idx]->data.value = p_value;
            return Iterator(_elements[idx]);
        }
    }

    // --- Constructors ---

    explicit InsertionOrderedHashMap(const InsertionOrderedHashMap &p_other) {
        reserve(hash_table_size_primes[p_other._capacity_idx]);
        if (p_other._size == 0) return;
        for (const KeyValue<TKey, TValue> &E : p_other) {
            insert(E.key, E.value);
        }
    }

    InsertionOrderedHashMap(InsertionOrderedHashMap &&p_other) {
        _elements     = p_other._elements;
        _hashes       = p_other._hashes;
        _head_element = p_other._head_element;
        _tail_element = p_other._tail_element;
        _capacity_idx = p_other._capacity_idx;
        _size         = p_other._size;
        p_other._elements     = nullptr;
        p_other._hashes       = nullptr;
        p_other._head_element = nullptr;
        p_other._tail_element = nullptr;
        p_other._capacity_idx = MIN_CAPACITY_INDEX;
        p_other._size         = 0;
    }

    void operator=(const InsertionOrderedHashMap &p_other) {
        if (this == &p_other) return;
        if (_size != 0) clear();
        reserve(hash_table_size_primes[p_other._capacity_idx]);
        if (p_other._elements == nullptr) return;
        for (const KeyValue<TKey, TValue> &E : p_other) {
            insert(E.key, E.value);
        }
    }

    InsertionOrderedHashMap &operator=(InsertionOrderedHashMap &&p_other) {
        if (this == &p_other) return *this;
        if (_size != 0) clear();
        if (_elements != nullptr) { std::free(_elements); std::free(_hashes); }
        _elements     = p_other._elements;
        _hashes       = p_other._hashes;
        _head_element = p_other._head_element;
        _tail_element = p_other._tail_element;
        _capacity_idx = p_other._capacity_idx;
        _size         = p_other._size;
        p_other._elements     = nullptr;
        p_other._hashes       = nullptr;
        p_other._head_element = nullptr;
        p_other._tail_element = nullptr;
        p_other._capacity_idx = MIN_CAPACITY_INDEX;
        p_other._size         = 0;
        return *this;
    }

    InsertionOrderedHashMap(uint32_t p_initial_capacity) {
        _capacity_idx = 0;
        reserve(p_initial_capacity);
    }

    InsertionOrderedHashMap() { _capacity_idx = MIN_CAPACITY_INDEX; }

    InsertionOrderedHashMap(std::initializer_list<KeyValue<TKey, TValue>> p_init) {
        reserve(static_cast<uint32_t>(p_init.size()));
        for (const KeyValue<TKey, TValue> &E : p_init) {
            insert(E.key, E.value);
        }
    }

    ~InsertionOrderedHashMap() {
        _clear_data();
        if (_elements != nullptr) {
            std::free(_elements);
            std::free(_hashes);
        }
    }
};

} // namespace ve::impl

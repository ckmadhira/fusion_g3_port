/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <ctype.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

// NB: This is a local, pytree FunctionRef and not from the ExecuTorch runtime.
#include <executorch/extension/pytree/function_ref.h>

namespace executorch {
namespace extension {
namespace pytree {

inline void pytree_check(bool must_be_true) {
  if (!must_be_true) {
    throw std::runtime_error("pytree assertion failed");
  }
}

#ifdef _MSC_VER
#define EXECUTORCH_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__)
#define EXECUTORCH_ALWAYS_INLINE inline __attribute__((__always_inline__))
#else
#define EXECUTORCH_ALWAYS_INLINE inline
#endif

enum class Kind : uint8_t { List, Tuple, NamedTuple, Dict, Leaf, Custom, None };

using KeyStr = std::string;
using KeyInt = int32_t;

struct Key {
  enum class Kind : uint8_t { None, Int, Str } kind_;

 private:
  std::variant<std::monostate, KeyInt, KeyStr> repr_;

 public:
  Key() = default;
  /*implicit*/ Key(KeyInt key) : repr_(key) {}
  /*implicit*/ Key(KeyStr key) : repr_(std::move(key)) {}

  Kind kind() const {
    return static_cast<Kind>(repr_.index());
  }

  KeyInt as_int() const {
    return std::get<KeyInt>(repr_);
  }

  operator KeyInt() const {
    return as_int();
  }

  const KeyStr& as_str() const {
    return std::get<KeyStr>(repr_);
  }

  operator const KeyStr&() const {
    return as_str();
  }

  bool operator==(const Key& rhs) const {
    return repr_ == rhs.repr_;
  }

  bool operator!=(const Key& rhs) const {
    return !operator==(rhs);
  }
};

struct Empty {};
template <typename T, typename Aux = Empty>
struct ContainerHandle;

template <typename T, typename Aux = Empty>
struct Container final : public Aux {
  using handle_type = ContainerHandle<T, Aux>;
  using leaf_type = T;

  Kind kind = Kind::None;
  size_t size = 0;
  leaf_type* leaf = nullptr;
  std::unique_ptr<handle_type[]> items;
  std::unique_ptr<Key[]> keys;
  std::string custom_type;
  // internal only field to keep associated to every node meta info
  mutable size_t leaves_num = 0u;

  /*implicit*/ Container(Kind kind, size_t size = 0u)
      : kind(kind),
        size(size),
        items(std::unique_ptr<handle_type[]>(new handle_type[size])) {
    if (kind == Kind::Dict) {
      keys = std::unique_ptr<Key[]>(new Key[size]);
    }
  }
  /*implicit*/ Container(leaf_type* leaf)
      : kind(Kind::Leaf), size(0u), leaf(leaf), leaves_num(1u) {}
  Container(const Container&) = delete;
  Container& operator=(const Container&) = delete;
};

template <typename T, typename Aux>
struct ContainerHandle {
  using container_type = Container<T, Aux>;
  using leaf_type = T;
  std::unique_ptr<container_type> handle;

  ContainerHandle() = default;

  template <typename... Args>
  ContainerHandle(Args... args)
      : handle(std::make_unique<container_type>(std::forward<Args>(args)...)) {}

  /*implicit*/ ContainerHandle(container_type* c) : handle(c) {}

  /*implicit*/ ContainerHandle(std::unique_ptr<container_type> c)
      : handle(std::move(c)) {}

  void set_leaf(leaf_type* leaf) {
    pytree_check(handle->kind == Kind::Leaf);
    handle->leaf = leaf;
  }

  operator leaf_type() const {
    pytree_check(handle->kind == Kind::Leaf);
    return *handle->leaf;
  }

  const leaf_type& leaf() const {
    pytree_check(handle->kind == Kind::Leaf);
    return *handle->leaf;
  }
  leaf_type& leaf() {
    pytree_check(handle->kind == Kind::Leaf);
    return *handle->leaf;
  }

  const leaf_type* leaf_ptr() const {
    pytree_check(handle->kind == Kind::Leaf);
    return handle->leaf;
  }
  leaf_type* leaf_ptr() {
    pytree_check(handle->kind == Kind::Leaf);
    return handle->leaf;
  }

  const ContainerHandle& operator[](size_t idx) const {
    pytree_check(idx < handle->size);
    return handle->items[idx];
  }

  ContainerHandle& operator[](size_t idx) {
    pytree_check(idx < handle->size);
    return handle->items[idx];
  }

  bool contains(const KeyStr& lookup_key) const {
    pytree_check(isDict());
    for (size_t i = 0; i < handle->size; ++i) {
      if (handle->keys[i] == lookup_key) {
        return true;
      }
    }
    return false;
  }

  const ContainerHandle& at(const Key& lookup_key) const {
    pytree_check(isDict());
    for (size_t i = 0; i < handle->size; ++i) {
      if (handle->keys[i] == lookup_key) {
        return handle->items[i];
      }
    }
    throw std::runtime_error("Dict::at lookup failed");
  }

  const ContainerHandle& at(const KeyInt& lookup_key) const {
    return at(Key(lookup_key));
  }

  const ContainerHandle& at(const KeyStr& lookup_key) const {
    return at(Key(lookup_key));
  }

  const Key& key(size_t idx) const {
    pytree_check(isDict());
    return handle->keys[idx];
  }
  Key& key(size_t idx) {
    pytree_check(isDict());
    return handle->keys[idx];
  }

  size_t size() const {
    return handle->size;
  }

  size_t leaves_num() const {
    return handle->leaves_num;
  }

  bool isDict() const {
    return handle->kind == Kind::Dict;
  }

  bool isList() const {
    return handle->kind == Kind::List;
  }

  bool isNamedTuple() const {
    return handle->kind == Kind::NamedTuple;
  }

  bool isTuple() const {
    return handle->kind == Kind::Tuple;
  }

  bool isLeaf() const {
    return handle->kind == Kind::Leaf;
  }

  Kind kind() const {
    return handle->kind;
  }

  // Checks only structure, no leaves comparison
  bool operator==(const ContainerHandle& rhs) {
    const Kind knd = kind();
    if (knd != rhs.kind()) {
      return false;
    }
    if (knd == Kind::Leaf) {
      return true;
    }
    const size_t _size = size();
    if (_size != rhs.size()) {
      return false;
    }

    for (size_t i = 0; i < _size; ++i) {
      if (knd == Kind::Dict && (key(i) != rhs.key(i))) {
        return false;
      }
      if (operator[](i) != rhs[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const ContainerHandle& rhs) {
    return !operator==(rhs);
  }
};

struct TreeSpecLeaf {};

template <typename Aux>
using TreeSpec = ContainerHandle<TreeSpecLeaf, Aux>;
template <typename Aux>
using TreeSpecContainer = Container<TreeSpecLeaf, Aux>;

using StrTreeSpec = std::string;

// Expects refresh_leaves_num() was called after the last modification
template <typename T, typename U, typename Aux>
ContainerHandle<U, Aux> clone(const ContainerHandle<T, Aux>& node, U* leaves) {
  if (node.isLeaf()) {
    return ContainerHandle<U, Aux>(leaves);
  }

  ContainerHandle<U, Aux> ret(node.kind(), node.size());
  size_t leaves_offset = 0;
  size_t size = node.size();
  for (size_t i = 0; i < size; ++i) {
    ret[i] = clone(node[i], leaves + leaves_offset);
    leaves_offset += node[i].leaves_num();
  }

  if (node.isDict()) {
    ret.handle->keys = std::unique_ptr<Key[]>(new Key[size]);
    for (size_t i = 0; i < size; ++i) {
      ret.handle->keys[i] = node.handle->keys[i];
    }
  }

  return ret;
}

template <typename T, typename Aux>
void traverse(
    ContainerHandle<T, Aux>& node,
    FunctionRef<void(ContainerHandle<T, Aux>&)> func) {
  for (size_t i = 0; i < node.size(); ++i) {
    traverse(node[i], func);
  }

  func(node);
}

template <typename T, typename Aux>
void traverse(
    const ContainerHandle<T, Aux>& node,
    FunctionRef<void(const ContainerHandle<T, Aux>&)> func) {
  for (size_t i = 0; i < node.size(); ++i) {
    traverse(node[i], func);
  }

  func(node);
}

struct Config final {
  static constexpr char kTuple = 'T';
  static constexpr char kNamedTuple = 'N';
  static constexpr char kList = 'L';
  static constexpr char kDict = 'D';
  static constexpr char kCustom = 'C';
  static constexpr char kLeaf = '$';
  static constexpr char kNodeDataBegin = '(';
  static constexpr char kNodeDataEnd = ')';
  static constexpr char kDictStrKeyQuote = '\'';
  static constexpr char kDictKeyValueSep = ':';
  static constexpr char kChildrenSep = ',';
  static constexpr char kChildrenDataSep = '#';
};

template <typename Aux>
StrTreeSpec to_str_internal(const TreeSpec<Aux>& spec) {
  std::string s;
  switch (spec.kind()) {
    case Kind::List:
      s.push_back(Config::kList);
      break;
    case Kind::NamedTuple:
      s.push_back(Config::kNamedTuple);
      break;
    case Kind::Tuple:
      s.push_back(Config::kTuple);
      break;
    case Kind::Dict:
      s.push_back(Config::kDict);
      break;
    case Kind::Leaf:
      s.push_back(Config::kLeaf);
      return s;
    case Kind::Custom:
      s.push_back(Config::kCustom);
      s.push_back('(');
      s.append(spec.handle->custom_type);
      s.push_back(')');
      break;
    case Kind::None:
      return s;
  }
  const size_t size = spec.size();
  s.append(std::to_string(size));
  for (size_t i = 0; i < size; ++i) {
    s.push_back(Config::kChildrenDataSep);
    s.append(std::to_string(spec[i].leaves_num()));
  }
  s.push_back(Config::kNodeDataBegin);
  if (spec.kind() == Kind::Dict) {
    for (size_t i = 0; i < size; ++i) {
      if (i) {
        s.push_back(Config::kChildrenSep);
      }
      const auto& key = spec.key(i);
      if (key.kind() == Key::Kind::Int) {
        s.append(std::to_string(key.as_int()));
      } else if (key.kind() == Key::Kind::Str) {
        s.push_back(Config::kDictStrKeyQuote);
        s.append(key.as_str());
        s.push_back(Config::kDictStrKeyQuote);
      } else {
        throw std::runtime_error(
            "invalid key in pytree dict; must be int or string");
      }
      s.push_back(Config::kDictKeyValueSep);
      s.append(to_str_internal(spec[i]));
    }
  } else {
    for (size_t i = 0; i < size; ++i) {
      if (i) {
        s.push_back(Config::kChildrenSep);
      }
      s.append(to_str_internal(spec[i]));
    }
  }
  s.push_back(Config::kNodeDataEnd);
  return s;
}

template <typename T>
struct arr {
  explicit arr(const size_t n) : data_(std::unique_ptr<T[]>(new T[n])), n_(n) {}

  T& operator[](size_t idx) {
    return data_[idx];
  }

  const T& operator[](size_t idx) const {
    return data_[idx];
  }

  T& at(size_t idx) {
    if (idx >= size()) {
      throw std::out_of_range(
          "bounds check failed in pytree arr at index " + std::to_string(idx));
    }
    return data_[idx];
  }

  const T& at(size_t idx) const {
    if (idx >= size()) {
      throw std::out_of_range(
          "bounds check failed in pytree arr at index " + std::to_string(idx));
    }
    return data_[idx];
  }

  inline T* data() {
    return data_.get();
  }

  T* begin() {
    return data_.get();
  }

  T* end() {
    return begin() + size();
  }

  const T* begin() const {
    return data_.get();
  }

  const T* end() const {
    return begin() + size();
  }

  inline size_t size() const {
    return n_;
  }

 private:
  std::unique_ptr<T[]> data_;
  size_t n_;
};

inline size_t read_number(const StrTreeSpec& spec, size_t& read_idx) {
  size_t num = 0;
  if (!isdigit(spec.at(read_idx))) {
    throw std::runtime_error(
        std::string("expected a digit while decoding pytree, not ") +
        spec[read_idx]);
  }
  while (isdigit(spec.at(read_idx))) {
    num = 10 * num + (spec[read_idx] - '0');
    read_idx++;
  }
  return num;
}

inline arr<size_t> read_node_layout(const StrTreeSpec& spec, size_t& read_idx) {
  const size_t child_num = read_number(spec, read_idx);
  arr<size_t> ret(child_num);

  size_t child_idx = 0;
  while (spec.at(read_idx) == Config::kChildrenDataSep) {
    ++read_idx;
    ret.at(child_idx++) = read_number(spec, read_idx);
  }
  return ret;
}

// spec_data comes from pre_parse, which guarantees 1)
// spec_data.size() == spec.size() and 2) contents of spec_data are
// in-bounds indices for spec, so we omit bounds checks for spec_data.
template <typename Aux>
TreeSpec<Aux> from_str_internal(
    const StrTreeSpec& spec,
    size_t read_idx,
    const arr<size_t>& spec_data) {
  const auto kind_char = spec.at(read_idx);
  switch (kind_char) {
    case Config::kTuple:
    case Config::kNamedTuple:
    case Config::kList: {
      Kind kind = Kind::List;
      std::string custom_type;
      if (Config::kNamedTuple == kind_char) {
        kind = Kind::NamedTuple;
      } else if (Config::kTuple == kind_char) {
        kind = Kind::Tuple;
      } else if (Config::kCustom == kind_char) {
        kind = Kind::Custom;
        read_idx++;
        assert(spec.at(read_idx) == '(');
        auto type_str_end = spec_data[read_idx];
        read_idx++;
        custom_type = spec.substr(read_idx, type_str_end - read_idx);
        assert(false);
      }
      read_idx++;
      auto layout = read_node_layout(spec, read_idx);
      const auto size = layout.size();
      auto c = std::make_unique<TreeSpecContainer<Aux>>(kind, size);

      if (Kind::Custom == kind) {
        c->custom_type = std::move(custom_type);
      }

      size_t child_idx = 0;
      size_t leaves_offset = 0;

      if (size > 0) {
        while (spec.at(read_idx) != Config::kNodeDataEnd) {
          // NOLINTNEXTLINE
          auto next_delim_idx = spec_data[read_idx];
          read_idx++;
          if (child_idx >= size) {
            throw std::out_of_range(
                "bounds check failed writing to pytree item at index " +
                std::to_string(child_idx));
          }
          c->items[child_idx] =
              from_str_internal<Aux>(spec, read_idx, spec_data);
          read_idx = next_delim_idx;
          leaves_offset += layout[child_idx++];
        }
      } else {
        read_idx++;
      }
      c->leaves_num = leaves_offset;
      return TreeSpec<Aux>(std::move(c));
    }

    case Config::kDict: {
      read_idx++;
      auto layout = read_node_layout(spec, read_idx);
      const auto size = layout.size();
      auto c = std::make_unique<TreeSpecContainer<Aux>>(Kind::Dict, size);

      size_t child_idx = 0;
      size_t leaves_offset = 0;

      if (size > 0) {
        while (spec.at(read_idx) != Config::kNodeDataEnd) {
          // NOLINTNEXTLINE
          auto next_delim_idx = spec_data[read_idx];
          read_idx++;
          if (child_idx >= size) {
            throw std::out_of_range(
                "bounds check failed decoding pytree dict at index " +
                std::to_string(child_idx));
          }
          if (spec.at(read_idx) == Config::kDictStrKeyQuote) {
            auto key_delim_idx = spec_data[read_idx];
            read_idx++;
            const size_t key_len = key_delim_idx - read_idx;
            // NOLINTNEXTLINE
            c->keys[child_idx] = spec.substr(read_idx, key_len);
            read_idx = key_delim_idx + 2;
          } else {
            size_t key = read_number(spec, read_idx);
            c->keys[child_idx] = KeyInt(key);
            read_idx += 1;
          }

          c->items[child_idx] =
              from_str_internal<Aux>(spec, read_idx, spec_data);
          read_idx = next_delim_idx;
          leaves_offset += layout.at(child_idx++);
        }
      } else {
        read_idx++;
      }
      c->leaves_num = leaves_offset;
      return TreeSpec<Aux>(std::move(c));
    }

    case Config::kLeaf:
      return new TreeSpecContainer<Aux>(nullptr);
  }
  return new TreeSpecContainer<Aux>(Kind::None);
}

template <typename T>
struct stack final {
  constexpr static const size_t SIZE = 8;

  size_t size_ = 0;
  T data[SIZE];

  void push(T&& item) {
    pytree_check(size_ < SIZE);
    data[size_++] = std::move(item);
  }

  T pop() {
    pytree_check(size_ > 0);
    return data[--size_];
  }

  T& top() {
    pytree_check(size_ > 0);
    return data[size_ - 1];
  }

  size_t size() {
    return size_;
  }
};

// We guarantee indicies in the result are in bounds.
inline arr<size_t> pre_parse(const StrTreeSpec& spec) {
  // Invariant: indices in stack are in bounds.
  stack<std::pair<size_t, size_t>> stack;
  size_t i = 0;
  const size_t size = spec.size();
  arr<size_t> ret(size);
  while (i < size) {
    const auto c = spec[i];
    switch (c) {
      case Config::kNodeDataBegin: {
        stack.push({i, i});
        break;
      }
      case Config::kNodeDataEnd: {
        auto& item = stack.top();
        size_t last_sep_idx = item.second;
        ret[last_sep_idx] = i;
        stack.pop();
        break;
      }
      case Config::kDictStrKeyQuote: {
        size_t idx = i;
        i++;
        while (spec.at(i) != Config::kDictStrKeyQuote) {
          i++;
        }
        if (i >= size) {
          throw std::out_of_range(
              "bounds check failed while parsing dictionary key at index " +
              std::to_string(i));
        }
        ret.at(idx) = i;
        ret.at(i) = idx;
        break;
      }
      case Config::kChildrenSep: {
        auto& item = stack.top();
        size_t last_sep_idx = item.second;
        ret[last_sep_idx] = i;
        item.second = i;
        break;
      }
    }
    i++;
  }
  return ret;
}

template <typename Aux = Empty>
TreeSpec<Aux> from_str(const StrTreeSpec& spec) {
  return from_str_internal<Aux>(spec, 0u, pre_parse(spec));
}

template <typename Aux>
StrTreeSpec to_str(const TreeSpec<Aux>& spec) {
  if (spec.leaves_num() == 0) {
    refresh_leaves_num(spec);
  }
  return to_str_internal(spec);
}

template <typename Aux>
StrTreeSpec to_str(const TreeSpec<Aux>& spec);

template <typename T, typename Aux>
ContainerHandle<T, Aux> unflatten(const TreeSpec<Aux>& spec, T* leaves) {
  if (spec.leaves_num() == 0) {
    refresh_leaves_num(spec);
  }
  return clone(spec, leaves);
}

template <typename T, typename Aux = Empty>
ContainerHandle<T, Aux> unflatten(const StrTreeSpec& spec, T* leaves) {
  return unflatten(from_str<Aux>(spec), leaves);
}

template <typename T, typename Aux>
void flatten_internal(const ContainerHandle<T, Aux>& tree, const T** leaves) {
  using tree_t = decltype(tree);
  size_t leaves_idx = 0;
  auto func = [&](tree_t node) {
    if (node.isLeaf()) {
      leaves[leaves_idx++] = node.leaf_ptr();
    }
  };
  traverse(tree, FunctionRef<void(tree_t&)>{func});
}

template <typename T, typename Aux>
void flatten_internal(ContainerHandle<T, Aux>& tree, T** leaves) {
  using tree_t = decltype(tree);
  size_t leaves_idx = 0;
  auto func = [&](tree_t node) {
    if (node.isLeaf()) {
      leaves[leaves_idx++] = node.leaf_ptr();
    }
  };
  traverse(tree, FunctionRef<void(tree_t&)>{func});
}

template <typename T, typename Aux>
size_t refresh_leaves_num(const ContainerHandle<T, Aux>& node) {
  if (node.isLeaf()) {
    node.handle->leaves_num = 1;
    return 1;
  }

  size_t n = 0;
  for (size_t i = 0; i < node.size(); ++i) {
    n += refresh_leaves_num(node[i]);
  }

  node.handle->leaves_num = n;
  return n;
}

template <typename T, typename Aux>
std::pair<arr<const T*>, std::unique_ptr<TreeSpec<Aux>>> flatten(
    const ContainerHandle<T, Aux>& tree) {
  refresh_leaves_num(tree);
  const size_t n = tree.leaves_num();
  arr<T*> leaves(n);
  flatten_internal(tree, leaves.data());
  auto spec_leaves = std::make_unique<TreeSpecLeaf[]>(n);
  return {
      std::move(leaves),
      std::make_unique<TreeSpec<Aux>>(clone(tree, spec_leaves.get()))};
}

// Duplication of logic for non const ContainerHandle
template <typename T, typename Aux>
std::pair<arr<T*>, std::unique_ptr<TreeSpec<Aux>>> flatten(
    ContainerHandle<T, Aux>& tree) {
  refresh_leaves_num(tree);
  const size_t n = tree.leaves_num();
  arr<T*> leaves(n);
  flatten_internal(tree, leaves.data());
  auto spec_leaves = std::make_unique<TreeSpecLeaf[]>(n);
  return {
      std::move(leaves),
      std::make_unique<TreeSpec<Aux>>(clone(tree, spec_leaves.get()))};
}

} // namespace pytree
} // namespace extension
} // namespace executorch

namespace torch {
namespace executor {
namespace pytree {
// TODO(T197294990): Remove these deprecated aliases once all users have moved
// to the new `::executorch` namespaces.
using ::executorch::extension::pytree::Empty;
using ::executorch::extension::pytree::from_str;
using ::executorch::extension::pytree::TreeSpec;
} // namespace pytree
} // namespace executor
} // namespace torch

// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "registry.hpp"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if __has_include(<malloc.h>)
#include <malloc.h>
#endif

#include <M5Unified.h>


namespace kanplay_ns {

//-------------------------------------------------------------------------

#if __has_include (<freertos/freertos.h>)
void registry_base_t::setNotifyTaskHandle(TaskHandle_t handle)
{
  if (_task_handle != nullptr) {
    M5_LOGE("task handle already set");
    return;
  }
  _task_handle = handle;
}
#endif

static void* alloc_sram_anti_fragment(size_t size)
{
  void* result = nullptr;
#if !defined (M5UNIFIED_PC_BUILD)
  // メモリブロックの断片化への対策として、最大領域を先回りして確保しておく。(これにより小さい断片化領域から使用させることができる)
  auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  // 上記の処理により、以下のメモリ確保は二番目に小さい領域から確保されることになる。
  result = m5gfx::heap_alloc_dma(size);
  // 先回りして確保しておいた領域を解放する。
  m5gfx::heap_free(dummy);
#endif
  if (result == nullptr)
  {
    result = m5gfx::heap_alloc_dma(size);
  }
  return result;
}

registry_base_t::registry_base_t(uint16_t history_count)
: _history_code { 0 }
, _history_count(history_count)
{
  _history = nullptr;
}

registry_base_t::~registry_base_t(void)
{ if (_history != nullptr) { m5gfx::heap_free(_history); } }

void registry_base_t::init(bool psram)
{
  if (_history_count) {
    size_t history_size = _history_count * sizeof(history_t);
    void* ptr = nullptr;
    // if (psram) {
      ptr = m5gfx::heap_alloc_psram(history_size);
    // }
    if (ptr == nullptr) {
      ptr = alloc_sram_anti_fragment(history_size);
    }
    memset(ptr, 0xFF, history_size);
    _history = (history_t*)ptr;
  }
}

void registry_base_t::_addHistory(uint16_t index, uint32_t value, data_size_t data_size)
{
  uint16_t history_index = _history_code & 0xFFFF;
  uint8_t history_uid = _history_code >> 16;
  if (_history != nullptr) {
    _history[history_index].value = value;
    _history[history_index].index = index;
    _history[history_index].data_size = data_size;
    _history[history_index].uid = history_uid;
  }
  if (++history_index >= _history_count)
  {
    history_index = 0;
    history_uid++;
  }
  _history_code = history_index | history_uid << 16;
}


// 変更履歴を取得する
const registry_base_t::history_t* registry_base_t::getHistory(history_code_t &code)
{
  if (_history_code == code || _history == nullptr) {
    return nullptr;
  }
  auto index = code & 0xFFFF;
  if (index >= _history_count) {
    M5_LOGE("history index out of range : %d", index);
    return nullptr;
  }
  uint8_t uid = code >> 16;
  auto res = &_history[index];
  if (uid != res->uid) {
    M5_LOGW("history uid looping : request:%08x  uid:%d  data uid:%d", code, uid, _history[index].uid);
    do
    {
      if (++index >= _history_count) {
        index = 0;
        ++uid;
      }
      res = &_history[index];
    } while (uid != res->uid);
  }
  if (++index >= _history_count) {
    index = 0;
    ++uid;
  }
  code = index | (uid << 16);
  return res;
}


void registry_t::assign(const registry_t &src) {
  memcpy(_reg_data, src._reg_data, _registry_size);
  if (_history_count == 0) {
    _history_code += 1 << 16;
  }
  _execNotify();
}

registry_t::registry_t(uint16_t registry_size, uint16_t history_count, data_size_t data_size)
: registry_base_t(history_count)
{
  _registry_size = registry_size;
  _data_size = data_size;
  // _reg_data = malloc(registry_size);
  // memset(_reg_data, 0, registry_size);
}

registry_t::~registry_t(void)
{ if (_reg_data != nullptr) { m5gfx::heap_free(_reg_data); } }

void registry_t::init(bool psram)
{
  if (_reg_data != nullptr) {
    M5_LOGE("registry_t::init: already initialized");
    return;
  }
  registry_base_t::init(psram);
  if (psram) {
    _reg_data = (uint8_t*)m5gfx::heap_alloc_psram(_registry_size);
  } else {
    _reg_data = (uint8_t*)alloc_sram_anti_fragment(_registry_size);
  }
  if (_reg_data) {
    memset(_reg_data, 0, _registry_size);
  }
}

bool registry_base_t::set8(uint16_t index, uint8_t value, bool force_notify)
{
  _addHistory(index, value, data_size_t::DATA_SIZE_8);
  if (force_notify) { _execNotify(); }
  return force_notify;
}

bool registry_base_t::set16(uint16_t index, uint16_t value, bool force_notify)
{
  _addHistory(index, value, data_size_t::DATA_SIZE_16);
  if (force_notify) { _execNotify(); }
  return force_notify;
}

bool registry_base_t::set32(uint16_t index, uint32_t value, bool force_notify)
{
  _addHistory(index, value, data_size_t::DATA_SIZE_32);
  if (force_notify) { _execNotify(); }
  return force_notify;
}

bool registry_t::set8(uint16_t index, uint8_t value, bool force_notify)
{
  if (index + 1 > _registry_size) {
    M5_LOGE("set8: index out of range : %d", index);
    assert(index < _registry_size && "set8: index out of range");
  }
  auto dst = &_reg_data_8[index];
  if (*dst != value || force_notify) {
    *dst = value;
    switch (_data_size) {
    default: return false;
    case data_size_t::DATA_SIZE_8:
      _addHistory(index, value, data_size_t::DATA_SIZE_8);
      break;
    case data_size_t::DATA_SIZE_16:
      {
        index >>= 1;
        uint16_t v = _reg_data_16[index];
        _addHistory(index << 1, v, data_size_t::DATA_SIZE_16);
      }
      break;
    case data_size_t::DATA_SIZE_32:
      {
        index >>= 2;
        uint32_t v = _reg_data_32[index];
        _addHistory(index << 2, v, data_size_t::DATA_SIZE_32);
      }
      break;
    }
    _execNotify();
    return true;
  }
  return false;
}

bool registry_t::set16(uint16_t index, uint16_t value, bool force_notify)
{
    if (index + 2 > _registry_size) {
        M5_LOGE("set16: index out of range : %d", index);
        return false;
    }
    if (index & 1) {
        M5_LOGE("set16: alignment error : %d", index);
        return false;
    }
    auto dst = &_reg_data_16[index >> 1];
    if (*dst != value || force_notify) {
        *dst = value;
        switch (_data_size) {
        default: return false;
        case data_size_t::DATA_SIZE_16:
            _addHistory(index, value, data_size_t::DATA_SIZE_16);
            break;
        case data_size_t::DATA_SIZE_8:
            {
                uint8_t v = _reg_data_8[index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
                v = _reg_data_8[++index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
            }
            break;
        case data_size_t::DATA_SIZE_32:
            {
                index >>= 2;
                uint32_t v = _reg_data_32[index];
                _addHistory(index << 2, v, data_size_t::DATA_SIZE_32);
            }
        }
        _execNotify();
        return true;
    }
    return false;
}

bool registry_t::set32(uint16_t index, uint32_t value, bool force_notify)
{
    if (index + 4 > _registry_size) {
        M5_LOGE("set32: index out of range : %d", index);
        return false;
    }
    if (index & 3) {
        M5_LOGE("set32: alignment error : %d", index);
        return false;
    }
    auto dst = &_reg_data_32[index >> 2];
    if (*dst != value || force_notify) {
        *dst = value;
        switch (_data_size) {
        default: return false;
        case data_size_t::DATA_SIZE_32:
            _addHistory(index, value, data_size_t::DATA_SIZE_32);
            break;
        case data_size_t::DATA_SIZE_16:
            {
                uint16_t v = _reg_data_16[index >> 1];
                _addHistory(index, v, data_size_t::DATA_SIZE_16);
                index += 2;
                v = _reg_data_16[index >> 1];
                _addHistory(index, v, data_size_t::DATA_SIZE_16);
            }
            break;
        case data_size_t::DATA_SIZE_8:
            {
                uint8_t v = _reg_data_8[index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
                v = _reg_data_8[++index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
                v = _reg_data_8[++index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
                v = _reg_data_8[++index];
                _addHistory(index, v, data_size_t::DATA_SIZE_8);
            }
            break;
        }
        _execNotify();
        return true;
    }
    return false;
}

uint8_t registry_t::get8(uint16_t index) const
{
    if (index + 1 > _registry_size) {
        M5_LOGE("get8: index out of range : %d", index);
        assert(index < _registry_size && "get8: index out of range");
    }
    return _reg_data_8[index];
}

uint16_t registry_t::get16(uint16_t index) const
{
    if (index + 2 > _registry_size) {
        M5_LOGE("get16: index out of range : %d", index);
        return 0;
    }
    if (index & 1) {
        M5_LOGE("get16: alignment error : %d", index);
        return 0;
    }
    return _reg_data_16[index >> 1];
}

uint32_t registry_t::get32(uint16_t index) const
{
    if (index + 4 > _registry_size) {
        M5_LOGE("get32: index out of range : %d", index);
        assert(index + 4 <= _registry_size && "get32: index out of range");
    }
    if (index & 3) {
        M5_LOGE("get32: alignment error : %d", index);
        return 0;
    }
    return _reg_data_32[index >> 2];
}

bool registry_t::operator==(const registry_t &rhs) const
{
    return memcmp(_reg_data, rhs._reg_data, _registry_size) == 0;
}

//-------------------------------------------------------------------------

bool registry_map8_t::set8(uint16_t index, uint8_t value, bool force_notify)
{
  bool no_change = false;
  // 既存の値を探す
  auto it = _data.find(index);
  if (it != _data.end()) {
    if (it->second == value) {
      no_change = true;
    } else {
      // 値が異なる場合は更新
      if (value == _default_value) {
        _data.erase(it);
      } else {
        it->second = value;
      }
    }
  } else {
    if (value == _default_value) {
      no_change = true;
    } else {
      _data[index] = value;
    }
  }

  if (no_change && !force_notify) {
    return false;
  }
  _addHistory(index, value, data_size_t::DATA_SIZE_8);
  _execNotify();
  return true;
/*
  if (value == _default_value) {
    _data.erase(index);
  } else {
    _data[index] = value;
  }
  _addHistory(index, value, data_size_t::DATA_SIZE_8);
  _execNotify();
*/
}

uint8_t registry_map8_t::get8(uint16_t index) const
{
  auto it = _data.find(index);
  if (it == _data.end()) {
    return _default_value;
  }
  return it->second;
}

void registry_map8_t::assign(const registry_map8_t &src)
{
  _data = src._data;
  if (_history_count == 0) {
    _history_code += 1 << 16;
  }
  _execNotify();
}

//-------------------------------------------------------------------------

bool registry_map32_t::set32(uint16_t index, uint32_t value, bool force_notify)
{
  bool no_change = false;
  // 既存の値を探す
  auto it = _data.find(index);
  if (it != _data.end()) {
    if (it->second == value) {
      no_change = true;
    } else {
      // 値が異なる場合は更新
      if (value == _default_value) {
        _data.erase(it);
      } else {
        it->second = value;
      }
    }
  } else {
    if (value == _default_value) {
      no_change = true;
    } else {
      _data[index] = value;
    }
  }

  if (no_change && !force_notify) {
    return false;
  }
  _addHistory(index, value, data_size_t::DATA_SIZE_32);
  _execNotify();
  return true;
/*
  if (value == _default_value) {
    _data.erase(index);
  } else {
    _data[index] = value;
  }
  _addHistory(index, value, data_size_t::DATA_SIZE_32);
  _execNotify();
*/
}

uint32_t registry_map32_t::get32(uint16_t index) const
{
  auto it = _data.find(index);
  if (it == _data.end()) {
    return _default_value;
  }
  return it->second;
}

void registry_map32_t::assign(const registry_map32_t &src)
{
  _data = src._data;
  if (_history_count == 0) {
    _history_code += 1 << 16;
  }
  _execNotify();
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

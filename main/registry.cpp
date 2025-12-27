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

static constexpr const uint32_t crc32_table[] = {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 
  0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
  0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
  0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
  0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
  0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
  0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
  0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B, 0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
  0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
  0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
  0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

uint32_t calc_crc32(const void *data, size_t length, uint32_t crc_init) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = crc_init ^ 0xFFFFFFFFUL; // 初期値
  for (size_t i = 0; i < length; i++) {
    uint8_t index = (uint8_t)(crc ^ bytes[i]);
    crc = (crc >> 8) ^ crc32_table[index];
  }
  return crc ^ 0xFFFFFFFFUL; // 最終 XOR
}

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
    if (psram) {
      ptr = m5gfx::heap_alloc_psram(history_size);
    }
    if (ptr == nullptr) {
      ptr = alloc_sram_anti_fragment(history_size);
      if (ptr == nullptr) {
        M5_LOGE("registry_base_t::init: history memory allocation failed");
        ptr = m5gfx::heap_alloc_psram(history_size);
        if (ptr == nullptr) {
          M5_LOGE("registry_base_t::init: history memory allocation failed in psram also");
          return;
        }
      }
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
  if (_reg_data == nullptr) {
    M5_LOGE("registry_t::init: registry memory allocation failed");
    _reg_data = (uint8_t*)m5gfx::heap_alloc_psram(_registry_size);
    if (_reg_data == nullptr) {
      M5_LOGE("registry_t::init: registry memory allocation failed in psram also");
      return;
    }
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

uint32_t registry_t::crc32(uint32_t crc_init) const
{
  return calc_crc32(_reg_data, _registry_size, crc_init);
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

// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_REGISTRY_HPP
#define KANPLAY_REGISTRY_HPP

#include <stdint.h>
#include <stddef.h>
#include <map>

#if __has_include (<freertos/freertos.h>)
 #include <freertos/FreeRTOS.h>
 #include <freertos/task.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
uint32_t calc_crc32(const void *data, size_t length, uint32_t crc_init);
//-------------------------------------------------------------------------
class registry_base_t {
public:
  enum data_size_t : uint8_t {
    DATA_NONE = 0,
    DATA_SIZE_8 = 1,
    DATA_SIZE_16 = 2,
    DATA_SIZE_32 = 4,
  };
  struct history_t {
    uint32_t value = 0;
    uint16_t index = 0;
    data_size_t data_size = DATA_NONE;
    uint8_t uid = 0;
  };

  typedef uint32_t history_code_t;
  registry_base_t(uint16_t history_count);
  virtual ~registry_base_t(void);

  virtual void init(bool psram = false);

  virtual bool set8(uint16_t index, uint8_t value, bool force_notify = false);
  virtual bool set16(uint16_t index, uint16_t value, bool force_notify = false);
  virtual bool set32(uint16_t index, uint32_t value, bool force_notify = false);
  virtual uint32_t crc32(uint32_t crc_init = 0) const { return crc_init; }

  const history_t* getHistory(history_code_t &code);
  history_code_t getHistoryCode(void) const { return _history_code; }

#if __has_include (<freertos/freertos.h>)
  void setNotifyTaskHandle(TaskHandle_t handle);
#endif

protected:
  void _addHistory(uint16_t index, uint32_t value, data_size_t data_size);
#if __has_include (<freertos/freertos.h>)
  TaskHandle_t _task_handle = nullptr;
  void _execNotify(void) const { if (_task_handle != nullptr) { xTaskNotify(_task_handle, true, eNotifyAction::eSetValueWithOverwrite); } }
#else
  void _execNotify(void) const {}
#endif
  history_t* _history = nullptr;
  history_code_t _history_code = 0;
  uint16_t _history_count;
};

class registry_t : public registry_base_t {
public:
  registry_t(uint16_t registry_size, uint16_t history_count, data_size_t data_size);
  virtual ~registry_t(void);

  void init(bool psram = false) override;

  bool set8(uint16_t index, uint8_t value, bool force_notify = false) override;
  bool set16(uint16_t index, uint16_t value, bool force_notify = false) override;
  bool set32(uint16_t index, uint32_t value, bool force_notify = false) override;
  uint8_t get8(uint16_t index) const;
  uint16_t get16(uint16_t index) const;
  uint32_t get32(uint16_t index) const;
  void* getBuffer(uint16_t index = 0) const { return &_reg_data_8[index]; }
  void assign(const registry_t &src);
  size_t size(void) const { return _registry_size; }
  uint32_t crc32(uint32_t crc_init = 0) const override;

  // 比較オペレータ
  bool operator==(const registry_t &rhs) const;
  bool operator!=(const registry_t &rhs) const { return !operator==(rhs); }

protected:
  union {
    void* _reg_data = nullptr;
    uint32_t* _reg_data_32;
    uint16_t* _reg_data_16;
    uint8_t* _reg_data_8;
  };
  uint16_t _registry_size;
  data_size_t _data_size;
};


template <typename T>
class registry_map_t : public registry_base_t {
public:
  constexpr registry_map_t(T default_value)
  : registry_base_t { 0 }
  , _default_value { default_value } {};

  // constexpr registry_map_t<T>(void)
  // : registry_base_t { 0 } {};

  bool set(uint16_t index, T value, bool notify = false)
  {
    auto current = get(index);
    if (current != value) {
      notify = true;
      if (value == _default_value) {
        _data.erase(index);
      } else {
        _data[index] = value;
      }
    }
    if (notify) {
      _addHistory(index, 0, data_size_t::DATA_SIZE_8);
      _execNotify();
    }
    return notify;
  }
  const T& get(uint16_t index) const
  {
    auto it = _data.find(index);
    if (it == _data.end()) {
      return _default_value;
    }
    return it->second;
  }
  void assign(const registry_map_t<T> &src)
  {
    _data = src._data;
    if (_history_count == 0) {
      _history_code += 1 << 16;
    }
    _execNotify();
  }
  uint32_t crc32(uint32_t crc) const override {
    for (const auto& pair : _data) {
      crc = calc_crc32( reinterpret_cast<const uint8_t*>(&pair), sizeof(pair), crc);
    }
    return crc;
  }


  // 比較オペレータ
  bool operator==(const registry_map_t<T> &rhs) const { return _data == rhs._data; }
  bool operator!=(const registry_map_t<T> &rhs) const { return !operator==(rhs); }

protected:
  std::map<uint16_t, T> _data;
  T _default_value;
};


class registry_map8_t : public registry_base_t {
public:
  registry_map8_t(uint16_t history_count, uint8_t default_value = 0)
  : registry_base_t { history_count }
  , _default_value { default_value } {};

  bool set8(uint16_t index, uint8_t value, bool force_notify = false);
  uint8_t get8(uint16_t index) const;
  void assign(const registry_map8_t &src);

  // 比較オペレータ
  bool operator==(const registry_map8_t &rhs) const { return _data == rhs._data; }
  bool operator!=(const registry_map8_t &rhs) const { return !operator==(rhs); }

protected:
  std::map<uint16_t, uint8_t> _data;
  uint8_t _default_value = 0;
};

class registry_map32_t : public registry_base_t {
public:
  registry_map32_t(uint16_t history_count, uint8_t default_value = 0)
  : registry_base_t { history_count }
  , _default_value { default_value } {};

  bool set32(uint16_t index, uint32_t value, bool notify = false);
  uint32_t get32(uint16_t index) const;
  void assign(const registry_map32_t &src);

  // 比較オペレータ
  bool operator==(const registry_map32_t &rhs) const { return _data == rhs._data; }
  bool operator!=(const registry_map32_t &rhs) const { return !operator==(rhs); }

protected:
  std::map<uint16_t, uint32_t> _data;
  uint8_t _default_value = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

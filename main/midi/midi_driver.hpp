// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef MIDI_DRIVER_HPP
#define MIDI_DRIVER_HPP

#include <vector>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#if __has_include(<freertos/FreeRTOS.h>)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#endif

namespace midi_driver {

  // MIDI Message structure
  struct MIDI_Message {
    std::vector<uint8_t> data;
    union {
      uint8_t status;
      struct {
        uint8_t channel : 4;
        uint8_t type : 4;
      };
    };
  };
/*
  // MIDI Encoder class
  class MIDI_Encoder {
    std::vector<uint8_t> _data;
    uint8_t _runningStatus;
  public:
    MIDI_Encoder() : _runningStatus(0) {}
    virtual ~MIDI_Encoder() = default;
    void clearRunningStatus(void) { _runningStatus = 0; }
    void clear(void) { _data.clear(); }
    const uint8_t* getBuffer(void) const { return _data.data(); }
    size_t getLength(void) const { return _data.size(); }
    void pushMessage(const MIDI_Message& message);
    void pushMessage(uint8_t status_byte, uint8_t data1 = 0, uint8_t data2 = 0);
  };
//*/
  // MIDI Decoder class
  class MIDI_Decoder {
    std::vector<uint8_t> _data;
    uint8_t _runningStatus;
  public:
    MIDI_Decoder() : _runningStatus(0) {}
    virtual ~MIDI_Decoder() = default;
    void clear(void) { _data.clear(); }

    void addData(const std::vector<uint8_t>& data) {
      _data.insert(_data.end(), data.begin(), data.end());
// printf("data.size:%d\n", _data.size());
    }
    void addData(const uint8_t* data, size_t length) {
      _data.insert(_data.end(), data, data + length);
// printf("data.size:%d\n", _data.size());
    }
    bool popMessage(MIDI_Message* message);
  };

  // Abstract base class for MIDI transport
  class MIDI_Transport {
  public:
    virtual ~MIDI_Transport() = default;

    // Pure virtual functions to be implemented by derived classes
    virtual bool begin(void) = 0;
    virtual void end(void) = 0;

    virtual std::vector<uint8_t> read(void) = 0;
    // virtual size_t write(const uint8_t* data, size_t length) = 0;
    virtual void addMessage(const uint8_t* data, size_t length) = 0;
    virtual bool sendFlush(void) = 0;

    bool isConnected(void) const { return _connected; }
    bool getUseTx(void) const { return _use_tx; }
    bool getUseRx(void) const { return _use_rx; }
    void setUseTx(bool enable) { setUseTxRx(enable, _use_rx); }
    void setUseRx(bool enable) { setUseTxRx(_use_tx, enable); }
    virtual void setUseTxRx(bool tx_enable, bool rx_enable) {
      _use_tx = tx_enable;
      _use_rx = rx_enable;
    }

#if __has_include (<freertos/FreeRTOS.h>)
    void setNotifyTaskHandle(TaskHandle_t handle) { _task_handle = handle; }
    void execTaskNotify(void) const { if (_task_handle != nullptr) { xTaskNotify(_task_handle, true, eNotifyAction::eSetValueWithOverwrite); } }
    void execTaskNotifyISR(void) const {
      if (_task_handle == nullptr) { return; }
      BaseType_t xHigherPriorityTaskWoken;
      xTaskNotifyFromISR(_task_handle, true, eNotifyAction::eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
      if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
      }
    }
protected:
    TaskHandle_t _task_handle = nullptr;
#else
protected:
    void _execNotify(void) const {}
#endif
  protected:
    bool _use_tx = false;
    bool _use_rx = false;
    bool _connected = false;
  };

  // MIDI Driver class
  class MIDIDriver {
    std::vector<uint8_t> _send_data;
    uint8_t _send_runningStatus;
  public:
    MIDIDriver(MIDI_Transport* transport) : _transport(transport) {}
    virtual ~MIDIDriver() = default;

    bool begin(void) { return _transport->begin(); }

    void setSendBufferSize(size_t size) { _send_buffer_size = size; }

    bool isConnected(void) const { return _transport->isConnected(); }
    bool getUseTx(void) const { return _transport->getUseTx(); }
    bool getUseRx(void) const { return _transport->getUseRx(); }
    void setUseTx(bool enable) { _transport->setUseTx(enable); }
    void setUseRx(bool enable) { _transport->setUseRx(enable); }

    void sendMessage(uint8_t status_byte, uint8_t data1, uint8_t data2);

    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
      sendMessage(0x90 | channel, note, velocity);
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
      sendMessage(0x80 | channel, note, velocity);
    }
    void sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
      sendMessage(0xB0 | channel, control, value);
    }
    void sendProgramChange(uint8_t channel, uint8_t program) {
      sendMessage(0xC0 | channel, program, 0);
    }

    bool sendFlush(void) {
      return _transport->sendFlush();
/*
if (_send_data.size() == 0) { return true; }
_send_runningStatus = 0;
bool result = _transport->write(_send_data.data(), _send_data.size());
_send_data.clear();
return result;
*/
    }
    bool receive(void) {
      auto data = _transport->read();
      if (data.empty()) { return false; }
      _decoder.addData(data);
      return true;
    }
    bool receiveMessage(MIDI_Message* message) {
      receive();
      return _decoder.popMessage(message);
    }

    void setUseTxRx(bool tx_enable, bool rx_enable) {
      _transport->setUseTxRx(tx_enable, rx_enable);
    }

#if __has_include (<freertos/FreeRTOS.h>)
    void setNotifyTaskHandle(TaskHandle_t handle) {
      _transport->setNotifyTaskHandle(handle);
    }
#endif

  private:
    MIDI_Transport* _transport;
    // MIDI_Encoder _encoder;
    MIDI_Decoder _decoder;
    size_t _send_buffer_size = 128;
  };

};

#endif

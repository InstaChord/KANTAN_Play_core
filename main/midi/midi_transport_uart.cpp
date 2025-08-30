// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "midi_transport_uart.hpp"

#if __has_include(<driver/uart.h>)

#include "../system_registry.hpp"

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace midi_driver {

static QueueHandle_t uart_queue = nullptr;

//----------------------------------------------------------------

MIDI_Transport_UART::~MIDI_Transport_UART()
{
  end();
}

bool MIDI_Transport_UART::begin(void)
{
  return true;
}

void MIDI_Transport_UART::end(void)
{
  if (_is_begin) {
    _is_begin = false;
    uart_driver_delete((uart_port_t)_config.uart_port_num);
  }
}
/*
size_t MIDI_Transport_UART::write(const uint8_t* data, size_t length)
{
// printf("uart_midi:write: %d\n", length);
  if (_use_tx == false) { return 0; }
  uart_port_t uart_num = (uart_port_t) _config.uart_port_num;
  return uart_write_bytes(uart_num, data, length);
}
*/
void MIDI_Transport_UART::addMessage(const uint8_t* data, size_t length)
{
  if (_tx_data.size() + length >= _config.buffer_size_tx) {
    // If the tx_data size exceeds the buffer size, send it immediately
    sendFlush();
  }
  if (_tx_data.empty()) {
    _tx_runningStatus = 0;
  }
  if (_tx_runningStatus != data[0]) {
    _tx_runningStatus = data[0];
    _tx_data.push_back(data[0]); // status byte
  }
  _tx_data.insert(_tx_data.end(), data + 1, data + length);
}

bool MIDI_Transport_UART::sendFlush(void)
{
  if (_use_tx == false) { return false; }
  uart_port_t uart_num = (uart_port_t) _config.uart_port_num;
  if (!_tx_data.empty()) {
    bool res = uart_write_bytes(uart_num, _tx_data.data(), _tx_data.size());
    if (res > 0) {
      _tx_data.clear();
      _tx_runningStatus = 0;
    }
  }
  return true;
}

std::vector<uint8_t> MIDI_Transport_UART::read(void)
{
  std::vector<uint8_t> rxValueVec;
  if (_use_rx == true)
  {
    size_t length = 0;
    uart_port_t uart_num = (uart_port_t) _config.uart_port_num;
    uart_get_buffered_data_len(uart_num, &length);
    if (length > 0)
    {
      rxValueVec.resize(length);
      size_t read_length = uart_read_bytes(uart_num, rxValueVec.data(), length, 1);
      if (read_length != length) {
        rxValueVec.resize(read_length);
        // error
      }
    }
  }
  return rxValueVec;
}

void MIDI_Transport_UART::uart_rx_task(MIDI_Transport_UART* me)
{
  uart_event_t event;
  for (;;) {
    if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
      me->execTaskNotifyISR();
    }
  }
}

void MIDI_Transport_UART::setUseTxRx(bool tx_enable, bool rx_enable)
{
  if (_use_tx == tx_enable && _use_rx == rx_enable) { return; }
  MIDI_Transport::setUseTxRx(tx_enable, rx_enable);
  int pin_tx = tx_enable ? _config.pin_tx : -1;
  int pin_rx = rx_enable ? _config.pin_rx : -1;
  // esp_err_t err = 

  const uart_port_t uart_num = (uart_port_t)_config.uart_port_num;

  uart_set_pin(uart_num, pin_tx, pin_rx, -1, -1);
  // M5_LOGD("uart_midi:uart_set_pin: %d", err);

  if (_is_begin) {
    return;
  }

  uart_config_t uart_config = {
      .baud_rate = _config.baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 58,
      .source_clk = UART_SCLK_APB,
      .flags = {
          .allow_pd = 0,
          .backup_before_sleep = 0,
      },
  };

  // Configure UART parameters
  esp_err_t err = uart_param_config(uart_num, &uart_config);
  // M5_LOGD("uart_midi:uart_param_config: %d", err);
  if (err == ESP_OK) {
    if (_config.pin_rx >= 0) {
      err = uart_driver_install(uart_num, _config.buffer_size_rx, _config.buffer_size_tx, 4, &uart_queue, 0);
      xTaskCreatePinnedToCore((TaskFunction_t)uart_rx_task, "uart_rx", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);
    } else {
      // Install UART driver using an event queue here
      err = uart_driver_install(uart_num, _config.buffer_size_rx, _config.buffer_size_tx, 0, nullptr, 0);
    }
    if (err == ESP_OK) {
      _is_begin = true;
      _connected = true;
    }
  }
  // M5_LOGD("uart_midi:uart_driver_install: %d", err);
}

//----------------------------------------------------------------

} // namespace midi_driver

#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "midi_transport_usb.hpp"

#include "../common_define.hpp"
#include "../system_registry.hpp"

#include <string.h>
#include <esp_log.h>
#include <mutex>


namespace midi_driver {

  static MIDI_Transport_USB* _instance = nullptr;
  static std::vector<uint8_t> _rx_data;
  static std::mutex mutex_rx;
  static bool isMIDIReady = false;


  class midi_udb_interface {
  public:
    virtual ~midi_udb_interface() {};
    virtual bool begin(void) = 0;
    virtual bool send(const std::vector<uint8_t>& data) = 0;
    virtual size_t getSendBufSize(void) = 0;
  };

  static midi_udb_interface* _midi_usb_instance = nullptr;
};


#if __has_include(<esp32-hal-tinyusb.h>)
#include <USB.h>
#include <USBMIDI.h>

// #include <esp32-hal-tinyusb.h>
#include <esp_mac.h>

namespace midi_driver {

  static USBMIDI usb_midi;
/*
  struct midiEventPacket_t {
    uint8_t header;
    uint8_t byte1;
    uint8_t byte2;
    uint8_t byte3;
  };

  extern "C" uint16_t kanplay_midi_descriptor(uint8_t *dst, uint8_t *itf) {
    uint8_t str_index = tinyusb_add_string_descriptor("KANTAN Play");
    uint8_t ep_in = tinyusb_get_free_in_endpoint();
    TU_VERIFY(ep_in != 0);
    uint8_t ep_out = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_out != 0);
    uint8_t descriptor[TUD_MIDI_DESC_LEN] = {
      TUD_MIDI_DESCRIPTOR(*itf, str_index, ep_out, (uint8_t)(0x80 | ep_in), CFG_TUD_ENDOINT_SIZE),
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_MIDI_DESC_LEN);
    return TUD_MIDI_DESC_LEN;
  }
*/

  class midi_usb_device : public midi_udb_interface {
  protected:

    static void usb_device_task(midi_usb_device* me)
    {
      std::vector<uint8_t> rxData;
      midiEventPacket_t event;
      for (;;) {
        vTaskDelay(1);
        if (!_instance->isConnected()) {
          do {
            vTaskDelay(10);
          } while (!_instance->isConnected());
        }
        if (usb_midi.readPacket(&event)) {
          {
            std::lock_guard<std::mutex> lock(mutex_rx);
            do {
              uint8_t* ptr = reinterpret_cast<uint8_t*>(&event);
              _rx_data.insert(_rx_data.end(), ptr, ptr + 4);
            } while (usb_midi.readPacket(&event));
          }
          _instance->execTaskNotify();
        }
      }
    }

    static void usb_device_start_event(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
    {
      _instance->setConnected(true);
    }

    static void usb_device_stop_event(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
    {
      _instance->setConnected(false);
    }

  public:

    bool begin(void) override {
      USB.onEvent(ARDUINO_USB_STARTED_EVENT, usb_device_start_event);
      USB.onEvent(ARDUINO_USB_STOPPED_EVENT, usb_device_stop_event);
      USB.productName(_instance->getConfig().device_name);
      USB.manufacturerName("InstaChord Corp.");
/*
      tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, kanplay_midi_descriptor);
      char serial_number[16];
      #if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
      if (serial_number == "__MAC__") {
        uint8_t m[6];
        esp_efuse_mac_get_default(m);
        snprintf(serial_number, sizeof(serial_number), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
      }
      #endif
      tinyusb_device_config_t tinyusb_device_config = {
        .vid = USB_ESPRESSIF_VID,
        .pid = 0x0002,
        .product_name = "KANTAN Play",
        .manufacturer_name = "InstaChord",
        .serial_number = serial_number,
        .fw_version = 0x0100,
        .usb_version = 0x0200,
        .usb_class = TUSB_CLASS_MISC,
        .usb_subclass = MISC_SUBCLASS_COMMON,
        .usb_protocol = MISC_PROTOCOL_IAD,
        .usb_attributes = TUSB_DESC_CONFIG_ATT_SELF_POWERED,
        .usb_power_ma = 500,
        .webusb_enabled = false,
        .webusb_url = ""
      };
      if (tinyusb_init(&tinyusb_device_config) == ESP_OK)
*/
      usb_midi.begin();
      if (USB.begin())
      {
        xTaskCreatePinnedToCore((TaskFunction_t)usb_device_task, "usb_device", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);
        return true;
      }
      return false;
    }

    void end(void) {
    }

    size_t getSendBufSize(void) override { return 64; }

    bool send(const std::vector<uint8_t>& data) override
    {
      if (!tud_midi_mounted()) {
        ESP_LOGI("", "MIDI not ready");
        return false;
      }
      midiEventPacket_t* packet;
      for (int i = 0; i < data.size(); i += 4) {
        packet = reinterpret_cast<midiEventPacket_t*>(const_cast<uint8_t*>(&data[i]));
        int retry = 4;
        while (!usb_midi.writePacket(packet) && --retry) {
          taskYIELD();
        }
      }
      return true;
      // return tud_midi_n_stream_write(0, 0, data.data(), data.size());
    }
  };
}

#endif


#if __has_include(<usb/usb_host.h>)
#include <usb/usb_host.h>

namespace midi_driver {

//----------------------------------------------------------------

  typedef void (*usb_host_enum_cb_t)(const usb_config_desc_t *config_desc);
  static usb_host_client_handle_t Client_Handle;
  static usb_device_handle_t Device_Handle;
  static bool isMIDI = false;
  static const size_t MIDI_IN_BUFFERS = 4;
  static usb_transfer_t *MIDIOut = NULL;
  static usb_transfer_t *MIDIIn[MIDI_IN_BUFFERS] = {NULL};

  static usb_intf_desc_t midi_host_interface;

  volatile static bool _tx_request_flip = false;
  volatile static bool _tx_process_flip = false;
  static void midi_transfer_cb(usb_transfer_t *transfer)
  {
    // ESP_LOGI("", "midi_transfer_cb context: %d", transfer->context);
    if (Device_Handle == transfer->device_handle) {
      if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && USB_EP_DESC_GET_EP_DIR(transfer)) {
        uint8_t *const p = transfer->data_buffer;
        {
          std::lock_guard<std::mutex> lock(mutex_rx);
          for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
            if ((p[i] + p[i+1] + p[i+2] + p[i+3]) == 0) break;
            _rx_data.insert(_rx_data.end(), p + i, p + i + 4);
            ESP_LOGI("", "midi: %02x %02x %02x %02x",
                p[i], p[i+1], p[i+2], p[i+3]);
          }
        }
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
          ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
        }
        _instance->execTaskNotifyISR();
      }
      else {
        _tx_process_flip = _tx_request_flip;
      }
    }
  }


  class midi_usb_host : public midi_udb_interface {
  public:
    bool begin(void) override {
      const usb_host_config_t config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
      };
      esp_err_t err = usb_host_install(&config);
      // ESP_LOGI("", "usb_host_install: %x", err);
      if (err != ESP_OK) { return false; }

      const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_client_cb,
            .callback_arg = this
        }
      };
      err = usb_host_client_register(&client_config, &Client_Handle);
      // ESP_LOGI("", "usb_host_client_register: %x", err);
      if (err != ESP_OK) { return false; }

      xTaskCreatePinnedToCore((TaskFunction_t)usb_host_task, "usb_host", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);
      xTaskCreatePinnedToCore((TaskFunction_t)usb_client_task, "usb_client", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub + 1, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);

      return true;
    }

    size_t getSendBufSize(void) override
    {
      if (MIDIOut) {
        return MIDIOut->data_buffer_size;
      }
      return 0;
    }

    bool send(const std::vector<uint8_t>& data) override
    {
      // ※ 前の転送が完了する前にMIDIOutを更新してしまうと、送信データが破損する。
      // そのため、転送が完了するまで待機する。
      while (isMIDIReady && (_tx_process_flip != _tx_request_flip)) {
        taskYIELD();
      }

      if (!isMIDIReady || MIDIOut == nullptr) {
        ESP_LOGI("", "MIDIOut is NULL or not ready");
        return false;
      }

      MIDIOut->num_bytes = data.size();
      memcpy(MIDIOut->data_buffer, data.data(), data.size());
      _tx_request_flip = !_tx_process_flip;
      auto err = usb_host_transfer_submit(MIDIOut);
      return (err == ESP_OK);
    }

  protected:

    static void usb_host_task(midi_usb_host* me)
    {
      // bool all_clients_gone = false;
      // bool all_dev_free = false;
      for (;;) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
#if 0
        if (err == ESP_OK) {
          if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            // ESP_LOGI("", "No more clients");
            // isMIDIReady = false;
            // kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(kanplay_ns::def::command::midiport_info_t::mp_enabled);
            // all_clients_gone = true;
          }
          if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            // ESP_LOGI("", "No more devices");
            // all_dev_free = true;
          }
        }
        else {
          if (err != ESP_ERR_TIMEOUT) {
            // ESP_LOGI("", "usb_host_lib_handle_events: %x flags: %x", err, event_flags);
          }
        }
#endif
      }
    }

    static void usb_client_task(midi_usb_host* me)
    {
      for (;;) {
        esp_err_t err = usb_host_client_handle_events(Client_Handle, portMAX_DELAY);
        if ((err != ESP_OK) && (err != ESP_ERR_TIMEOUT)) {
          // ESP_LOGI("", "usb_host_client_handle_events: %x", err);
        }
      }
    }


    static bool check_interface_desc_MIDI(const usb_intf_desc_t *intf)
    {
      // USB MIDI
      if ((intf->bInterfaceClass == USB_CLASS_AUDIO)
      && (intf->bInterfaceSubClass == 3) // MIDI_STREAMING
      && (intf->bNumEndpoints != 0))
      {
        return true;
      }
      return false;
    }

    static void prepare_endpoints(const void *p)
    {
      const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
      esp_err_t err;

      // must be bulk for MIDI
      if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
        ESP_LOGI("", "Not bulk endpoint: 0x%02x", endpoint->bmAttributes);
        return;
      }
      if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
        for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
          err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIIn[i]);
          if (err != ESP_OK) {
            MIDIIn[i] = NULL;
            ESP_LOGI("", "usb_host_transfer_alloc In fail: %x", err);
          }
          else {
            MIDIIn[i]->device_handle = Device_Handle;
            MIDIIn[i]->bEndpointAddress = endpoint->bEndpointAddress;
            MIDIIn[i]->callback = midi_transfer_cb;
            MIDIIn[i]->context = (void *)i;
            MIDIIn[i]->num_bytes = endpoint->wMaxPacketSize;
            esp_err_t err = usb_host_transfer_submit(MIDIIn[i]);
            if (err != ESP_OK) {
              ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
            }
          }
        }
      }
      else {
        err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIOut);
        if (err != ESP_OK) {
          MIDIOut = NULL;
          ESP_LOGI("", "usb_host_transfer_alloc Out fail: %x", err);
          return;
        }
        ESP_LOGI("", "Out data_buffer_size: %d", MIDIOut->data_buffer_size);
        MIDIOut->device_handle = Device_Handle;
        MIDIOut->bEndpointAddress = endpoint->bEndpointAddress;
        MIDIOut->callback = midi_transfer_cb;
        MIDIOut->context = NULL;
    //    MIDIOut->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
      }
      isMIDIReady = ((MIDIOut != NULL) && (MIDIIn[0] != NULL));
    }

    static void proc_config_desc(const usb_config_desc_t *config_desc)
    {
      const uint8_t *p = &config_desc->val[0];
      uint8_t bLength;
      for (int i = 0; i < config_desc->wTotalLength; i+=bLength, p+=bLength) {
        bLength = *p;
        if ((i + bLength) <= config_desc->wTotalLength) {
          const uint8_t bDescriptorType = *(p + 1);
          switch (bDescriptorType) {
            case USB_B_DESCRIPTOR_TYPE_INTERFACE:
              if (!isMIDI) {
                auto intf = reinterpret_cast<const usb_intf_desc_t*>(p);
                if (check_interface_desc_MIDI(intf)) {
                  midi_host_interface = *intf;
                  isMIDI = true;
                  esp_err_t err = usb_host_interface_claim(Client_Handle, Device_Handle,
                      intf->bInterfaceNumber, intf->bAlternateSetting);
                  if (err != ESP_OK) ESP_LOGI("", "usb_host_interface_claim failed: %x", err);
                }
              }
              break;
            case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
              if (isMIDI && !isMIDIReady) {
                auto endpoint = reinterpret_cast<const usb_ep_desc_t*>(p);
                prepare_endpoints(endpoint);
              }
              break;

            default:
            case USB_B_DESCRIPTOR_TYPE_DEVICE:
            case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
            case USB_B_DESCRIPTOR_TYPE_STRING:
            case USB_B_DESCRIPTOR_TYPE_DEVICE_QUALIFIER:
            case USB_B_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION:
            case USB_B_DESCRIPTOR_TYPE_INTERFACE_POWER:
              break;
          }
        }
        else {
          return;
        }
      }
    }

    static void usb_client_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
    {
      // midi_usb_host *me = static_cast<midi_usb_host *>(arg);

      esp_err_t err;
      switch (event_msg->event)
      {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
          // ESP_LOGI("", "New device address: %d", event_msg->new_dev.address);
          err = usb_host_device_open(Client_Handle, event_msg->new_dev.address, &Device_Handle);
          if (err == ESP_OK) {
            usb_device_info_t dev_info;
            err = usb_host_device_info(Device_Handle, &dev_info);
            if (err == ESP_OK) {
              const usb_device_desc_t *dev_desc;
              err = usb_host_get_device_descriptor(Device_Handle, &dev_desc);
              if (err == ESP_OK) {
                const usb_config_desc_t *config_desc;
                err = usb_host_get_active_config_descriptor(Device_Handle, &config_desc);
                if (err == ESP_OK) {
                  proc_config_desc(config_desc);
                }
              }
            }
          }
          break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
          if (Device_Handle == event_msg->dev_gone.dev_hdl) {
            isMIDIReady = false;
            if (MIDIOut != nullptr) {
              if (MIDIOut->device_handle == event_msg->dev_gone.dev_hdl) {
                err = usb_host_transfer_free(MIDIOut);
                MIDIOut = nullptr;
              }
            }
            for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
              if (MIDIIn[i] != nullptr) {
                if (MIDIIn[i]->device_handle == event_msg->dev_gone.dev_hdl) {
                  err = usb_host_transfer_free(MIDIIn[i]);
                  MIDIIn[i] = nullptr;
                }
              }
            }
            if (isMIDI) {
              isMIDI = false;
              err = usb_host_interface_release(Client_Handle, Device_Handle, midi_host_interface.bInterfaceNumber);
            }
            err = usb_host_device_close(Client_Handle, event_msg->dev_gone.dev_hdl);
            // ESP_LOGI("", "Device gone: %d", event_msg->dev_gone.device_handle);
            Device_Handle = nullptr;
          }
          else {
            // ESP_LOGI("", "Device gone, but not the one we are using: %d", event_msg->dev_gone.device_handle);
          }
          break;

        default:
          // ESP_LOGI("", "Unknown value %d", event_msg->event);
          break;
      }
      if (_instance->isConnected() != isMIDIReady)
      {
        _instance->setConnected(isMIDIReady);
      }
    }
  };
} // namespace midi_driver

#endif


namespace midi_driver {

MIDI_Transport_USB::~MIDI_Transport_USB()
{
  end();
}

bool MIDI_Transport_USB::begin(void)
{
  return true;
}

void MIDI_Transport_USB::end(void)
{
  // 現状、USBの動作は一度beginするとendできない。
  // (安全にUSB関連のリソースを解放する処理が実装できていないため)
}

void MIDI_Transport_USB::addMessage(const uint8_t* data, size_t length)
{
  if (_use_tx == false) {
    ESP_LOGI("", "tx not enabled");
    return;
  }

  // TODO:システムエクスクルーシブの送信には未対応。
  // 必要に応じて実装すること…。

  _tx_data.push_back(data[0] >> 4);
  _tx_data.insert(_tx_data.end(), data, data + 3);
  int remain = _midi_usb_instance->getSendBufSize() - _tx_data.size();
  if (remain - 4 <= 0) {
    sendFlush();
  }
}

bool MIDI_Transport_USB::sendFlush(void)
{
  if (_use_tx == false) {
    ESP_LOGI("", "MIDIOut is NULL or tx not enabled");
    return false;
  }
  if (_tx_data.empty()) {
    return true;
  }

  if (_midi_usb_instance->send(_tx_data))
  {
    _tx_data.clear();
    return true;
  }
  return false;
  
  // MIDIOut->num_bytes = _tx_data.size();
  // memcpy(MIDIOut->data_buffer, _tx_data.data(), _tx_data.size());
  // _tx_data.clear();
  // _tx_request_flip = !_tx_process_flip;
  // auto err = usb_host_transfer_submit(MIDIOut);
  // return (err == ESP_OK);
}

std::vector<uint8_t> MIDI_Transport_USB::read(void)
{
  std::vector<uint8_t> rxValueVec;
  std::lock_guard<std::mutex> lock(mutex_rx);
  if (!_rx_data.empty()) {

    static constexpr uint8_t cin_length_table[] = {
       0, 0, 2, 3, 3, 1, 2, 3,
       3, 3, 3, 3, 2, 2, 3, 1,
    };
    for (int i = 0; i < _rx_data.size(); i += 4) {
      uint8_t cin = _rx_data[i] & 0x0f; // Code Index Number
      size_t len = cin_length_table[cin];
      if (len) {
        rxValueVec.insert(rxValueVec.end(), _rx_data.begin() + i + 1, _rx_data.begin() + i + 1 + len);
      }
    }
    _rx_data.clear();
  }
  return rxValueVec;
}

void MIDI_Transport_USB::setConnected(bool flg)
{
  _connected = flg;
  auto midiport_info = kanplay_ns::def::command::midiport_info_t::mp_off;
  if (_connected) {
    midiport_info = kanplay_ns::def::command::midiport_info_t::mp_connected;
  } else if (_use_tx || _use_rx) {
    midiport_info = kanplay_ns::def::command::midiport_info_t::mp_enabled;
  }
  kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(midiport_info);
}

void MIDI_Transport_USB::setUseTxRx(bool tx_enable, bool rx_enable)
{
  if (_use_tx == tx_enable && _use_rx == rx_enable) { return; }

  _instance = this;

  MIDI_Transport::setUseTxRx(tx_enable, rx_enable);
  // M5_LOGD("uart_midi:uart_set_pin: %d", err);

  kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB
  ( (tx_enable || rx_enable)
    ? kanplay_ns::def::command::midiport_info_t::mp_enabled
    : kanplay_ns::def::command::midiport_info_t::mp_off
  );

  if (_is_begin) return;
  _is_begin = true;

  if (_midi_usb_instance == nullptr) {
    if (_usb_mode == kanplay_ns::def::command::usb_mode_t::usb_device) {
      _midi_usb_instance = new midi_usb_device();
    } else {
      _midi_usb_instance = new midi_usb_host();
    }
  }
  _midi_usb_instance->begin();
}

bool MIDI_Transport_USB::setUSBMode(kanplay_ns::def::command::usb_mode_t mode)
{
  if (_usb_mode == mode) {
    return true; // 設定値の変更がない場合はtrueで終了
  }
  if (_is_begin) {
    return false; // begin済みなので設定変更できないのでfalseで終了
  }

  _usb_mode = mode;
  return true;
}

//----------------------------------------------------------------

} // namespace midi_driver

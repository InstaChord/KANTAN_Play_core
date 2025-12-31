// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_i2s.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"

#if !defined (M5UNIFIED_PC_BUILD)

#if __has_include(<driver/i2s_std.h>)
 #include <driver/i2s_std.h>
#else
 #include <driver/i2s.h>
#endif

#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------

static constexpr const uint16_t i2s_dma_frame_num = 96;

#if !defined (M5UNIFIED_PC_BUILD)

static constexpr const i2s_port_t i2s_port = I2S_NUM_1;
static constexpr const uint16_t i2s_dma_desc_num = 4;

static const size_t overwrap = 0;
// static int32_t bufdata[overwrap + i2s_dma_frame_num];
// static int32_t* i2sbuf = &bufdata[overwrap];

#if __has_include(<driver/i2s_std.h>)

static i2s_chan_handle_t _i2s_tx_handle = nullptr;
static i2s_chan_handle_t _i2s_rx_handle = nullptr;
static esp_err_t _i2s_init(void)
{
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)i2s_port, I2S_ROLE_SLAVE);
  chan_cfg.dma_desc_num = i2s_dma_desc_num;
  chan_cfg.dma_frame_num = i2s_dma_frame_num >> 1;
  esp_err_t err = i2s_new_channel(&chan_cfg, &_i2s_tx_handle, &_i2s_rx_handle);
  if (err != ESP_OK) { return err; }
  i2s_std_config_t i2s_config;
  memset(&i2s_config, 0, sizeof(i2s_std_config_t));
  i2s_config.clk_cfg.clk_src = i2s_clock_src_t::I2S_CLK_SRC_PLL_160M;
  i2s_config.clk_cfg.sample_rate_hz = 48000; // dummy setting
  // i2s_config.clk_cfg.mclk_multiple = i2s_mclk_multiple_t::I2S_MCLK_MULTIPLE_128;
  i2s_config.slot_cfg.data_bit_width = i2s_data_bit_width_t::I2S_DATA_BIT_WIDTH_32BIT;
  i2s_config.slot_cfg.slot_bit_width = i2s_slot_bit_width_t::I2S_SLOT_BIT_WIDTH_32BIT;
  i2s_config.slot_cfg.slot_mode = i2s_slot_mode_t::I2S_SLOT_MODE_STEREO;
  i2s_config.slot_cfg.slot_mask = i2s_std_slot_mask_t::I2S_STD_SLOT_BOTH;
  i2s_config.slot_cfg.ws_width = 32;
  i2s_config.slot_cfg.ws_pol = false;
  i2s_config.slot_cfg.bit_shift = true;
#if SOC_I2S_HW_VERSION_1    // For esp32/esp32-s2
  i2s_config.slot_cfg.msb_right = false;
#else
  i2s_config.slot_cfg.left_align = true;
  i2s_config.slot_cfg.big_endian = false;
  i2s_config.slot_cfg.bit_order_lsb = false;
#endif
  i2s_config.gpio_cfg.bclk = def::hw::pin::i2s_bck;
  i2s_config.gpio_cfg.ws   = def::hw::pin::i2s_ws;
  i2s_config.gpio_cfg.dout = def::hw::pin::i2s_out;
  i2s_config.gpio_cfg.mclk = def::hw::pin::i2s_mclk;
  i2s_config.gpio_cfg.din  = def::hw::pin::i2s_in;
  err = i2s_channel_init_std_mode(_i2s_tx_handle, &i2s_config);
  err = i2s_channel_init_std_mode(_i2s_rx_handle, &i2s_config);

  return ESP_OK;
}

static esp_err_t _i2s_start(void) {
  if (_i2s_tx_handle == nullptr) { return ESP_FAIL; }
  return i2s_channel_enable(_i2s_tx_handle) || i2s_channel_enable(_i2s_rx_handle);
}

static esp_err_t _i2s_write(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_channel_write(_i2s_tx_handle, buf, len, result, tick);
}

static esp_err_t _i2s_read(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_channel_read(_i2s_rx_handle, buf, len, result, tick);
}

#else

static esp_err_t _i2s_init(void)
{
    i2s_config_t i2s_config;
    memset(&i2s_config, 0, sizeof(i2s_config_t));
    i2s_config.mode                 = (i2s_mode_t)( I2S_MODE_SLAVE | I2S_MODE_TX | I2S_MODE_RX );
    i2s_config.sample_rate          = 48000; // dummy setting
    i2s_config.bits_per_sample      = i2s_bits_per_sample_t::I2S_BITS_PER_SAMPLE_32BIT;
    i2s_config.channel_format       = i2s_channel_fmt_t::I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = i2s_comm_format_t::I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_config.tx_desc_auto_clear   = false;
    i2s_config.use_apll             = false;
    i2s_config.fixed_mclk           = 0;
    i2s_config.mclk_multiple        = i2s_mclk_multiple_t::I2S_MCLK_MULTIPLE_DEFAULT;
    i2s_config.bits_per_chan        = i2s_bits_per_chan_t::I2S_BITS_PER_CHAN_32BIT;
#if I2S_DRIVER_VERSION > 1
    i2s_config.dma_desc_num         = i2s_dma_desc_num;
    i2s_config.dma_frame_num        = i2s_dma_frame_num >> 1;
#else
    i2s_config.dma_buf_count        = i2s_dma_desc_num;
    i2s_config.dma_buf_len          = i2s_dma_frame_num >> 1;
#endif
    esp_err_t err;
    if (ESP_OK != (err = i2s_driver_install(i2s_port, &i2s_config, 0, nullptr)))
    {
      M5_LOGE("i2s_driver_install: %d", err);
      return err;
    }

    i2s_pin_config_t pin_config;
    memset(&pin_config, ~0u, sizeof(i2s_pin_config_t)); /// all pin set to I2S_PIN_NO_CHANGE
    pin_config.bck_io_num     = def::hw::pin::i2s_bck;
    pin_config.ws_io_num      = def::hw::pin::i2s_ws;
    pin_config.data_out_num   = def::hw::pin::i2s_out;
    pin_config.data_in_num    = def::hw::pin::i2s_in;
    pin_config.mck_io_num     = def::hw::pin::i2s_mclk;
    err = i2s_set_pin(i2s_port, &pin_config);
    if (ESP_OK != err)
    {
      M5_LOGE("i2s_set_pin: %d", err);
      return err;
    }

  return ESP_OK;
}

static esp_err_t _i2s_start(void) {
  return i2s_start(i2s_port);
}

static esp_err_t _i2s_write(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_write(i2s_port, buf, len, result, tick);
}

static esp_err_t _i2s_read(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_read(i2s_port, buf, len, result, tick);
}

#endif

#endif

static int32_t* bufdata = nullptr;
static constexpr const size_t buf_size = (i2s_dma_frame_num) * sizeof(int32_t);

bool task_i2s_t::start(void)
{
  int len = system_registry->raw_wave_length;
  auto wav_buf = system_registry->raw_wave;
  for (int i = 0; i < len; ++i) {
    wav_buf[i] = std::make_pair(128, 128);
  }

#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "i2s", this);
#else

  // メモリブロックの断片化への対策として、小さい断片化領域から使用するため、敢えて最大領域を先回りして確保する。
  auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  // 上記の処理により、以下のメモリ確保は二番目に小さい領域から確保されることになる。
  bufdata = (int32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  // 先回りして確保しておいた領域を解放する。
  m5gfx::heap_free(dummy);
  if (bufdata == nullptr) {
    bufdata = (int32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  }
  memset(bufdata, 0, buf_size);

  _i2s_init();

  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "i2s", 1024*3, this, def::system::task_priority_i2s, nullptr, def::system::task_cpu_i2s);
#endif
  return true;
}

void task_i2s_t::task_func(task_i2s_t* me)
{
#if defined (M5UNIFIED_PC_BUILD)
  auto raw_wave_pos = system_registry->raw_wave_pos;
  auto wav_buf = system_registry->raw_wave;
  for (;;) {
    uint8_t min_level = 32 + (rand() & 127);
    uint8_t max_level = min_level + 64;

    wav_buf[raw_wave_pos] = std::make_pair(min_level, max_level);
  
    if (++raw_wave_pos >= (system_registry->raw_wave_length)) {
      raw_wave_pos = 0;
    }
    system_registry->raw_wave_pos = raw_wave_pos;
    SDL_Delay(1);
  }

#else
  int32_t* i2sbuf = &bufdata[overwrap];

  size_t transfer_size;

// M5.delay(4000);
#if __has_include(<driver/i2s_std.h>)
  do {
    // ++preload;
    i2s_channel_preload_data(_i2s_tx_handle, i2sbuf, buf_size, &transfer_size);
  } while (transfer_size == buf_size);
#endif
  // M5_LOGD("preload: %d", preload);

  _i2s_start();

  int32_t current_volume = 0;
  int volume_shift = 8;
  int shifted_volume = 0;

  // int32_t min_level = 0;
  // int32_t max_level = 0;

  for (;;) {
    _i2s_read(i2sbuf, buf_size, &transfer_size, 128);
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_I2S);

    // マスターボリュームのレンジ0~100を 1~256に変換
    int32_t target_volume = system_registry->user_setting.getMasterVolume() << 8;
    if (target_volume > 25600) { target_volume = 25600; }

    // 現在の音量と目標の音量に差がある場合は滑らかに接近させる
    if (current_volume != target_volume) {
      current_volume += (target_volume - current_volume + (target_volume < current_volume ? 0 : 32)) >> 5;
      shifted_volume = current_volume / 100;
    }
    volume_shift = 8;

    int32_t min_level = INT32_MAX;
    int32_t max_level = INT32_MIN;

// if (current_volume != 25600)
{
    // ボリュームを適用
    for (int i = 0; i < i2s_dma_frame_num; i+=2) {
      int l = i2sbuf[i  ];
      int r = i2sbuf[i+1];
      if (min_level > l) { min_level = l; }
      if (max_level < l) { max_level = l; }
      if (min_level > r) { min_level = r; }
      if (max_level < r) { max_level = r; }
      i2sbuf[i  ] = (l >> volume_shift) * shifted_volume;
      i2sbuf[i+1] = (r >> volume_shift) * shifted_volume;
    }
    min_level = ((min_level >> 16) + 32768 + 128) >> 8;
    max_level = ((max_level >> 16) + 32768 + 128) >> 8;

    auto wav_buf = system_registry->raw_wave;
    auto raw_wave_pos = system_registry->raw_wave_pos;
    wav_buf[raw_wave_pos ++] = std::make_pair<uint8_t, uint8_t>(min_level, max_level);

    if (raw_wave_pos >= (system_registry->raw_wave_length)) {
      raw_wave_pos = 0;
    }
    system_registry->raw_wave_pos = raw_wave_pos;
}

// M5_LOGE("readsize: %d", readsize);
/* デバッグ用 ノコギリ波をミキシングする
static int32_t value;
int add = system_registry->internal_input.get16(0);
for (int i = 0; i < i2s_dma_frame_num; i++) {
  i2sbuf[i] = (i2sbuf[i] + (value << 12)) >> 2;
  value += add;
  if (value > 65536) {
    value -= 65536*2;
  }
}
//*/
// size_t result;
    system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_I2S);
    _i2s_write(bufdata, buf_size, &transfer_size, 128);
  }
#endif
}

void task_i2s_t::playRaw(float samplerate, const int16_t* src, size_t len, bool stereo)
{
  
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

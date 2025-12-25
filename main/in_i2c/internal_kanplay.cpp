// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "internal_kanplay.hpp"

#include "internal_es8388.hpp"
#include "internal_si5351.hpp"
#include "internal_bmi270.hpp"
#include "../system_registry.hpp"
#include "../common_define.hpp"
#include "firmware_kanplay.h"

namespace kanplay_ns {
//-------------------------------------------------------------------------

static constexpr const uint32_t i2c_freq = 800000;
static constexpr const uint8_t i2c_addr = 0x56;
static constexpr const uint8_t i2c_bootloader_addr = 0x54;

static internal_es8388_t internal_es8388;
static internal_si5351_t internal_si5351;
static internal_bmi270_t internal_bmi270;

static bool writeRegister8(uint8_t reg, uint8_t data)
{
    return M5.In_I2C.writeRegister8(i2c_addr, reg, data, i2c_freq);
}

static bool writeRegister(uint8_t reg, const uint8_t* data, size_t length)
{
    return M5.In_I2C.writeRegister(i2c_addr, reg, data, length, i2c_freq);
}

static bool readRegister(uint8_t reg, uint8_t* result, size_t length)
{
    return M5.In_I2C.readRegister(i2c_addr, reg, result, length, i2c_freq);
}

#if !defined ( M5UNIFIED_PC_BUILD )

static void gpio_interrupt_handler(void *args)
{
  // タスクに通知して起こす
  BaseType_t xHigherPriorityTaskWoken;
  xTaskNotifyFromISR((TaskHandle_t)args, true, eNotifyAction::eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

#endif

void internal_kanplay_t::setupInterrupt(void)
{
#if !defined ( M5UNIFIED_PC_BUILD )
  // STM32側からのINT通知ピンがLOWになった時に割込みを発生させる
  m5gfx::pinMode(def::hw::pin::stm_int, m5gfx::input);
  gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
  gpio_set_intr_type((gpio_num_t)def::hw::pin::stm_int, GPIO_INTR_NEGEDGE);
  gpio_isr_handler_add((gpio_num_t)def::hw::pin::stm_int, gpio_interrupt_handler, xTaskGetCurrentTaskHandle());
#endif
}

uint8_t internal_kanplay_t::getFirmwareVersion(void)
{
  uint8_t fw_ver = 0;
  readRegister(0xFE, &fw_ver, 1);
  return fw_ver;
}

bool internal_kanplay_t::checkUpdate(void)
{
  uint8_t fw_ver = getFirmwareVersion();
  return (fw_ver != 0) &&(fw_ver < def::system::internal_firmware_version);
}

bool internal_kanplay_t::init(void)
{
  int retry = 16;

  uint8_t fw_ver = 0;
  while (0 == (fw_ver = getFirmwareVersion()) && --retry) { M5.delay(16); }

  if (retry == 0) {
    M5_LOGE("Failed to read firmware version.");
    return false;
  }
  M5_LOGI("Firmware version: %d", fw_ver);

  if (fw_ver < def::system::internal_firmware_version) {
    M5_LOGW("Firmware upgrade start.");
    execFirmwareUpdate();
  }

  // SAM reset
  writeRegister8(0x50, 0);

  // AMP disable
  writeRegister8(0x40, 0);

  // HeadPhone disable
  writeRegister8(0x41, 0);

  // Microphone disable
  writeRegister8(0x42, 0);

  internal_si5351.init(10, 27000000);
  internal_es8388.init();
  internal_si5351.update(0);
  internal_es8388.unmute();
  internal_bmi270.init();

  // SAM reset release
  writeRegister8(0x50, 1);

  // Encoder counter reset
  writeRegister(0x30, (const uint8_t[]){1,1,1}, 3);

  M5.delay(64);

  // イヤホン挿入状態のステータスを無効値で初期化。
  // 後の処理でステータスが正しく設定され、状況に合わせてAMPの出力が変更される。
  system_registry->runtime_info.setHeadphoneEnabled(0xFF);

  return true;
}

static uint32_t calcImuStandardDeviation(void)
{
  // 過去サンプルの加速度の平均値を求める
  int32_t avg_x = 0;
  int32_t avg_y = 0;
  int32_t avg_z = 0;
  for (int i = 0; i < 16; ++i) {
    auto accel = internal_bmi270.getAccel(i);
    avg_x += accel.x;
    avg_y += accel.y;
    avg_z += accel.z;
  }
  avg_x = (avg_x + 8) >> 4;
  avg_y = (avg_y + 8) >> 4;
  avg_z = (avg_z + 8) >> 4;

  // 標準偏差を求める
  uint32_t sd = 0;
  for (int i = 0; i < 16; ++i) {
    auto accel = internal_bmi270.getAccel(i);
    int diff_x = accel.x - avg_x;   sd += diff_x * diff_x;
    int diff_y = accel.y - avg_y;   sd += diff_y * diff_y;
    int diff_z = accel.z - avg_z;   sd += diff_z * diff_z;
  }
  return sd;
}

static void updateImuVelocity(void)
{
  if (internal_bmi270.update()) {
    // IMUの標準偏差をsystem_registryに保存
    uint32_t sd = calcImuStandardDeviation();
    system_registry->internal_imu.setImuStandardDeviation(sd);
  }
}

#define DEBUGGING_TEST 0
#define DEBUGGING_RANDUM 0

bool internal_kanplay_t::update(void)
{
    bool flg_int = false;

#if defined ( M5UNIFIED_PC_BUILD )
    M5.delay(1);
#else
    flg_int = (ulTaskNotifyTake(pdTRUE, 1));
#endif
#if DEBUGGING_TEST
bool debuging_lv = (millis() >> 7) & 1;
static bool prev_debuging_lv;
if (prev_debuging_lv != debuging_lv) {
prev_debuging_lv = debuging_lv;
flg_int = true;
}
#endif
  if (!flg_int) {
    // I2C通信に時間が掛かる場面なのでSuspend申告してCPUクロックを下げることを許可しておく
    // (CPUクロックを高くしてもI2Cの通信時間が短くなるわけではないので)
    system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_I2C);

    for (int loop = 0; loop < 3; ++loop) {
      static uint8_t proc_index = 0;
      if (++proc_index > 5) proc_index = 0;
      switch (proc_index) {
      case 0:
      case 3:
        updateImuVelocity();
        break;

      default:
// RGB LEDの変更指示は連続して実行するとSTM32側がハングアップすることがあるため、一度に処理しない。
        {
          auto history = system_registry->rgbled_control.getHistory(rgbled_history_code);
          if (history != nullptr) {
            uint32_t rgb_reg_index = history->index >> 2;
            if (rgb_reg_index < def::hw::max_rgb_led) {
              // RGB LEDは全開で点灯させない。1/4に輝度を下げる。
              // uint32_t color = (history->value >> 2) & 0x3F3F3F;
              uint32_t color = history->value;
              uint8_t brightness = system_registry->user_setting.getLedBrightness();
              static constexpr const uint8_t brightness_table[] = { 21, 34, 55, 89, 144 };
              brightness = brightness_table[brightness];
              uint32_t r = (color & 0xFF) * brightness >> 8;
              uint32_t g = ((color >> 8) & 0xFF) * brightness >> 8;
              uint32_t b = ((color >> 16) & 0xFF) * brightness >> 8;
              color = r | (g << 8) | (b << 16);

              // ハード側のLED番号と、かんぷれシステムのLED番号の対応
              static constexpr const uint8_t led_index_mapping[] = {
              14, 15, 16, 17, 18,
                9, 10, 11, 12, 13,
                4,  5,  6,  7,  8,
                  0,  1,  2,  3,
              };
              writeRegister(0x70 + led_index_mapping[rgb_reg_index] * 4, (const uint8_t*)&color, 3);
            }
          }
        }
        break;
      }
#if !defined ( M5UNIFIED_PC_BUILD )
      flg_int = (ulTaskNotifyTake(pdTRUE, 1));

#if DEBUGGING_TEST
debuging_lv = (millis() >> 7) & 1;
if (prev_debuging_lv != debuging_lv) {
prev_debuging_lv = debuging_lv;
flg_int = true;
}
#endif
      
      if (flg_int) {
        break;
      }
#endif
    }
    // I2C通信が終わったらCPUクロックを上げる
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_I2C);
  }

  static uint32_t btns = ~0u;
  static uint8_t reg62[4] = {0, 0, 0, 0};
  static uint32_t encoder[3] = { 0, 0, 0 };

  uint32_t tmp[4];
  // STM32から各種情報を取得
  bool i2c_ok = readRegister(0x00, (uint8_t*)tmp, 3);
  if (i2c_ok) {
#if DEBUGGING_TEST
if (debuging_lv) {
  tmp[0] |= 0x7FFF0;
}
#endif
#if DEBUGGING_RANDUM
  if (0 == (tmp[0] & 0x4000)) {
    tmp[0] = tmp[0] & ~0x7FFF0 | (rand() & 0x7FFF0);
  }
#endif
    if (btns != tmp[0]) {
      btns = tmp[0];
    } else {
      // メインのボタン状態が変化していなければエンコーダとノブの状態も取得する
      if (readRegister(0x10, (uint8_t*)tmp, 12)) {
        encoder[0] = tmp[0];
        encoder[1] = tmp[1];
        encoder[2] = tmp[2];
      }
      if (readRegister(0x62, (uint8_t*)tmp, 4)) {
        *((uint32_t*)reg62) = tmp[0];
      }
    }

    {
      uint32_t button_bitmap = 0;
      if (0 == (btns & 0x000001)) { button_bitmap += def::button_bitmask::SUB_1;    }
      if (0 == (btns & 0x000002)) { button_bitmap += def::button_bitmask::SUB_2;    }
      if (0 == (btns & 0x000004)) { button_bitmap += def::button_bitmask::SUB_3;    }
      if (0 == (btns & 0x000008)) { button_bitmap += def::button_bitmask::SUB_4;    }
      if (0 == (btns & 0x000010)) { button_bitmap += def::button_bitmask::MAIN_11;  }
      if (0 == (btns & 0x000020)) { button_bitmap += def::button_bitmask::MAIN_12;  }
      if (0 == (btns & 0x000040)) { button_bitmap += def::button_bitmask::MAIN_13;  }
      if (0 == (btns & 0x000080)) { button_bitmap += def::button_bitmask::MAIN_14;  }
      if (0 == (btns & 0x000100)) { button_bitmap += def::button_bitmask::MAIN_15;  }
      if (0 == (btns & 0x000200)) { button_bitmap += def::button_bitmask::MAIN_06;  }
      if (0 == (btns & 0x000400)) { button_bitmap += def::button_bitmask::MAIN_07;  }
      if (0 == (btns & 0x000800)) { button_bitmap += def::button_bitmask::MAIN_08;  }
      if (0 == (btns & 0x001000)) { button_bitmap += def::button_bitmask::MAIN_09;  }
      if (0 == (btns & 0x002000)) { button_bitmap += def::button_bitmask::MAIN_10;  }
      if (0 == (btns & 0x004000)) { button_bitmap += def::button_bitmask::MAIN_01;  }
      if (0 == (btns & 0x008000)) { button_bitmap += def::button_bitmask::MAIN_02;  }
      if (0 == (btns & 0x010000)) { button_bitmap += def::button_bitmask::MAIN_03;  }
      if (0 == (btns & 0x020000)) { button_bitmap += def::button_bitmask::MAIN_04;  }
      if (0 == (btns & 0x040000)) { button_bitmap += def::button_bitmask::MAIN_05;  }
      if (0 == (btns & 0x080000)) { button_bitmap += def::button_bitmask::SIDE_1;   }
      if (0 == (btns & 0x100000)) { button_bitmap += def::button_bitmask::SIDE_2;   }
      if (0 == (btns & 0x200000)) { button_bitmap += def::button_bitmask::ENC1_PUSH;}
      if (0 == (btns & 0x400000)) { button_bitmap += def::button_bitmask::ENC2_PUSH;}
      if (0 == (reg62[1])       ) { button_bitmap += def::button_bitmask::KNOB_L;   }
      if (0 == (reg62[2])       ) { button_bitmap += def::button_bitmask::KNOB_R;   }
      if (0 == (reg62[3])       ) { button_bitmap += def::button_bitmask::KNOB_K;   }

      static uint32_t prev_btnmask = 0;
      if (prev_btnmask != button_bitmap) {
        if (button_bitmap & ~prev_btnmask & 0x7FFFu) {
          // 演奏に関わるボタンが押された場合はIMUの値を取得、ベロシティを更新する
          updateImuVelocity();
        }
        prev_btnmask = button_bitmap;
        // エンコーダの状態を含まない状態でボタンビットマスク更新
        system_registry->internal_input.setButtonBitmask(button_bitmap);
      }
      // エンコーダ３は上下方向を逆転させておく
      static constexpr const uint32_t enc_mask_table[3][2] = {
        { def::button_bitmask::ENC1_DOWN, def::button_bitmask::ENC1_UP },
        { def::button_bitmask::ENC2_DOWN, def::button_bitmask::ENC2_UP },
        { def::button_bitmask::ENC3_UP, def::button_bitmask::ENC3_DOWN },
      };
      static uint8_t prev_enc[3] = { 0, };
      for (int enc = 0; enc < 3; ++enc)
      {
        // エンコーダの回転操作をボタンビットマスクに反映
        int8_t diff = encoder[enc] - prev_enc[enc];
        if (diff) {
          prev_enc[enc] = encoder[enc];
          system_registry->internal_input.setEncValue(enc, encoder[enc]);
          uint32_t enc_bitmap = button_bitmap + ((diff < 0) ? enc_mask_table[enc][0] : enc_mask_table[enc][1]);
          diff = abs(diff);
          do {
            // 回転量に応じてボタンビットマスクを更新 (ボタン連打操作のように扱う)
            system_registry->internal_input.setButtonBitmask(button_bitmap);
            system_registry->internal_input.setButtonBitmask(enc_bitmap);
          } while (--diff);
        }
      }

      // イヤホンの接続状態を更新
      if (system_registry->runtime_info.getHeadphoneEnabled() != reg62[0]) {
        system_registry->runtime_info.setHeadphoneEnabled(reg62[0]);
        // イヤホン挿入時はAMPを無効にする
        uint8_t reg0x40[3];
        reg0x40[0] = reg62[0] ? 0 : 1; // 本体スピーカーアンプ
        reg0x40[1] = reg62[0] ? 1 : 0; // イヤホンジャック
        reg0x40[2] = 0;                // マイク
        writeRegister(0x40, reg0x40, 3);
      }
    }
  }

  {
    auto volume = system_registry->user_setting.getMasterVolume();
    volume = 13 + (volume / 5);
    if (internal_es8388.getOutVolume() != volume) {
      internal_es8388.setOutVolume(volume);
    }
    auto adcmic = system_registry->user_setting.getADCMicAmp();
    if (internal_es8388.getInVolume() != adcmic) {
      internal_es8388.setInVolume(adcmic);
    }
  }

  return true;
}

void internal_kanplay_t::mute(void)
{
  static constexpr const uint8_t dummy[4] = { 0, 0, 0, 0, };
  writeRegister(0x40, dummy, 3);
  internal_es8388.mute();
}

//-------------------------------------------------------------------------


#define  OPC_WREN       (uint8_t)(0x06)
#define  OPC_USRCD      (uint8_t)(0x77)
#define APPLICATION_ADDRESS 0x08001800
#define TOTAL_APPLICATION_SIZE 56*1024

static bool waitingBootloader(int retry = 2048) {
  // bootloader i2c address is 0x54, wait until bootloader is valid
  do {
    M5.delay(1);
    if (0 == (retry & 0xFF)) {
      M5_LOGI("waiting bootloader."); 
    }
  } while (!M5.In_I2C.scanID(i2c_bootloader_addr) && --retry);
  M5_LOGI("Success."); 
  return retry;
}

static bool writeBootloader(uint8_t command)
{
  return M5.In_I2C.start(i2c_bootloader_addr, false, i2c_freq)
      && M5.In_I2C.write(&command, 1)
      && M5.In_I2C.stop();
}

bool internal_kanplay_t::execFirmwareUpdate(void)
{
  // write any number more than 1, jump to bootloader
  if (!writeRegister8(0xFD, 1)) {
    M5_LOGE("Failed to jump to bootloader.");
    return false;
  }

  // func_enter_iap
  {
    M5_LOGI("update start."); 

    if (!waitingBootloader()) {
      M5_LOGE("Failed to wait bootloader.");
      return false;
    }

    if (!writeBootloader( OPC_WREN )) {
      M5_LOGE("Failed to send OPC_WREN.");
      return false;
    }
    M5_LOGI("send OPC_WREN done.");
  }

  // send firmware data
  // func_iap_upgrade
  {
    uint32_t buffer_index = 0;
    uint32_t app_address = 0;
    uint32_t sendbuffer_Size = 0;   

    // total 56K
    sendbuffer_Size = TOTAL_APPLICATION_SIZE; 
    // application address is 0x08001800
    app_address = APPLICATION_ADDRESS;

    // bootloader i2c address is 0x54
    if (!waitingBootloader()) {
      M5_LOGE("Failed to wait bootloader.");
      return false;
    }

    uint8_t sendbuffer[8];
    bool flg_error = false;
    do {
      // Must be a legitimate address
      if (app_address < APPLICATION_ADDRESS) {
        flg_error = true;
        break;
      }

      /*Fille data in to buffuer*/
      sendbuffer[0]=OPC_WREN;      /*Command Code*/
      
      /*Address*/
      sendbuffer[1]=(uint8_t)(app_address >> 24);
      sendbuffer[2]=(uint8_t)(app_address >> 16);
      sendbuffer[3]=(uint8_t)(app_address >> 8);
      sendbuffer[4]=(uint8_t)(app_address >> 0);

      uint32_t sendsize = sendbuffer_Size > 2048 ? 2048 : sendbuffer_Size;
      sendbuffer[5] = (uint8_t)(sendsize >> 8);
      sendbuffer[6] = (uint8_t)(sendsize >> 0);
      sendbuffer[7]=0xFF;
      M5_LOGW("write data: %d / %d", buffer_index, TOTAL_APPLICATION_SIZE); 
      // send a packet
      if (!M5.In_I2C.start(i2c_bootloader_addr, false, i2c_freq)
        || !M5.In_I2C.write(sendbuffer, 8)
        || !M5.In_I2C.write(&appHexData[buffer_index], sendsize)
      )  {
        M5_LOGE("Failed to send firmware data.");
        flg_error = true;
        break;
      }
      M5.In_I2C.stop();
      buffer_index += sendsize;

      // Send data decrement
      sendbuffer_Size -= sendsize;
      // Send address increasing
      app_address += sendsize;

      if (!waitingBootloader()) {
        M5_LOGE("Failed to wait bootloader.");
        flg_error = true;
        break;
      }
    } while (sendbuffer_Size > 0);
    if (flg_error) {
      M5_LOGE("Failed to send firmware data.");
      return false;
    }

    // send OPC_USRCD(0x77) jump to application
    if (!writeBootloader( OPC_USRCD )) {
      M5_LOGE("Failed to send OPC_USRCD.");
      return false;
    }
    M5_LOGI("upgrade done\n");
  }
  return true;
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

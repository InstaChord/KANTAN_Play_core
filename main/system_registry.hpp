// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_SYSTEM_REGISTRY_HPP
#define KANPLAY_SYSTEM_REGISTRY_HPP

#include <ArduinoJson.h>

#include <assert.h>

#include "registry.hpp"
#include "common_define.hpp"

#include <string.h>
#include <stdio.h>
#include <vector>
#include <map>
#include <algorithm>
#include <array>
#include <mutex>

namespace kanplay_ns {
//-------------------------------------------------------------------------

class system_registry_t;

extern system_registry_t* system_registry;

class system_registry_t {
    bool save_impl(const char* filename);
    bool loadInternal(def::app::data_type_t type);

    bool saveSettingInternal(JsonVariant& json);
    bool loadSettingInternal(JsonVariant& json);
    // bool saveSetting(void);
    // bool saveResume(void);
    bool loadSetting(void);
    bool loadResume(void);
    bool loadMapping(void);
    size_t saveSettingJSON(uint8_t* data, size_t data_length);
    size_t saveMappingJSON(uint8_t* data, size_t data_length);
    size_t saveResumeJSON(uint8_t* data, size_t data_length);
    bool loadSettingJSON(const uint8_t* data, size_t data_length);
    bool loadMappingJSON(const uint8_t* data, size_t data_length);
    bool loadResumeJSON(const uint8_t* data, size_t data_length);
    uint32_t calcSettingCRC32(void) const;
    uint32_t calcMappingCRC32(void) const;
    uint32_t calcResumeCRC32(void) const;
    uint32_t calcSongCRC32(void) const;
    uint32_t calcKmapCRC32(void) const;

    uint32_t _last_setting_crc32 = 0;
    uint32_t _last_mapping_crc32 = 0;
    uint32_t _last_resume_crc32 = 0;
public:
    void init(void);

    // Sampler performance policy: 240MHz while sound/input/communication is
    // active, 160MHz after a quiet interval. Other firmware ignores this.
    void setSamplerPerformanceClock(bool performance);

    void updateCRC32(void);
    void updateUnchangedSongCRC32(void) { unchanged_song_crc32 = calcSongCRC32(); }
    void updateUnchangedKmapCRC32(void) { unchanged_kmap_crc32 = calcKmapCRC32(); }

    void updateControlMapping(void);

    // Mapping 1 (Device) を工場出荷時の既定値に戻す
    void resetDeviceMapping(void);

    void reset(void);
    bool save(void);
    bool load(void);

    void syncParams(void);

    struct reg_working_command_t {
        void set(const def::command::command_param_t& command_param);
        void clear(const def::command::command_param_t& command_param);
        bool check(const def::command::command_param_t& command_param) const;
        uint32_t getChangeCounter(void) const { return _working_command_change_counter; }

#if __has_include (<freertos/FreeRTOS.h>)
        void setNotifyTaskHandle(TaskHandle_t handle);
protected:
        void _execNotify(void) const { if (_task_handle != nullptr) { xTaskNotify(_task_handle, (uint32_t)this, eNotifyAction::eSetValueWithOverwrite); } }
        TaskHandle_t _task_handle = nullptr;
#else
protected:
        void _execNotify(void) const {}
#endif
        uint32_t _working_command_change_counter = 0;
    } working_command;

    // ユーザー設定で変更される情報
    // ユーザーが設定する情報で、終了時に保存され起動時に再現される情報
    struct reg_user_setting_t : public registry_t {
        reg_user_setting_t(void) : registry_t(16, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            LED_BRIGHTNESS,
            DISPLAY_BRIGHTNESS,
            LANGUAGE,
            GUI_DETAIL_MODE,
            GUI_WAVE_VIEW,
            MASTER_VOLUME,
            MIDI_MASTER_VOLUME,
            ADC_MIC_AMP,
            OFFBEAT_STYLE,
            IMU_VELOCITY_LEVEL,
            CHATTERING_THRESHOLD,
            EXT_MIDI_VELOCITY,
            TIMEZONE,
            GUIDE_SOUND,
        };

        // ディスプレイの明るさ
        void setDisplayBrightness(uint8_t brightness) { set8(DISPLAY_BRIGHTNESS, brightness); }
        uint8_t getDisplayBrightness(void) const { return get8(DISPLAY_BRIGHTNESS); }

        // LEDの明るさ
        void setLedBrightness(uint8_t brightness) { set8(LED_BRIGHTNESS, brightness); }
        uint8_t getLedBrightness(void) const { return get8(LED_BRIGHTNESS); }

        // 言語設定
        void setLanguage(def::lang::language_t lang) { set8(LANGUAGE, static_cast<uint8_t>(lang)); }
        def::lang::language_t getLanguage(void) const { return static_cast<def::lang::language_t>(get8(LANGUAGE)); }

        // GUIの詳細/簡易モード
        void setGuiDetailMode(bool enabled) { set8(GUI_DETAIL_MODE, enabled); }
        bool getGuiDetailMode(void) const { return get8(GUI_DETAIL_MODE); }

        // GUIの波形モニター表示
        void setGuiWaveView(bool enabled) { set8(GUI_WAVE_VIEW, enabled); }
        bool getGuiWaveView(void) const { return get8(GUI_WAVE_VIEW); }

        // 現在の全体ボリューム (0-100)
        void setMasterVolume(uint8_t volume) { set8(MASTER_VOLUME, volume < 100 ? volume : 100); }
        uint8_t getMasterVolume(void) const { return get8(MASTER_VOLUME); }

        // 現在のMIDIマスターボリューム (0-127)
        void setMIDIMasterVolume(uint8_t volume) { set8(MIDI_MASTER_VOLUME, volume); }
        uint8_t getMIDIMasterVolume(void) const { return get8(MIDI_MASTER_VOLUME); }

        // 現在のADCマイクアンプレベル (SAMからES8388への入力時)
        void setADCMicAmp(uint8_t level) { set8(ADC_MIC_AMP, level); }
        uint8_t getADCMicAmp(void) const { return get8(ADC_MIC_AMP); }

        // オフビート演奏の方法 (false=自動 / true=手動(ボタン離した時) )
        void setOffbeatStyle(def::play::offbeat_style_t style) {
            auto tmp = (uint8_t)std::min<uint8_t>(def::play::offbeat_style_t::offbeat_max - 1, std::max<uint8_t>(def::play::offbeat_style_t::offbeat_min + 1, style));
            set8(OFFBEAT_STYLE, tmp);
        }
        def::play::offbeat_style_t getOffbeatStyle(void) const { return (def::play::offbeat_style_t)get8(OFFBEAT_STYLE); }

        // IMUベロシティの強さ (0はIMUベロシティ不使用で固定値動作)
        void setImuVelocityLevel(uint8_t ratio) { set8(IMU_VELOCITY_LEVEL, ratio); }
        uint8_t getImuVelocityLevel(void) const { return get8(IMU_VELOCITY_LEVEL); }

        // 外部MIDIのベロシティ値を使用するか否か
        void setExtMidiVelocity(bool enabled) { set8(EXT_MIDI_VELOCITY, enabled); }
        bool getExtMidiVelocity(void) const { return get8(EXT_MIDI_VELOCITY); }

        // チャタリング防止のための閾値(msec)
        void setChatteringThreshold(uint8_t msec) { set8(CHATTERING_THRESHOLD, msec); }
        uint8_t getChatteringThreshold(void) const { return get8(CHATTERING_THRESHOLD); }

        void setTimeZone15min(int8_t offset);
        int8_t getTimeZone15min(void) const { return get8(TIMEZONE); }
        void setTimeZone(int8_t offset) { setTimeZone15min(offset * 4); }
        int8_t getTimeZone(void) const { return get8(TIMEZONE) / 4; }

        // ガイド音（メニュー操作・パートオンオフ）のON/OFF
        void setGuideSound(bool enabled) { set8(GUIDE_SOUND, enabled ? 1 : 0); }
        bool getGuideSound(void) const { return get8(GUIDE_SOUND) != 0; }
    } user_setting;

    // MIDIポートに関する設定情報
    struct reg_midi_port_setting_t : public registry_t {
        reg_midi_port_setting_t(void) : registry_t(9, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            PORT_C_MIDI,
            BLE_MIDI,
            USB_MIDI,
            INSTACHORD_LINK_PORT,
            INSTACHORD_LINK_DEV,
            INSTACHORD_LINK_STYLE,
            USB_POWER_ENABLED, // USB給電 オン・オフ
            USB_MODE,          // USBモード(Host/Device)
            EXTERNAL_INPUT_SOURCE, // 排他的な主入力 (Sampler互換構成)
        };
        void setPortCMIDI(def::command::ex_midi_mode_t mode) { set8(PORT_C_MIDI, static_cast<uint8_t>(mode)); }
        def::command::ex_midi_mode_t getPortCMIDI(void) const { return static_cast<def::command::ex_midi_mode_t>(get8(PORT_C_MIDI)); }

        void setBLEMIDI(def::command::ex_midi_mode_t mode) { set8(BLE_MIDI, static_cast<uint8_t>(mode)); }
        def::command::ex_midi_mode_t getBLEMIDI(void) const { return static_cast<def::command::ex_midi_mode_t>(get8(BLE_MIDI)); }

        void setUSBMIDI(def::command::ex_midi_mode_t mode) { set8(USB_MIDI, static_cast<uint8_t>(mode)); }
        def::command::ex_midi_mode_t getUSBMIDI(void) const { return static_cast<def::command::ex_midi_mode_t>(get8(USB_MIDI)); }

        void setInstaChordLinkPort(def::command::instachord_link_port_t mode) { set8(INSTACHORD_LINK_PORT, static_cast<uint8_t>(mode)); }
        def::command::instachord_link_port_t getInstaChordLinkPort(void) const { return static_cast<def::command::instachord_link_port_t>(get8(INSTACHORD_LINK_PORT)); }

        void setInstaChordLinkDev(def::command::instachord_link_dev_t device) { set8(INSTACHORD_LINK_DEV, static_cast<uint8_t>(device)); }
        def::command::instachord_link_dev_t getInstaChordLinkDev(void) const { return static_cast<def::command::instachord_link_dev_t>(get8(INSTACHORD_LINK_DEV)); }

        void setInstaChordLinkStyle(def::command::instachord_link_style_t style) { set8(INSTACHORD_LINK_STYLE, static_cast<uint8_t>(style)); }
        def::command::instachord_link_style_t getInstaChordLinkStyle(void) const { return static_cast<def::command::instachord_link_style_t>(get8(INSTACHORD_LINK_STYLE)); }

        void setUSBPowerEnabled(bool enabled) { set8(USB_POWER_ENABLED, enabled); }
        bool getUSBPowerEnabled(void) const { return static_cast<bool>(get8(USB_POWER_ENABLED)); }

        void setUSBMode(def::command::usb_mode_t mode) { set8(USB_MODE, static_cast<uint8_t>(mode)); }
        def::command::usb_mode_t getUSBMode(void) const { return static_cast<def::command::usb_mode_t>(get8(USB_MODE)); }

        void setExternalInputSource(def::command::external_input_source_t source) {
            if (source >= def::command::external_input_source_max) {
                source = def::command::external_input_off;
            }

            set8(EXTERNAL_INPUT_SOURCE, static_cast<uint8_t>(source));
        }
        // Boot only, before the MIDI/I2C workers start. Selection at runtime
        // only stages the saved source; it must not start a second stack.
        void applyExternalInputSourceAtBoot(void) {
            const auto source = getExternalInputSource();
            _reg_data_8[BLE_MIDI] = def::command::midi_off;
            _reg_data_8[USB_MIDI] = def::command::midi_off;
            _reg_data_8[USB_POWER_ENABLED] = false;
            _reg_data_8[USB_MODE] = def::command::usb_device;
            switch (source) {
            case def::command::external_input_usb_midi_host:
                _reg_data_8[USB_MODE] = def::command::usb_host;
                _reg_data_8[USB_MIDI] = def::command::midi_input;
                // task_i2c delays physical VBUS until the host stack is ready.
                _reg_data_8[USB_POWER_ENABLED] = true;
                break;
            case def::command::external_input_usb_midi_device:
                _reg_data_8[USB_MIDI] = def::command::midi_input;
                break;
            case def::command::external_input_ble_midi:
                _reg_data_8[BLE_MIDI] = def::command::midi_input;
                break;
            case def::command::external_input_off:
            default:
                break;
            }
        }
        def::command::external_input_source_t getExternalInputSource(void) const {
            auto source = static_cast<def::command::external_input_source_t>(get8(EXTERNAL_INPUT_SOURCE));
            return source < def::command::external_input_source_max
                 ? source : def::command::external_input_off;
        }
    } midi_port_setting;

    // 実行時に変化する保存されない情報 (設定画面が存在しない可変情報)
    struct reg_runtime_info_t : public registry_t {
        reg_runtime_info_t(void) : registry_t(64, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            PROGRESSION_POS_L,
            PROGRESSION_POS_H,
            PART_EFFECT_1,
            PART_EFFECT_2,
            PART_EFFECT_3,
            PART_EFFECT_4,
            PART_EFFECT_5,
            PART_EFFECT_6,
            BATTERY_LEVEL,
            BATTERY_CHARGING,
            WIFI_CLIENT_COUNT,
            WIFI_OTA_PROGRESS,
            WIFI_STA_INFO,
            WIFI_AP_INFO,
            BLUETOOTH_INFO,
            SNTP_SYNC,
            SONG_MODIFIED,
            HEADPHONE_ENABLED,
            POWER_OFF,
            MASTER_KEY,
            PRESS_VELOCITY,
            PLAY_SLOT,
            PLAY_MODE,
            GUI_FLAG_MENU,
            GUI_FLAG_PARTEDIT,
            GUI_FLAG_SONGRECORDING,
            GUI_PERFORM_STYLE,
            NOTE_SCALE,
            CHORD_AUTOPLAY_STATE,
            SUSTAIN_STATE,
            EDIT_VELOCITY,
            BUTTON_MAPPING_SWITCH,
            DEVELOPER_MODE,
            MIDI_CHVOL_MAX,
            MIDI_PORT_STATE_PC,  // ポートC
            MIDI_PORT_STATE_BLE, // BLE MIDI
            MIDI_PORT_STATE_USB, // USB MIDI
            MIDI_TX_COUNT_PC,
            MIDI_RX_COUNT_PC,
            MIDI_TX_COUNT_BLE,
            MIDI_RX_COUNT_BLE,
            MIDI_TX_COUNT_USB,
            MIDI_RX_COUNT_USB,
            CHORD_MINOR_SWAP_PRESS_COUNT,
            CHORD_SEMITONE_FLAT_PRESS_COUNT,
            CHORD_SEMITONE_SHARP_PRESS_COUNT,
            PREVIEW_PLAY_STATE,
            SONG_AUTO_REPEAT,
            SONG_PART_OPERATION,
            PERFORMANCE_ACTIVE,
            FIRMWARE_CHANNEL,
            WIFI_CONNECTIVITY,
        };

        // 音が鳴ったパートへの発光エフェクト設定
        void hitPartEffect(uint8_t part_index) { set8(PART_EFFECT_1 + part_index, getPartEffect(part_index) + 1); }
        uint8_t getPartEffect(uint8_t part_index) const { return get8(PART_EFFECT_1 + part_index); }

        // バッテリー残量
        void setBatteryLevel(uint8_t level) { set8(BATTERY_LEVEL, level); }
        uint8_t getBatteryLevel(void) const { return get8(BATTERY_LEVEL); }

        // バッテリー充電状態
        void setBatteryCharging(bool charging) { set8(BATTERY_CHARGING, charging); }
        bool getBatteryCharging(void) const { return get8(BATTERY_CHARGING); }

        // WiFiクライアント数
        void setWiFiStationCount(uint8_t count) { set8(WIFI_CLIENT_COUNT, count); }
        uint8_t getWiFiStationCount(void) const { return get8(WIFI_CLIENT_COUNT); }

        // WiFi OTAアップデート進捗
        void setWiFiOtaProgress(uint8_t update) { set8(WIFI_OTA_PROGRESS, update); }
        uint8_t getWiFiOtaProgress(void) const { return get8(WIFI_OTA_PROGRESS); }

        void setWiFiConnectivity(def::command::wifi_connectivity_state_t state) {
            set8(WIFI_CONNECTIVITY, static_cast<uint8_t>(state));
        }
        def::command::wifi_connectivity_state_t getWiFiConnectivity(void) const {
            return static_cast<def::command::wifi_connectivity_state_t>(get8(WIFI_CONNECTIVITY));
        }

        // OTAでインストールするファームウェアチャンネル
        void setFirmwareChannel(def::command::firmware_channel_t channel) { set8(FIRMWARE_CHANNEL, static_cast<uint8_t>(channel)); }
        def::command::firmware_channel_t getFirmwareChannel(void) const { return static_cast<def::command::firmware_channel_t>(get8(FIRMWARE_CHANNEL)); }

        // WiFi STAモード情報
        void setWiFiSTAInfo(def::command::wifi_sta_info_t state) { set8(WIFI_STA_INFO, static_cast<uint8_t>(state)); }
        def::command::wifi_sta_info_t getWiFiSTAInfo(void) const { return static_cast<def::command::wifi_sta_info_t>(get8(WIFI_STA_INFO)); }

        // WiFi APモード情報
        void setWiFiAPInfo(def::command::wifi_ap_info_t state) { set8(WIFI_AP_INFO, static_cast<uint8_t>(state)); }
        def::command::wifi_ap_info_t getWiFiAPInfo(void) const { return static_cast<def::command::wifi_ap_info_t>(get8(WIFI_AP_INFO)); }

        // SNTP同期状態
        void setSntpSync(bool sync) { set8(SNTP_SYNC, sync); }
        bool getSntpSync(void) const { return get8(SNTP_SYNC); }

        // 未保存の変更があるか否か
        bool getSongModified(void) const { return get8(SONG_MODIFIED); }
        void setSongModified(bool flg) { set8(SONG_MODIFIED, flg); }

        // 現在のヘッドホンジャック挿抜状態
        void setHeadphoneEnabled(uint8_t inserted) { set8(HEADPHONE_ENABLED, inserted); }
        uint8_t getHeadphoneEnabled(void) const { return get8(HEADPHONE_ENABLED); }

        void setPowerOff(uint8_t state) { set8(POWER_OFF, state); }
        uint8_t getPowerOff(void) const { return get8(POWER_OFF); }


        // 現在の全体キー
        void setMasterKey(uint8_t key) { set8(MASTER_KEY, key); }
        uint8_t getMasterKey(void) const { return get8(MASTER_KEY); }

        // 現在の使用スロット番号
        void setPlaySlot(uint8_t slot_index) {
            if (slot_index < system_registry->song_data.song_info.getNumSlot()) {
                set8(PLAY_SLOT, slot_index);
                system_registry->current_slot = &(system_registry->song_data.slot[slot_index]);
            }
        }
        uint8_t getPlaySlot(void) const { return get8(PLAY_SLOT); }

        def::gui_mode_t getGuiMode(void) const {
            if (getGuiFlag_Menu()) { return def::gui_mode_t::gm_menu; }
            if (getGuiFlag_PartEdit()) { return def::gui_mode_t::gm_part_edit; }
            if (getGuiFlag_SongRecording()) { return def::gui_mode_t::gm_song_recording; }
            switch (getPlayMode()) {
            case def::playmode::pm_auto_song:
            case def::playmode::pm_guide_play:
            case def::playmode::pm_free_guide:
                return def::gui_mode_t::gm_song_play;
            default:
                break;
            }
            switch (getGui_PerformStyle()) {
            default:
            case def::perform_style_t::ps_chord:
                return def::gui_mode_t::gm_perform_chord;
            case def::perform_style_t::ps_note:
                return def::gui_mode_t::gm_perform_note;
            case def::perform_style_t::ps_drum:
                return def::gui_mode_t::gm_perform_drum;
            }
            return def::gui_mode_t::gm_unknown;
        }

        // メニューUIを表示しているか否か
        void setGuiFlag_Menu(bool visible) { set8(GUI_FLAG_MENU, visible); }
        bool getGuiFlag_Menu(void) const { return get8(GUI_FLAG_MENU); }

        // パート編集モードか否か
        void setGuiFlag_PartEdit(bool enabled) { set8(GUI_FLAG_PARTEDIT, enabled); }
        bool getGuiFlag_PartEdit(void) const { return get8(GUI_FLAG_PARTEDIT); }

        // シーケンス編集モードか否か
        void setGuiFlag_SongRecording(bool enabled) { set8(GUI_FLAG_SONGRECORDING, enabled); }
        bool getGuiFlag_SongRecording(void) const { return get8(GUI_FLAG_SONGRECORDING); }

        void setGui_PerformStyle(def::perform_style_t style) { set8(GUI_PERFORM_STYLE, static_cast<uint8_t>(style)); }
        def::perform_style_t getGui_PerformStyle(void) const { return static_cast<def::perform_style_t>(get8(GUI_PERFORM_STYLE)); }

        // ノート演奏時のスケール
        void setNoteScale(uint8_t scale) { set8(NOTE_SCALE, scale); }
        uint8_t getNoteScale(void) const { return get8(NOTE_SCALE); }

        void setPlayMode(def::playmode::playmode_t mode) { set8(PLAY_MODE, mode); }
        def::playmode::playmode_t getPlayMode(void) const { return (def::playmode::playmode_t)get8(PLAY_MODE); }

        // IMUによるボタン押下時のベロシティ
        void setPressVelocity(uint8_t level) { set8(PRESS_VELOCITY, level); }
        uint8_t getPressVelocity(void) const { return get8(PRESS_VELOCITY); }

        // 自動ビート演奏状態
        void setAutoplayState(def::play::auto_play_state_t mode) { set8(CHORD_AUTOPLAY_STATE, mode); }
        def::play::auto_play_state_t getAutoplayState(void) const { return (def::play::auto_play_state_t)get8(CHORD_AUTOPLAY_STATE); }
        def::play::auto_play_state_t getGuiAutoplayState(void) const {
            auto res = def::play::auto_play_state_t::auto_play_none;
            auto seq = system_registry->currentPlayMode();

            // ソング記録モードはガイド演奏モードと同等扱いとする
            if (getGuiFlag_SongRecording()) { seq = def::playmode::pm_guide_play; }

            if (seq == def::playmode::pm_beat_play || seq == def::playmode::pm_auto_song) {
                res = (def::play::auto_play_state_t)get8(CHORD_AUTOPLAY_STATE);
                // ビート演奏モードと自動演奏モード時はnoneは無効化してwaitingにする
                if (res == def::play::auto_play_state_t::auto_play_none) {
                    res = def::play::auto_play_state_t::auto_play_waiting;
                }
            } else if (seq == def::playmode::pm_guide_play) {
                res = (def::play::auto_play_state_t)get8(CHORD_AUTOPLAY_STATE);
                // ガイド演奏モードとシーケンス編集モード時はビートモード以外は無効化
                if (res != def::play::auto_play_state_t::auto_play_beatmode) {
                    res = def::play::auto_play_state_t::auto_play_none;
                }
            }
            return res;
        }

        // 直近の演奏後タイマーが有効かどうか。音声ガイド等の抑制判定に使う。
        void setPerformanceActive(bool active) { set8(PERFORMANCE_ACTIVE, active); }
        bool getPerformanceActive(void) const { return get8(PERFORMANCE_ACTIVE); }

        void setSustainState(def::play::sustain_state_t state) { set8(SUSTAIN_STATE, state); }
        def::play::sustain_state_t getSustainState(void) const { return (def::play::sustain_state_t)get8(SUSTAIN_STATE); }

        // 編集時のベロシティ
        void setEditVelocity(int8_t level) { set8(EDIT_VELOCITY, level); }
        int8_t getEditVelocity(void) const { return (int8_t)get8(EDIT_VELOCITY); }

        // ボタンマッピング切り替え
        void setButtonMappingSwitch(uint8_t map_index) { set8(BUTTON_MAPPING_SWITCH, map_index); }
        uint8_t getButtonMappingSwitch(void) const { return get8(BUTTON_MAPPING_SWITCH); }
        bool getSubButtonSwap(void) const { return 1 == get8(BUTTON_MAPPING_SWITCH); }

        // 開発者モード
        void setDeveloperMode(bool enabled) { set8(DEVELOPER_MODE, enabled); }
        bool getDeveloperMode(void) const { return get8(DEVELOPER_MODE); }

        // MIDIチャンネルボリュームの最大値
        // ※ Instachord Link時に下げる。通常時は127とする
        void setMIDIChannelVolumeMax(uint8_t max_volume) { set8(MIDI_CHVOL_MAX, max_volume); }
        uint8_t getMIDIChannelVolumeMax(void) const { return get8(MIDI_CHVOL_MAX); }

        // MIDIポートCの状態
        void setMidiPortStatePC(def::command::midiport_info_t mode) { set8(MIDI_PORT_STATE_PC, static_cast<uint8_t>(mode)); }
        def::command::midiport_info_t getMidiPortStatePC(void) const { return static_cast<def::command::midiport_info_t>(get8(MIDI_PORT_STATE_PC)); }

        // BLE MIDIの状態
        void setMidiPortStateBLE(def::command::midiport_info_t mode) { set8(MIDI_PORT_STATE_BLE, static_cast<uint8_t>(mode)); }
        def::command::midiport_info_t getMidiPortStateBLE(void) const { return static_cast<def::command::midiport_info_t>(get8(MIDI_PORT_STATE_BLE)); }

        // USB MIDIの状態
        void setMidiPortStateUSB(def::command::midiport_info_t mode) { set8(MIDI_PORT_STATE_USB, static_cast<uint8_t>(mode)); }
        def::command::midiport_info_t getMidiPortStateUSB(void) const { return static_cast<def::command::midiport_info_t>(get8(MIDI_PORT_STATE_USB)); }

        // ポートC MIDI送信カウンタ
        void setMidiTxCountPC(uint8_t count) { set8(MIDI_TX_COUNT_PC, count); }
        uint8_t getMidiTxCountPC(void) const { return get8(MIDI_TX_COUNT_PC); }

        // BLE MIDI送信カウンタ
        void setMidiTxCountBLE(uint8_t count) { set8(MIDI_TX_COUNT_BLE, count); }
        uint8_t getMidiTxCountBLE(void) const { return get8(MIDI_TX_COUNT_BLE); }

        // USB MIDI送信カウンタ
        void setMidiTxCountUSB(uint8_t count) { set8(MIDI_TX_COUNT_USB, count); }
        uint8_t getMidiTxCountUSB(void) const { return get8(MIDI_TX_COUNT_USB); }

        // ポートC MIDI受信カウンタ
        void setMidiRxCountPC(uint8_t count) { set8(MIDI_RX_COUNT_PC, count); }
        uint8_t getMidiRxCountPC(void) const { return get8(MIDI_RX_COUNT_PC); }

        // BLE MIDI受信カウンタ
        void setMidiRxCountBLE(uint8_t count) { set8(MIDI_RX_COUNT_BLE, count); }
        uint8_t getMidiRxCountBLE(void) const { return get8(MIDI_RX_COUNT_BLE); }

        // USB MIDI受信カウンタ
        void setMidiRxCountUSB(uint8_t count) { set8(MIDI_RX_COUNT_USB, count); }
        uint8_t getMidiRxCountUSB(void) const { return get8(MIDI_RX_COUNT_USB); }

        // 現在のコード進行の再生位置
        uint16_t getProgressionPosition(void) const { return get16(PROGRESSION_POS_L); }
        void setProgressionPosition(uint16_t step_index) { set16(PROGRESSION_POS_L, step_index); }

        void addChordMinorSwapPressCount(int count) {
            count += get8(CHORD_MINOR_SWAP_PRESS_COUNT);
            if (count < 0) { count = 0; } else if (count > 255) { count = 255; }
            set8(CHORD_MINOR_SWAP_PRESS_COUNT, count);
        }
        void clearChordMinorSwapPressCount(void) { set8(CHORD_MINOR_SWAP_PRESS_COUNT, 0); }
        uint8_t getChordMinorSwapPressCount(void) const { return get8(CHORD_MINOR_SWAP_PRESS_COUNT); }
        void addChordSemitoneFlatPressCount(int count) {
            count += get8(CHORD_SEMITONE_FLAT_PRESS_COUNT);
            if (count < 0) { count = 0; } else if (count > 255) { count = 255; }
            set8(CHORD_SEMITONE_FLAT_PRESS_COUNT, count);
        }
        void clearChordSemitoneFlatPressCount(void) { set8(CHORD_SEMITONE_FLAT_PRESS_COUNT, 0); }
        uint8_t getChordSemitoneFlatPressCount(void) const { return get8(CHORD_SEMITONE_FLAT_PRESS_COUNT); }
        void addChordSemitoneSharpPressCount(int count) {
            count += get8(CHORD_SEMITONE_SHARP_PRESS_COUNT);
            if (count < 0) { count = 0; } else if (count > 255) { count = 255; }
            set8(CHORD_SEMITONE_SHARP_PRESS_COUNT, count);
        }
        void clearChordSemitoneSharpPressCount(void) { set8(CHORD_SEMITONE_SHARP_PRESS_COUNT, 0); }
        uint8_t getChordSemitoneSharpPressCount(void) const { return get8(CHORD_SEMITONE_SHARP_PRESS_COUNT); }
        int getChordSemitoneShift(void) {
            int res = 0;
            if (get8(CHORD_SEMITONE_FLAT_PRESS_COUNT)) { --res; }
            if (get8(CHORD_SEMITONE_SHARP_PRESS_COUNT)) { ++res; }
            return res;
        }

        // プレビュー演奏状態
        void setPreviewPlay(bool preview) { set8(PREVIEW_PLAY_STATE, preview); }
        bool getPreviewPlay(void) const { return get8(PREVIEW_PLAY_STATE); }

        // シーケンス自動リピート
        void setSongAutoRepeat(bool enabled) { set8(SONG_AUTO_REPEAT, enabled); }
        bool getSongAutoRepeat(void) const { return get8(SONG_AUTO_REPEAT); }

        // シーケンス演奏時のパート操作 (Auto / Manual)
        void setSongPartOperation(def::play::song_part_operation_t mode) { set8(SONG_PART_OPERATION, mode); }
        def::play::song_part_operation_t getSongPartOperation(void) const { return (def::play::song_part_operation_t)get8(SONG_PART_OPERATION); }
    } runtime_info;

    struct reg_popup_notify_t : public registry_t {
        reg_popup_notify_t(void) : registry_t(8, 4, DATA_SIZE_8) {}
        enum category_t : uint16_t {
            ERROR_NOTIFY = 0x00,
            SUCCESS_NOTIFY = 0x01,
            MESSAGE = 0x02,
        };
        void setPopup(bool is_success, def::notify_type_t notify) { set8(is_success ? SUCCESS_NOTIFY : ERROR_NOTIFY, notify, true); }
        void setMessage(def::notify_type_t notify) { set8(MESSAGE, notify, true); }
        bool getPopupHistory(history_code_t &code, def::notify_type_t &notify_type, category_t &category) {
            auto history = getHistory(code);
            if (history == nullptr) { return false; }
            notify_type = static_cast<def::notify_type_t>(history->value);
            category = static_cast<category_t>(history->index);
            return true;
        }
    } popup_notify;

    struct reg_popup_qr_t: public registry_t {
        reg_popup_qr_t(void) : registry_t(8, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            QRCODE_TYPE = 0x00,
        };
        void setQRCodeType(def::qrcode_type_t qrtype) { set8(QRCODE_TYPE, qrtype); }
        def::qrcode_type_t getQRCodeType(void) { return static_cast<def::qrcode_type_t>(get8(QRCODE_TYPE)); }
    } popup_qr;

    struct reg_wifi_control_t : public registry_t {
        reg_wifi_control_t(void) : registry_t(8, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            WIFIMODE,
            OPERATION,
            WEBSERVER,
        };

        // WiFi APモード
        void setWifiMode(def::command::wifi_mode_t ctrl) { set8(WIFIMODE, static_cast<uint8_t>(ctrl)); }
        def::command::wifi_mode_t getWifiMode(void) const { return static_cast<def::command::wifi_mode_t>(get8(WIFIMODE)); }

        // WiFi 操作指示
        void setOperation(def::command::wifi_operation_t operation) { set8(OPERATION, static_cast<uint8_t>(operation)); }
        def::command::wifi_operation_t getOperation(void) const { return static_cast<def::command::wifi_operation_t>(get8(OPERATION)); }

        void setWebServerMode(def::command::webserver_mode_t value) { set8(WEBSERVER, static_cast<uint8_t>(value)); }
        def::command::webserver_mode_t getWebServerMode(void) const { return static_cast<def::command::webserver_mode_t>(get8(WEBSERVER)); }
    } wifi_control;

    struct reg_sub_button_t : public registry_t {
        reg_sub_button_t(void) : registry_t(64, 0, DATA_SIZE_32) {}
        enum index_t : uint16_t {
            SUB_BUTTON_1_COMMAND,
            SUB_BUTTON_2_COMMAND = 0x04,
            SUB_BUTTON_3_COMMAND = 0x08,
            SUB_BUTTON_4_COMMAND = 0x0C,
            SUB_BUTTON_5_COMMAND = 0x10,
            SUB_BUTTON_6_COMMAND = 0x14,
            SUB_BUTTON_7_COMMAND = 0x18,
            SUB_BUTTON_8_COMMAND = 0x1C,
            SUB_BUTTON_1_COLOR = 0x20,
        };
        // サブボタンのコマンド
        void setCommandParamArray(uint8_t index, const def::command::command_param_array_t& pair) { set32(SUB_BUTTON_1_COMMAND + index * 4, pair.raw32_0); }
        def::command::command_param_array_t getCommandParamArray(uint8_t index) const { return static_cast<def::command::command_param_array_t>(get32(SUB_BUTTON_1_COMMAND + index * 4)); }
        void setSubButtonColor(uint8_t index, uint32_t color) { set32(SUB_BUTTON_1_COLOR + index * 4, color); }
        uint32_t getSubButtonColor(uint8_t index) const { return get32(SUB_BUTTON_1_COLOR + index * 4); }
    };

    struct reg_task_status_t : public registry_t {
        reg_task_status_t(void) : registry_t(64, 0, DATA_SIZE_32) {}
        enum bitindex_t : uint32_t {
            TASK_SPI,
            TASK_I2S,
            TASK_I2C,
            TASK_COMMANDER,
            TASK_OPERATOR,
            TASK_KANTANPLAY,
            TASK_MIDI_INTERNAL,
            TASK_MIDI_EXTERNAL,
            TASK_MIDI_USB,
            TASK_MIDI_BLE,
            TASK_WIFI,
            MAX_TASK,
        };
        enum index_t : uint16_t {
            TASK_STATUS = 0x00,
            LOW_POWER_COUNTER = 0x04,
            HIGH_POWER_COUNTER = 0x08,
            TASK_SPI_COUNTER = 0x0C,
            TASK_I2S_COUNTER = 0x10,
            TASK_I2C_COUNTER = 0x14,
            TASK_COMMANDER_COUNTER = 0x18,
            TASK_OPERATOR_COUNTER = 0x1C,
            TASK_KANTANPLAY_COUNTER = 0x20,
            TASK_MIDI_INTERNAL_COUNTER = 0x24,
            TASK_MIDI_EXTERNAL_COUNTER = 0x28,
            TASK_MIDI_USB_COUNTER = 0x2C,
            TASK_MIDI_BLE_COUNTER = 0x30,
            TASK_MIDI_WIFI_COUNTER = 0x34,
        };
        void setWorking(bitindex_t index);
        void setSuspend(bitindex_t index);
        bool isWorking(void) const { return get32(TASK_STATUS); }
        uint32_t getLowPowerCounter(void) const { return get32(LOW_POWER_COUNTER); }
        uint32_t getHighPowerCounter(void) const { return get32(HIGH_POWER_COUNTER); }
        uint32_t getWorkingCounter(index_t index) const { return get32(index); }
    };

    struct reg_internal_input_t : public registry_t {
        reg_internal_input_t(void) : registry_t(32, 32, DATA_SIZE_32) {}
        enum index_t : uint16_t {
            BUTTON_BITMASK = 0x00,
            ENC1_VALUE = 0x04,
            ENC2_VALUE = 0x08,
            ENC3_VALUE = 0x0C,
            TOUCH_VALUE = 0x10,
        };
        void setButtonBitmask(uint32_t bitmask) { set32(BUTTON_BITMASK, bitmask); }
        uint32_t getButtonBitmask(void) const { return get32(BUTTON_BITMASK); }
        void setEncValue(uint8_t index, uint32_t value) { set32(ENC1_VALUE + (index * 4), value); }
        uint32_t getEncValue(uint8_t index) const { return get32(ENC1_VALUE + (index * 4)); }
        void setTouchValue(uint16_t x, uint16_t y, bool isPressed) { set32(TOUCH_VALUE, isPressed | x << 1 | y << 17); }
        int16_t getTouchX(void) const { return ((int16_t)get16(TOUCH_VALUE    )) >> 1; }
        int16_t getTouchY(void) const { return ((int16_t)get16(TOUCH_VALUE + 2)) >> 1; }
        bool getTouchPressed(void) const { return get16(TOUCH_VALUE) & 1; }
    };

    struct reg_external_input_t : public registry_t {
        reg_external_input_t(void) : registry_t(8, 8, DATA_SIZE_32) {}
        enum index_t : uint16_t {
            PORTA_BITMASK_BYTE0 = 0x00,
            PORTA_BITMASK_BYTE1 = 0x01,
            PORTA_BITMASK_BYTE2 = 0x02,
            PORTA_BITMASK_BYTE3 = 0x03,
            PORTB_BITMASK_BYTE0 = 0x04,
            PORTB_BITMASK_BYTE1 = 0x05,
            PORTB_BITMASK_BYTE2 = 0x06,
            PORTB_BITMASK_BYTE3 = 0x07,
        };
        void setPortABitmask8(uint8_t index, uint8_t bitmask) { set8(PORTA_BITMASK_BYTE0 + index, bitmask); }
        void setPortBValue8(uint8_t index, uint8_t bitmask) { set8(PORTB_BITMASK_BYTE0 + index, bitmask); }
        uint8_t getPortBValue8(uint8_t index) const { return get8(PORTB_BITMASK_BYTE0 + index); }

        uint32_t getPortAButtonBitmask(void) const { return get32(PORTA_BITMASK_BYTE0); }
        uint32_t getPortBButtonBitmask(void) const { return get32(PORTB_BITMASK_BYTE0); }
    };

    // 外部MIDIの生メッセージ通知。通常アプリのコマンドマッピングとは独立して、
    // サンプラーなどがLearn入力として受け取るために使用する。
    struct reg_midi_input_t : public registry_t {
        reg_midi_input_t(void) : registry_t(8, 32, DATA_SIZE_32) {}
        // registry_t::set32 uses byte offsets, therefore each event kind must
        // be 4-byte aligned.  Keeping separate slots also preserves bursts of
        // Note and CC messages in the 32-entry history ring.
        enum index_t : uint16_t { NOTE_MESSAGE = 0x00, CC_MESSAGE = 0x04 };
        struct message_t {
            index_t type;
            uint8_t status;
            uint8_t number;
            uint8_t value;
        };
        void setNoteMessage(uint8_t status, uint8_t note, uint8_t velocity) {
            pushNoteMessage({ NOTE_MESSAGE, status, note, velocity });
            set32(NOTE_MESSAGE, uint32_t(status) | (uint32_t(note) << 8) | (uint32_t(velocity) << 16), true);
        }
        void setCCMessage(uint8_t status, uint8_t controller, uint8_t value) {
            pushCCMessage({ CC_MESSAGE, status, controller, value });
            set32(CC_MESSAGE, uint32_t(status) | (uint32_t(controller) << 8) | (uint32_t(value) << 16), true);
        }
        bool popMessage(message_t* message) {
            if (message == nullptr) { return false; }
            std::lock_guard<std::mutex> lock(_message_mutex);
            // Musical edges take precedence over continuously streamed CC.
            // Preserve FIFO ordering inside each class so Note Off cannot
            // overtake its matching Note On.
            if (_note_tail != _note_head) {
                *message = _note_queue[_note_tail];
                _note_tail = (_note_tail + 1) % _note_queue.size();
                return true;
            }
            if (_cc_tail != _cc_head) {
                *message = _cc_queue[_cc_tail];
                _cc_tail = (_cc_tail + 1) % _cc_queue.size();
                return true;
            }
            return false;
        }

    private:
        // The registry history is useful for state observation but is not a
        // real-time queue. Keep Note and CC traffic separate so a controller's
        // continuous knobs cannot delay performance Note On/Off edges.
        void pushNoteMessage(const message_t& message) {
            std::lock_guard<std::mutex> lock(_message_mutex);
            const size_t next = (_note_head + 1) % _note_queue.size();
            if (next == _note_tail) {
                // Preserve the most recent release events during an extreme
                // burst rather than blocking the MIDI worker.
                _note_tail = (_note_tail + 1) % _note_queue.size();
            }
            _note_queue[_note_head] = message;
            _note_head = next;
        }
        void pushCCMessage(const message_t& message) {
            std::lock_guard<std::mutex> lock(_message_mutex);
            const size_t next = (_cc_head + 1) % _cc_queue.size();
            if (next == _cc_tail) {
                _cc_tail = (_cc_tail + 1) % _cc_queue.size();
            }
            _cc_queue[_cc_head] = message;
            _cc_head = next;
        }
        std::array<message_t, 64> _note_queue {};
        std::array<message_t, 32> _cc_queue {};
        size_t _note_head = 0;
        size_t _note_tail = 0;
        size_t _cc_head = 0;
        size_t _cc_tail = 0;
        std::mutex _message_mutex;
    };

    struct reg_internal_imu_t : public registry_t {
        reg_internal_imu_t(void) : registry_t(32, 0, DATA_SIZE_32) {}
        enum index_t : uint16_t {
            IMU_STANDARD_DEVIATION = 0x00,
            IMU_ACCEL_X = 0x04,
            IMU_ACCEL_Y = 0x08,
            IMU_ACCEL_Z = 0x0C,
            IMU_SAMPLE_SEQUENCE = 0x10,
            IMU_GYRO_X = 0x14,
            IMU_GYRO_Y = 0x18,
            IMU_GYRO_Z = 0x1C,
        };
        // IMUの加速度の標準偏差
        void setImuStandardDeviation(uint32_t sd) { set32(IMU_STANDARD_DEVIATION, sd); }
        uint32_t getImuStandardDeviation(void) const { return get32(IMU_STANDARD_DEVIATION); }
        // Odd/even sequence lock keeps a cross-core XYZ snapshot coherent
        // without blocking the time-sensitive I2C task.
        void setMotion(int16_t x, int16_t y, int16_t z,
                       int16_t gyro_x, int16_t gyro_y, int16_t gyro_z) {
            uint32_t sequence = (get32(IMU_SAMPLE_SEQUENCE) + 1u) | 1u;
            set32(IMU_SAMPLE_SEQUENCE, sequence);
            set32(IMU_ACCEL_X, (uint32_t)(int32_t)x);
            set32(IMU_ACCEL_Y, (uint32_t)(int32_t)y);
            set32(IMU_ACCEL_Z, (uint32_t)(int32_t)z);
            set32(IMU_GYRO_X, (uint32_t)(int32_t)gyro_x);
            set32(IMU_GYRO_Y, (uint32_t)(int32_t)gyro_y);
            set32(IMU_GYRO_Z, (uint32_t)(int32_t)gyro_z);
            set32(IMU_SAMPLE_SEQUENCE, sequence + 1u);
        }
        bool getMotion(int16_t* x, int16_t* y, int16_t* z,
                       int16_t* gyro_x, int16_t* gyro_y, int16_t* gyro_z,
                       uint32_t* sequence = nullptr) const {
            const uint32_t first = get32(IMU_SAMPLE_SEQUENCE);
            const int16_t sx = (int16_t)(int32_t)get32(IMU_ACCEL_X);
            const int16_t sy = (int16_t)(int32_t)get32(IMU_ACCEL_Y);
            const int16_t sz = (int16_t)(int32_t)get32(IMU_ACCEL_Z);
            const int16_t sgx = (int16_t)(int32_t)get32(IMU_GYRO_X);
            const int16_t sgy = (int16_t)(int32_t)get32(IMU_GYRO_Y);
            const int16_t sgz = (int16_t)(int32_t)get32(IMU_GYRO_Z);
            const uint32_t last = get32(IMU_SAMPLE_SEQUENCE);
            if (first == 0 || (first & 1u) || first != last) { return false; }
            if (x) { *x = sx; }
            if (y) { *y = sy; }
            if (z) { *z = sz; }
            if (gyro_x) { *gyro_x = sgx; }
            if (gyro_y) { *gyro_y = sgy; }
            if (gyro_z) { *gyro_z = sgz; }
            if (sequence) { *sequence = last; }
            return true;
        }
        bool getAccel(int16_t* x, int16_t* y, int16_t* z, uint32_t* sequence = nullptr) const {
            return getMotion(x, y, z, nullptr, nullptr, nullptr, sequence);
        }
    };

    struct reg_rgbled_control_t : public registry_t {
        reg_rgbled_control_t(void) : registry_t(80, 64, DATA_SIZE_32) {}
        void setColor(uint8_t index, uint32_t color) { set32(index * 4, color); }
        uint32_t getColor(uint8_t index) const { return get32(index * 4); }
        void refresh(void) { for (int i = 0; i < def::hw::max_rgb_led; ++i) { set32(i * 4, get32(i * 4), true); } }
    };

    // MIDI出力コントロール
    struct reg_midi_out_control_t : public registry_base_t {
        // 読み出しには非対応、値をセットすると履歴として取得できる
        reg_midi_out_control_t(void) : registry_base_t(256) {}

        void setMessage(uint8_t status, uint8_t data1, uint8_t data2 = 0) {
            set16(status, data1 + (data2 << 8), true);
        }
        void setNoteVelocity(uint8_t channel, uint8_t note, uint8_t value) {
            uint8_t status = 0x80 + ((value & 0x80) >> 3);
            setMessage((status | channel), note, value & 0x7F);
        }
        void setProgramChange(uint8_t channel, uint8_t value) {
            if (_program_number[channel] == value) { return; }
            _program_number[channel] = value;
            uint8_t status = 0xC0;
            setMessage((status | channel), value);
        }
        void setControlChange(uint8_t channel, uint8_t control, uint8_t value) {
            uint8_t status = 0xB0;
            setMessage((status | channel), control, value);
        }
        void setChannelVolume(uint8_t channel, uint8_t value) {
            if (_channel_volume[channel] == value) { return; }
            _channel_volume[channel] = value;
            setControlChange(channel, 7, value);
        }
        uint8_t getProgramChange(uint8_t channel) const {
            return _program_number[channel] & 0x7F;
        }
        uint8_t getChannelVolume(uint8_t channel) const {
            return _channel_volume[channel] & 0x7F;
        }
        void setChannelPan(uint8_t channel, uint8_t value) {
            if (_channel_pan[channel] == value) { return; }
            _channel_pan[channel] = value;
            setControlChange(channel, 10, value);
        }
        uint8_t getChannelPan(uint8_t channel) const {
            return _channel_pan[channel] & 0x7F;
        }
    protected:
        uint8_t _channel_volume[def::midi::channel_max] = { 128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,   };
        uint8_t _program_number[def::midi::channel_max] = { 128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,   };
        uint8_t _channel_pan[def::midi::channel_max]    = { 128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,  128, 128, 128, 128,   };
    } midi_out_control;

    // コード演奏アルペジオパターン
    struct reg_arpeggio_table_t : public registry_t {
        reg_arpeggio_table_t(void) : registry_t(def::app::max_arpeggio_step * 8, 0, data_size_t::DATA_SIZE_8) {}

        // パターンのベロシティ値
        void setVelocity(uint8_t step, uint8_t pitch, int8_t velocity) { set8(step * 8 + pitch, velocity); }
        int8_t getVelocity(uint8_t step, uint8_t pitch) const { return (int8_t)get8(step * 8 + pitch); }
        // 奏法 (sametime / low to high / high to low / mute)
        void setStyle(uint8_t step, def::play::arpeggio_style_t style) { set8(step * 8 + 7, style); }
        def::play::arpeggio_style_t getStyle(uint8_t step) const { return (def::play::arpeggio_style_t)get8(step * 8 + 7); }

        void reset(void) {
            for (int i = 0; i < def::app::max_arpeggio_step * 8; ++i) {
                set8(i, 0);
            }
        }

        void copyFrom(uint8_t dst_step, const reg_arpeggio_table_t &src, uint8_t src_step, uint8_t length) {
            for (int i = 0; i < length; ++i) {
                for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
                    setVelocity(dst_step + i, pitch, src.getVelocity(src_step + i, pitch));
                }
                setStyle(dst_step + i, src.getStyle(src_step + i));
            }
        }

        // ベロシティのパターンが空か否か
        bool isEmpty(void) {
            for (int i = 0; i < def::app::max_arpeggio_step * def::app::max_pitch_with_drum; ++i) {
                if (get8(i) != 0) { return false; }
            }
            return true;
        }
    };
    struct reg_part_info_t : public registry_t {
        reg_part_info_t(void) : registry_t(12, 0, DATA_SIZE_8) {}

        enum index_t : uint16_t {
            PROGRAM_NUMBER,
            VOLUME,
            ANCHOR_STEP,
            LOOP_STEP,
            STROKE_SPEED,
            OCTAVE_OFFSET,
            VOICING,
            ENABLED, // パートの有効/無効
            PAN,     // 左右バランス (-5=L100, 0=C0, +5=R100)
        };
        void setTone(uint8_t program) { set8(PROGRAM_NUMBER, program); }
        uint8_t getTone(void) const { return get8(PROGRAM_NUMBER); }
        bool isDrumPart(void) const { return get8(PROGRAM_NUMBER) == 128; }
        // パートのボリューム
        void setVolume(uint8_t volume) { set8(VOLUME, volume); }
        uint8_t getVolume(void) const { return get8(VOLUME); }
        // ループ禁止が解除されるステップ
        void setAnchorStep(uint8_t step) { set8(ANCHOR_STEP, step); }
        uint8_t getAnchorStep(void) const { return get8(ANCHOR_STEP); }
        // ループ終端ステップ
        void setLoopStep(uint8_t step) { set8(LOOP_STEP, step); }
        uint8_t getLoopStep(void) const { return get8(LOOP_STEP); }
        void setStrokeSpeed(uint8_t msec) { set8(STROKE_SPEED, msec); }
        uint8_t getStrokeSpeed(void) const { return get8(STROKE_SPEED); }
        void setPosition(int8_t offset) { set8(OCTAVE_OFFSET, offset); }
        int getPosition(void) const { return (int8_t)get8(OCTAVE_OFFSET); }
        void setVoicing(uint8_t voicing) { set8(VOICING, voicing); }
        KANTANMusic_Voicing getVoicing(void) const { return (KANTANMusic_Voicing)get8(VOICING); }
        void setEnabled(bool enabled) { set8(ENABLED, enabled); }
        bool getEnabled(void) const { return get8(ENABLED); }
        // 左右バランス (-5=L100, 0=C0, +5=R100)
        void setPan(int8_t pan) { set8(PAN, pan); }
        int8_t getPan(void) const { return (int8_t)get8(PAN); }
        // Pan設定値をCC#10の値(0〜127)に変換して返す
        uint8_t getPanCC(void) const {
            static constexpr const uint8_t table[] = { 0, 13, 26, 39, 52, 64, 76, 89, 102, 115, 127 };
            int idx = getPan() + 5;
            if (idx < 0) { idx = 0; } else if (idx > 10) { idx = 10; }
            return table[idx];
        }

        void reset(void) {
            setTone(0);
            setVolume(100);
            setAnchorStep(0);
            setLoopStep(1);
            setStrokeSpeed(20);
            setPosition(0);
            setVoicing(0);
            setEnabled(true);
            setPan(0);
        }
    };
    struct kanplay_part_t {
        reg_arpeggio_table_t arpeggio;
        reg_part_info_t part_info;
        void init(bool psram = false) {
            arpeggio.init(psram);
            part_info.init(psram);
        }
        void assign(const kanplay_part_t &src) {
            arpeggio.assign(src.arpeggio);
            part_info.assign(src.part_info);
        }
        void reset(void) {
            arpeggio.reset();
            part_info.reset();
        }
        uint32_t crc32(uint32_t crc = 0) const {
            crc = arpeggio.crc32(crc);
            crc = part_info.crc32(crc);
            return crc;
        }
        bool operator== (const kanplay_part_t &src) const {
            return arpeggio == src.arpeggio
                && part_info == src.part_info;
        }
        bool operator!= (const kanplay_part_t &src) const { return !(*this == src); }
    };

    struct reg_slot_info_t : public registry_t {
        reg_slot_info_t(void) : registry_t(6, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            TEMPO_BPM_L,
            TEMPO_BPM_H,
            SWING,
            KEY_OFFSET,
            STEP_PER_BEAT,
            NOTE_PROGRAM,
        };

        // 基準キーに対するオフセット量
        void setKeyOffset(int8_t offset) { set8(KEY_OFFSET, offset); }
        int8_t getKeyOffset(void) const { return get8(KEY_OFFSET); }

        // コード演奏時の１ビートあたりのステップ数 (1~4)
        void setStepPerBeat(uint8_t spb) {
            if (spb < def::app::step_per_beat_min) { spb = def::app::step_per_beat_min; }
            if (spb > def::app::step_per_beat_max) { spb = def::app::step_per_beat_max; }
            set8(STEP_PER_BEAT, spb);
        }
        uint8_t getStepPerBeat(void) const {
            auto spb = get8(STEP_PER_BEAT);
            if (spb < def::app::step_per_beat_min || spb > def::app::step_per_beat_max) {
                spb = def::app::step_per_beat_default;
            }
            return spb;
        }

        void setNoteProgram(uint8_t program) { set8(NOTE_PROGRAM, program); }
        uint8_t getNoteProgram(void) const { return get8(NOTE_PROGRAM); }

        void reset(void) {
            setStepPerBeat(def::app::step_per_beat_default);
            setKeyOffset(0);
            setNoteProgram(0);
        }
    };

    // コード演奏モードにおけるドラムパートのノート番号情報
    struct reg_chord_part_drum_t : public registry_t {
        reg_chord_part_drum_t(void) : registry_t(16, 0, DATA_SIZE_8) {}

        enum index_t : uint16_t {
            DRUM_NOTE_NUMBER_0,
            DRUM_NOTE_NUMBER_1,
            DRUM_NOTE_NUMBER_2,
            DRUM_NOTE_NUMBER_3,
            DRUM_NOTE_NUMBER_4,
            DRUM_NOTE_NUMBER_5,
            DRUM_NOTE_NUMBER_6,
        };
        void setDrumNoteNumber(uint8_t pitch, uint8_t note) { set8(DRUM_NOTE_NUMBER_0 + pitch, note); }
        uint8_t getDrumNoteNumber(uint8_t pitch) const { return get8(DRUM_NOTE_NUMBER_0 + pitch); }
        void reset(void)
        {   // ドラムパートの初期ノート番号設定
            setDrumNoteNumber(0, 57);
            setDrumNoteNumber(1, 42);
            setDrumNoteNumber(2, 46);
            setDrumNoteNumber(3, 50);
            setDrumNoteNumber(4, 39);
            setDrumNoteNumber(5, 38);
            setDrumNoteNumber(6, 36);
        }
    };

    struct kanplay_slot_t {
        kanplay_part_t chord_part[def::app::max_chord_part];
        reg_chord_part_drum_t chord_part_drum[def::app::max_chord_part];
        reg_slot_info_t slot_info;
        void init(bool psram = false) {
            for (int i = 0; i < def::app::max_chord_part; ++i) {
                chord_part[i].init(psram);
                chord_part_drum[i].init(psram);
            }
            slot_info.init(psram);
        }

        void assign(const kanplay_slot_t &src) {
            for (int i = 0; i < def::app::max_chord_part; ++i) {
                chord_part[i].assign(src.chord_part[i]);
                chord_part_drum[i].assign(src.chord_part_drum[i]);
            }
            slot_info.assign(src.slot_info);
        }
        void reset(void) {
            for (int i = 0; i < def::app::max_chord_part; ++i) {
                chord_part[i].reset();
                chord_part_drum[i].reset();
            }
            slot_info.reset();
        }
        uint32_t crc32(uint32_t crc = 0) const {
            for (int i = 0; i < def::app::max_chord_part; ++i) {
                crc = chord_part[i].crc32(crc);
                crc = chord_part_drum[i].crc32(crc);
            }
            crc = slot_info.crc32(crc);
            return crc;
        }

        // 比較オペレータ
        bool operator== (const kanplay_slot_t &src) const {
            for (int i = 0; i < def::app::max_chord_part; ++i) {
                if (chord_part[i] != src.chord_part[i]) { return false; }
                if (chord_part_drum[i] != src.chord_part_drum[i]) { return false; }
            }
            return slot_info == src.slot_info;
        }
        bool operator!= (const kanplay_slot_t &src) const { return !(*this == src); }
    };

    struct reg_chord_play_t : public registry_t {
        reg_chord_play_t(void) : registry_t(32, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            CHORD_DEGREE,
            CHORD_MODIFIER,
            CHORD_MINOR_SWAP,
            CHORD_SEMITONE,
            CHORD_BASS_DEGREE,
            CHORD_BASS_SEMITONE,
            PART_1_STEP,  // パートの現在のステップ
            PART_2_STEP,
            PART_3_STEP,
            PART_4_STEP,
            PART_5_STEP,
            PART_6_STEP,
            PART_1_ENABLE,  // パートの演奏が現在有効か否か
            PART_2_ENABLE,
            PART_3_ENABLE,
            PART_4_ENABLE,
            PART_5_ENABLE,
            PART_6_ENABLE,
            EDIT_TARGET_PART,
            EDIT_ENC2_TARGET, // 編集時のエンコーダ2のターゲット
            CURSOR_Y,    // 編集時カーソル縦方向位置
            RANGE_X,     // 編集時範囲選択地点横方向位置
            RANGE_W,     // 編集時範囲選択幅
            CONFIRM_ALLCLEAR, // 全消去確認
            CONFIRM_PASTE,    // 貼り付け確認
            TONE_PREVIEW_PROGRAM,
        };
        void setChordDegree(const degree_param_t &degree) { set8(CHORD_DEGREE, degree.raw); }
        degree_param_t getChordDegree(void) const { return get8(CHORD_DEGREE); }
        void setChordModifier(uint8_t modifier) { set8(CHORD_MODIFIER, modifier); }
        KANTANMusic_Modifier getChordModifier(void) const { return (KANTANMusic_Modifier)get8(CHORD_MODIFIER); }
        void setChordMinorSwap(uint8_t swap) { set8(CHORD_MINOR_SWAP, swap); }
        uint8_t getChordMinorSwap(void) const { return get8(CHORD_MINOR_SWAP); }
        void setChordSemitoneShift(int semitone) { set8(CHORD_SEMITONE, semitone); }
        int getChordSemitoneShift(void) const { return (int8_t)get8(CHORD_SEMITONE); }
        void setChordBassDegree(uint8_t degree) { set8(CHORD_BASS_DEGREE, degree); }
        uint8_t getChordBassDegree(void) const { return get8(CHORD_BASS_DEGREE); }
        void setChordBassSemitoneShift(int semitone) { set8(CHORD_BASS_SEMITONE, semitone); }
        int getChordBassSemitoneShift(void) const { return (int8_t)get8(CHORD_BASS_SEMITONE); }
        void setPartStep(uint8_t part_index, int8_t step) { set8(PART_1_STEP + part_index, step); }
        int8_t getPartStep(uint8_t part_index) const { return get8(PART_1_STEP + part_index); }
        void setPartEnable(uint8_t part_index, uint8_t enable) { set8(PART_1_ENABLE + part_index, enable); }
        uint8_t getPartEnable(uint8_t part_index) const { return get8(PART_1_ENABLE + part_index); }
        void setEditTargetPart(uint8_t part_index) { set8(EDIT_TARGET_PART, part_index); }
        uint8_t getEditTargetPart(void) const { return get8(EDIT_TARGET_PART); }
        void setEditEnc2Target(uint8_t target) { set8(EDIT_ENC2_TARGET, target); }
        uint8_t getEditEnc2Target(void) const { return get8(EDIT_ENC2_TARGET); }
        // カーソル位置(縦方向)
        void setCursorY(int y) { 
            while (y < 0) { y = 0; }
            while (y > def::app::max_cursor_y - 1) { y = def::app::max_cursor_y - 1; }
            set8(CURSOR_Y, y);
        }
        uint8_t getCursorY(void) const { uint8_t y = get8(CURSOR_Y); return y < def::app::max_cursor_y ? y : 0; }
        void moveCursorY(int step) {
            setCursorY(getCursorY() + step);
        }
        // 範囲選択位置(横方向)
        void setRangeX(int x) {
            while (x < 0) { x += def::app::max_cursor_x; }
            while (x >= def::app::max_cursor_x) { x -= def::app::max_cursor_x; }
            set8(RANGE_X, x);
        }
        uint8_t getRangeX(void) const { uint8_t x = get8(RANGE_X); return x < def::app::max_cursor_x ? x : 0; }

        void setRangeWidth(int width) { set8(RANGE_W, width); }
        uint8_t getRangeWidth(void) const { return get8(RANGE_W); }

        void setConfirm_AllClear(bool confirm) { set8(CONFIRM_ALLCLEAR, confirm); }
        uint8_t getConfirm_AllClear(void) const { return get8(CONFIRM_ALLCLEAR); }

        void setConfirm_Paste(bool confirm) { set8(CONFIRM_PASTE, confirm); }
        uint8_t getConfirm_Paste(void) const { return get8(CONFIRM_PASTE); }
        void setTonePreviewProgram(uint8_t program) { set8(TONE_PREVIEW_PROGRAM, program); }
        uint8_t getTonePreviewProgram(void) const { return get8(TONE_PREVIEW_PROGRAM); }
    };

    struct reg_chord_progression_t : public registry_t {
        reg_chord_progression_t(void) : registry_t(8192, 0, DATA_SIZE_32) {}
        std::pair<uint32_t, progression_desc_t>* begin(void) const {
            return (std::pair<uint32_t, progression_desc_t>*)_reg_data;
        }
        std::pair<uint32_t, progression_desc_t>* end(void) const {
            return (std::pair<uint32_t, progression_desc_t>*)(_reg_data) + _data_count;
        }
        size_t max_count(void) const {
            return _registry_size / sizeof(std::pair<uint32_t, progression_desc_t>);
        }

        static bool _sameChord(const progression_desc_t& a, const progression_desc_t& b) {
            return a.main_degree == b.main_degree
                && a.bass_degree == b.bass_degree
                && a.getModifier() == b.getModifier();
        }
        static void _copyChord(progression_desc_t& dst, const progression_desc_t& src) {
            dst.main_degree = src.main_degree;
            dst.bass_degree = src.bass_degree;
            dst.setModifier(src.getModifier());
        }

        // 前方伝播:
        // 録音で途中に値を入れた時、その変更は「次の同種変更」まで有効になる。
        // chord / slot / part はそれぞれ独立したイベントとして扱い、
        // たとえば途中に slot 変更があっても chord は次の chord 変更まで伝播する。
        void _propagateDescriptor(std::pair<uint32_t, progression_desc_t>* it,
                                  const progression_desc_t& old_desc,
                                  const progression_desc_t& new_desc) {
            bool propagate_chord = !_sameChord(old_desc, new_desc);
            bool propagate_slot  = old_desc.getSlotIndex() != new_desc.getSlotIndex();
            bool propagate_part  = old_desc.getPartBits()  != new_desc.getPartBits();
            if (!propagate_chord && !propagate_slot && !propagate_part) { return; }

            auto e = end();
            for (; it != e; ++it) {
                if (propagate_chord) {
                    if (_sameChord(it->second, old_desc)) {
                        _copyChord(it->second, new_desc);
                    } else {
                        propagate_chord = false;
                    }
                }
                if (propagate_slot) {
                    if (it->second.getSlotIndex() == old_desc.getSlotIndex()) {
                        it->second.setSlotIndex(new_desc.getSlotIndex());
                    } else {
                        propagate_slot = false;
                    }
                }
                if (propagate_part) {
                    if (it->second.getPartBits() == old_desc.getPartBits()) {
                        it->second.setPartBits(new_desc.getPartBits());
                    } else {
                        propagate_part = false;
                    }
                }
                if (!propagate_chord && !propagate_slot && !propagate_part) { break; }
            }
        }

        // 指定したステップと同値かそれより小さい最大のステップを持つ要素のイテレータを返す
        std::pair<uint32_t, progression_desc_t>* find(uint16_t step) const {
            if (step >= def::app::max_progression_length) { return nullptr; }
            auto bg = begin();
            auto ed = end();
            // upper_boundでstepより大きい最初の要素を見つける
            auto it = std::upper_bound( bg
                                      , ed
                                      , step
                                      , [](const auto& a, const auto& b) { return a < b.first; }
                                      );
            if (it == bg) { return nullptr; }
            return --it;
        }
        progression_desc_t getStepDescriptor(uint16_t step) const {
            // 指定した位置またはその直前のステップ情報を返す
            auto it = find(step);
            if (it != nullptr) { if (it->first <= step) { return it->second; } }
            return progression_desc_t();
        }

        // 指定したステップに対して値を設定する
        bool setStepDescriptor(uint16_t step, const progression_desc_t& value) {
            // 最大値チェック
            if (step >= def::app::max_progression_length) { return false; }
            if (_data_count >= max_count()) { return false; }

            // 書き込み前の実効値（前方伝播の基準となる「旧値」）を求める
            // 書き込み対象ステップより手前の直近エントリ、またはそのステップ自身の現在値
            progression_desc_t old_desc = value;
            {
                auto prev = find(step);
                if (prev != nullptr) {
                    old_desc = prev->second;
                }
            }

            // 対象ステップの要素を探索
            auto it = find(step);
            auto insert_pos = begin();
            if (it != nullptr) {
                if (it->first < step) {
                    // 指定ステップより前の要素が見つかった場合、その次の位置に挿入する
                    insert_pos = it + 1;
                } else {
                    // 指定ステップと同じ要素が見つかった場合、その位置に上書きする
                    it->second = value;
                    _propagateDescriptor(it + 1, old_desc, value);
                    return true;
                }
            } else {
                // 指定ステップより前の要素が存在しない場合、先頭に挿入する
                insert_pos = begin();
            }

            // 挿入位置以降の要素を1つ後ろにシフトする
            auto e = end();
            for (auto shift_it = e; shift_it != insert_pos; ) {
                --shift_it;
                *(shift_it + 1) = *shift_it;
            }
            // 新しい要素を挿入する
            insert_pos->first = step;
            insert_pos->second = value;
            ++_data_count;
            _propagateDescriptor(insert_pos + 1, old_desc, value);
            return true;
        }
        void clear(void) { _data_count = 0; }
        size_t getDataCount(void) const { return _data_count; }
        void setDataCount(size_t count) { _data_count = count; }
        void deleteAfter(uint16_t step) {
            // 指定したステップ以降のデータを削除する
            auto it = find(step);
            if (it != nullptr) {
                if (it->first < step) {
                    ++it;
                }
                size_t index = it - begin();
                _data_count = index;
            }
        }

        // 指定ステップより後にある最初のエントリを1つ削除する（上書き録音で次のコードチェンジを消すために使用）
        void deleteNextEntry(uint16_t step) {
            auto bg = begin();
            auto e = end();
            // step より大きい最初のエントリを探す
            auto it = std::upper_bound(bg, e, step,
                [](const auto& a, const auto& b) { return a < b.first; });
            if (it == e) { return; }
            // そのエントリを削除（後続を前に詰める）
            auto next = it + 1;
            while (next != e) { *it++ = *next++; }
            --_data_count;
        }
        bool saveJson(JsonVariant &json, uint16_t length_limit = def::app::max_progression_length);
        bool loadJson(const JsonVariant &json);
        uint32_t crc32(uint32_t crc_init) const override {
            return calc_crc32(_reg_data, _data_count * sizeof(std::pair<uint32_t, progression_desc_t>), crc_init);
        }
        void assign(const reg_chord_progression_t &src) {
            _data_count = src._data_count;
            memcpy(_reg_data, src._reg_data, _registry_size);
        }

    protected:
        size_t _data_count = 0;
    };
#if 0
    // シーケンス演奏パターン情報
    struct reg_progression_map_t : public registry_map_t<progression_desc_t> {
        reg_progression_map_t(void) : registry_map_t<progression_desc_t>(progression_desc_t()) {}
        std::map<uint16_t, progression_desc_t>::const_iterator get_le(uint16_t step) const {
            // 指定した位置またはその直前のステップ情報を返す
            if (step >= def::app::max_progression_length) {
                return _data.end();
            }
            auto it = std::lower_bound( _data.begin()
                                      , _data.end()
                                      , step
                                      , [](const auto& a, const auto& b) { return a.first < b; }
                                      );
            if (it != _data.end()) {
                if (it->first == step) {
                    return it;
                }
            }
            if (it != _data.begin()) {
                --it;
            }
            return it; 
        }
        const progression_desc_t& getStepDescriptor(uint16_t step) const {
            // 指定した位置またはその直前のステップ情報を返す
            auto it = get_le(step);
            if (it != _data.end()) {
                if (it->first <= step) {
                    return it->second;
                }
            }
            return _default_value;
        }
        void setStepDescriptor(uint16_t step, const progression_desc_t& value) {
            if (step >= def::app::max_progression_length) {
                return;
            }
            auto it = get_le(step);
            if (it != _data.end() && it->first <= step) {
                if (it->first == step) {
                    // 上書きされる値がある場合、先に削除しておく
                    _data.erase(it);
                    // 再帰呼び出しで対応することにより、現在位置より前の値との同一内容チェックが働く
                    setStepDescriptor(step, value);
                    return;
                }
                if (it->second == value) {
                    // 既に同じ内容が存在しているなら登録しない
                    return;
                }
            }
            set(step, value);
        }
        void clear(void) {
            _data.clear();
        }
        void deleteAfter(uint16_t step) {
            // 指定したステップ以降のデータを削除する
            auto it = _data.lower_bound(step);
            while (it != _data.end()) {
                it = _data.erase(it);
            }
        }
        const auto& getDataMap(void) const { return _data; }
        bool saveJson(JsonVariant &json);
        bool loadJson(const JsonVariant &json);
    };
#endif
    struct reg_progression_info_t : public registry_t {
        reg_progression_info_t(void) : registry_t(16, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            LENGTH_L,
            LENGTH_H,
        };
        void setLength(uint16_t step) { set16(LENGTH_L, step); }
        uint16_t getLength(void) const { return get16(LENGTH_L); }
    };

    struct progression_data_t {
        reg_chord_progression_t timeline;
        reg_progression_info_t info;

        void init(bool psram = false) {
            timeline.init(psram);
            info.init(psram);
        }
        void assign(const progression_data_t &src) {
            timeline.assign(src.timeline);
            info.assign(src.info);
        }
        void reset(void) {
            info.setLength(0);
            timeline.clear();
        }
        uint32_t crc32(uint32_t crc) const {
            crc = info.crc32(crc);
            crc = timeline.crc32(crc);
            return crc;
        }
        progression_desc_t getStepDescriptor(uint16_t step) const {
            if (step >= info.getLength()) {
                step = -1;
            }
            return timeline.getStepDescriptor(step);
        }
        void setStepDescriptor(uint16_t step, const progression_desc_t& value) {
            if (step >= def::app::max_progression_length) {
                return;
            }
            timeline.setStepDescriptor(step, value);
            auto max_step = info.getLength();
            if (step >= max_step) {
                info.setLength(step+1);
            }
        }
        void deleteAfter(uint16_t step)
        {
            auto max_step = info.getLength();
            if (step < max_step) {
                info.setLength(step);
                timeline.deleteAfter(step);
            }
        }

        // シーケンスを200%に引き伸ばす（各ステップを2倍の位置に配置）
        bool stretch(void) {
            uint16_t len = info.getLength();
            if (len == 0) { return false; }
            uint32_t new_len = (uint32_t)len * 2;
            if (new_len > def::app::max_progression_length) { return false; }
            // スパース配列の各エントリのステップ番号を2倍にする
            for (auto it = timeline.begin(); it != timeline.end(); ++it) {
                it->first *= 2;
            }
            info.setLength(new_len);
            return true;
        }

        // シーケンスを50%に圧縮する（奇数ステップの実効値を採用）
        bool compress(void) {
            uint16_t len = info.getLength();
            if (len <= 1) { return false; }
            uint16_t new_len = len / 2;
            if (new_len == 0) { return false; }

            auto data = timeline.begin();
            size_t old_count = timeline.end() - data;
            size_t read_idx = 0;
            size_t write_idx = 0;
            progression_desc_t current_desc = {};
            progression_desc_t prev_desc = {};

            for (uint16_t new_step = 0; new_step < new_len; ++new_step) {
                uint16_t old_step = new_step * 2 + 1;
                // old_step 以下の最後のエントリまで進めて実効値を取得
                while (read_idx < old_count && data[read_idx].first <= old_step) {
                    current_desc = data[read_idx].second;
                    ++read_idx;
                }
                // 変化点のみ記録（スパース配列の再構築）
                if (new_step == 0 || current_desc != prev_desc) {
                    data[write_idx].first = new_step;
                    data[write_idx].second = current_desc;
                    ++write_idx;
                }
                prev_desc = current_desc;
            }
            timeline.setDataCount(write_idx);
            info.setLength(new_len);
            return true;
        }
    };

    struct reg_command_request_t : public registry_base_t {
#if __has_include (<freertos/FreeRTOS.h>)
        using registry_base_t::setNotifyTaskHandle;
#endif
        reg_command_request_t(void) : registry_base_t(64) {}
        enum index_t : uint16_t {
            COMMAND_RELEASED = 0,
            COMMAND_PRESSED = 2,
        };

        bool getQueue(history_code_t *code, def::command::command_param_t *command_param, bool *is_pressed) {
            auto history = getHistory(*code);
            if (history == nullptr) { return false; }
            *command_param = static_cast<def::command::command_param_t>(history->value);
            *is_pressed = history->index == COMMAND_PRESSED;
            return true;
        }
        void addQueue(const def::command::command_param_t& command_param, bool is_pressed = true)
        { set16(is_pressed ? COMMAND_PRESSED : COMMAND_RELEASED, command_param.raw, true); }

        void addQueueW(const def::command::command_param_t& command_param) {
            addQueue(command_param, true);
            addQueue(command_param, false);
        }
    };

    struct reg_song_info_t : public registry_t {
        reg_song_info_t(void) : registry_t(8, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
            TEMPO_BPM_L,
            TEMPO_BPM_H,
            SWING,
            BASE_KEY,
            NUM_SLOT,
        };
        void setTempo(uint16_t bpm) {
            if (bpm < def::app::tempo_bpm_min) { bpm = def::app::tempo_bpm_min; }
            if (bpm > def::app::tempo_bpm_max) { bpm = def::app::tempo_bpm_max; }
            set16(TEMPO_BPM_L, bpm);
        }
        uint16_t getTempo(void) const { return get16(TEMPO_BPM_L); }
        void setSwing(uint8_t swing) { set8(SWING, swing); }
        uint8_t getSwing(void) const { return get8(SWING); }
        void setBaseKey(uint8_t key) { set8(BASE_KEY, key); }
        uint8_t getBaseKey(void) const { return get8(BASE_KEY); }
        void setNumSlot(uint8_t n) {
            if (n < def::app::min_active_slot) { n = def::app::min_active_slot; }
            if (n > def::app::max_slot) { n = def::app::max_slot; }
            set8(NUM_SLOT, n);
        }
        uint8_t getNumSlot(void) const {
            uint8_t n = get8(NUM_SLOT);
            if (n < def::app::min_active_slot || n > def::app::max_slot) {
                return def::app::default_active_slot;
            }
            return n;
        }
        void reset(void) {
            setTempo(def::app::tempo_bpm_default);
            setSwing(def::app::swing_percent_default);
            setBaseKey(0);
            setNumSlot(def::app::default_active_slot);
        }
    };

    // ソングデータ
    struct song_data_t {
        reg_song_info_t song_info;
        progression_data_t progression;

        kanplay_slot_t slot[def::app::max_slot];

        size_t saveSongJSON(uint8_t* data, size_t data_length);
        bool loadSongJSON(const uint8_t* data, size_t data_length, def::app::data_type_t dir_type = def::app::data_type_t::data_unknown);

        void init(bool psram = false) {
            song_info.init(psram);
            progression.init(true);
            for (int i = 0; i < def::app::max_slot; ++i) {
                slot[i].init(psram);
            }
        }
        uint32_t crc32(uint32_t crc = 0) const {
            auto num_slot = song_info.getNumSlot();
            crc = song_info.crc32(crc);
            crc = progression.crc32(crc);
            for (int i = 0; i < num_slot; ++i) {
                crc = slot[i].crc32(crc);
            }
            return crc;
        }

        // メモリ上の文字列データから読み込む(旧仕様)
        bool loadText(uint8_t* data, size_t data_length);

        // メモリ上に文字列データを保存する(旧仕様)
        // size_t saveText(uint8_t* data, size_t data_length);

        bool assign(const song_data_t &src) {
            song_info.assign(src.song_info);
            progression.assign(src.progression);
            auto num_slot = song_info.getNumSlot();
            for (int i = 0; i < num_slot; ++i) {
                slot[i].assign(src.slot[i]);
            }
            return true;
        }
        void reset(void) {
            song_info.reset();
            progression.reset();
            auto num_slot = song_info.getNumSlot();
            for (int i = 0; i < num_slot; ++i) {
                slot[i].reset();
            }
        }

        // 比較オペレータ
        bool operator== (const song_data_t &src) const {
            if (song_info != src.song_info) { return false; }
            if (progression.info != src.progression.info) { return false; }
            auto num_slot = song_info.getNumSlot();
            for (int i = 0; i < num_slot; ++i) {
                if (slot[i] != src.slot[i]) { return false; }
            }
            return true;
        }
        bool operator!= (const song_data_t &src) const { return !(*this == src); }
    };

    // アルペジオパターン単体のJSON保存/読出し
    static size_t saveArpeggioJSON(uint8_t* data_buffer, size_t data_length, const kanplay_slot_t& slot, uint8_t part_index);
    static bool loadArpeggioJSON(const uint8_t* data, size_t data_length, kanplay_slot_t& slot, uint8_t part_index);

    // コード進行データ単体のJSON保存/読出し
    size_t saveProgressionJSON(uint8_t* data_buffer, size_t data_length);
    bool loadProgressionJSON(const uint8_t* data, size_t data_length);

    // ボタンへのコマンドマッピングテーブル
    // コマンドは2Byteだが１ボタンに最大4個までコマンドを割り当てることができる
    // そのため、ボタンに8Byteの割り当てとしている
    struct reg_command_mapping_t : public registry_t {
        reg_command_mapping_t(uint8_t button_count) : registry_t(button_count * 8, 0, DATA_SIZE_32) {}
        void setCommandParamArray(uint8_t button_index, def::command::command_param_array_t command)
        {
            set32(button_index * 8, command.raw32_0);
            set32(button_index * 8 + 4, command.raw32_1);
        }
        def::command::command_param_array_t getCommandParamArray(uint8_t button_index) const { return def::command::command_param_array_t { get32(button_index * 8), get32(button_index * 8 + 4) }; }
        void reset(void) {
            for (int i = 0; i < _registry_size; i += 4) {
                set32(i, 0);
            }
        }
        size_t getButtonCount(void) const { return _registry_size >> 3; }
        bool empty(void) const {
            for (int i = 0; i < _registry_size; i += 4) {
                if (get32(i) != 0) {
                    return false;
                }
            }
            return true;
        }
    };

    struct reg_color_setting_t : public registry_t {
        reg_color_setting_t(void) : registry_t(72, 0, DATA_SIZE_32) {}
        enum index_t : uint16_t {
            ENABLE_PART_COLOR = 0x00,
            DISABLE_PART_COLOR = 0x04,
            ARPEGGIO_NOTE_FORE_COLOR = 0x08,
            ARPEGGIO_NOTE_BACK_COLOR = 0x0C,
            ARPEGGIO_STEP_COLOR = 0x10,
            BUTTON_DEFAULT_COLOR = 0x14,
            BUTTON_DEGREE_COLOR = 0x18,
            BUTTON_MODIFIER_COLOR = 0x1C,
            BUTTON_MINOR_SWAP_COLOR = 0x20,
            BUTTON_SEMITONE_COLOR = 0x24,
            BUTTON_NOTE_COLOR = 0x28,
            BUTTON_DRUM_COLOR = 0x2C,
            BUTTON_CURSOR_COLOR = 0x30,
            BUTTON_PRESSED_TEXT_COLOR = 0x34,
            BUTTON_WORKING_TEXT_COLOR = 0x38,
            BUTTON_DEFAULT_TEXT_COLOR = 0x3C,
            BUTTON_MENU_NUMBER_COLOR = 0x40,
            BUTTON_PART_COLOR = 0x44,
        };
        void setEnablePartColor(uint32_t color) { set32(ENABLE_PART_COLOR, color); }
        uint32_t getEnablePartColor(void) const { return get32(ENABLE_PART_COLOR); }
        void setDisablePartColor(uint32_t color) { set32(DISABLE_PART_COLOR, color); }
        uint32_t getDisablePartColor(void) const { return get32(DISABLE_PART_COLOR); }
        void setArpeggioNoteForeColor(uint32_t color) { set32(ARPEGGIO_NOTE_FORE_COLOR, color); }
        uint32_t getArpeggioNoteForeColor(void) const { return get32(ARPEGGIO_NOTE_FORE_COLOR); }
        void setArpeggioNoteBackColor(uint32_t color) { set32(ARPEGGIO_NOTE_BACK_COLOR, color); }
        uint32_t getArpeggioNoteBackColor(void) const { return get32(ARPEGGIO_NOTE_BACK_COLOR); }
        void setArpeggioStepColor(uint32_t color) { set32(ARPEGGIO_STEP_COLOR, color); }
        uint32_t getArpeggioStepColor(void) const { return get32(ARPEGGIO_STEP_COLOR); }
        void setButtonDefaultColor(uint32_t color) { set32(BUTTON_DEFAULT_COLOR, color); }
        uint32_t getButtonDefaultColor(void) const { return get32(BUTTON_DEFAULT_COLOR); }
        void setButtonDegreeColor(uint32_t color) { set32(BUTTON_DEGREE_COLOR, color); }
        uint32_t getButtonDegreeColor(void) const { return get32(BUTTON_DEGREE_COLOR); }
        void setButtonModifierColor(uint32_t color) { set32(BUTTON_MODIFIER_COLOR, color); }
        uint32_t getButtonModifierColor(void) const { return get32(BUTTON_MODIFIER_COLOR); }
        void setButtonMinorSwapColor(uint32_t color) { set32(BUTTON_MINOR_SWAP_COLOR, color); }
        uint32_t getButtonMinorSwapColor(void) const { return get32(BUTTON_MINOR_SWAP_COLOR); }
        void setButtonSemitoneColor(uint32_t color) { set32(BUTTON_SEMITONE_COLOR, color); }
        uint32_t getButtonSemitoneColor(void) const { return get32(BUTTON_SEMITONE_COLOR); }
        void setButtonNoteColor(uint32_t color) { set32(BUTTON_NOTE_COLOR, color); }
        uint32_t getButtonNoteColor(void) const { return get32(BUTTON_NOTE_COLOR); }
        void setButtonDrumColor(uint32_t color) { set32(BUTTON_DRUM_COLOR, color); }
        uint32_t getButtonDrumColor(void) const { return get32(BUTTON_DRUM_COLOR); }
        void setButtonCursorColor(uint32_t color) { set32(BUTTON_CURSOR_COLOR, color); }
        uint32_t getButtonCursorColor(void) const { return get32(BUTTON_CURSOR_COLOR); }
        void setButtonPressedTextColor(uint32_t color) { set32(BUTTON_PRESSED_TEXT_COLOR, color); }
        uint32_t getButtonPressedTextColor(void) const { return get32(BUTTON_PRESSED_TEXT_COLOR); }
        void setButtonWorkingTextColor(uint32_t color) { set32(BUTTON_WORKING_TEXT_COLOR, color); }
        uint32_t getButtonWorkingTextColor(void) const { return get32(BUTTON_WORKING_TEXT_COLOR); }
        void setButtonDefaultTextColor(uint32_t color) { set32(BUTTON_DEFAULT_TEXT_COLOR, color); }
        uint32_t getButtonDefaultTextColor(void) const { return get32(BUTTON_DEFAULT_TEXT_COLOR); }
        void setButtonMenuNumberColor(uint32_t color) { set32(BUTTON_MENU_NUMBER_COLOR, color); }
        uint32_t getButtonMenuNumberColor(void) const { return get32(BUTTON_MENU_NUMBER_COLOR); }
        void setButtonPartColor(uint32_t color) { set32(BUTTON_PART_COLOR, color); }
        uint32_t getButtonPartColor(void) const { return get32(BUTTON_PART_COLOR); }
    };

    struct reg_menu_status_t : public registry_t {
        reg_menu_status_t(void) : registry_t(20, 0, DATA_SIZE_8) {}
        enum index_t : uint16_t {
          CURRENT_LEVEL,
          MENU_CATEGORY,
          CURRENT_MENU_ID_L,
          CURRENT_MENU_ID_H,
          SELECT_INDEX_LEVEL_M1L,
          SELECT_INDEX_LEVEL_M1H,
          SELECT_INDEX_LEVEL_0L,
          SELECT_INDEX_LEVEL_0H,
          SELECT_INDEX_LEVEL_1L,
          SELECT_INDEX_LEVEL_1H,
          SELECT_INDEX_LEVEL_2L,
          SELECT_INDEX_LEVEL_2H,
          SELECT_INDEX_LEVEL_3L,
          SELECT_INDEX_LEVEL_3H,
          SELECT_INDEX_LEVEL_4L,
          SELECT_INDEX_LEVEL_4H,
          SELECT_INDEX_LEVEL_5L,
          SELECT_INDEX_LEVEL_5H,
        };
        void reset(void) { for (int i = 0; i < 20; ++i) { set8(i, 0); } }
        void setCurrentLevel(uint8_t level) { set8(CURRENT_LEVEL, level); }
        uint8_t getCurrentLevel(void) const { return get8(CURRENT_LEVEL); }
        void setCurrentMenuID(uint16_t menu_id) { set16(CURRENT_MENU_ID_L, menu_id); }
        uint16_t getCurrentMenuID(void) const { return get16(CURRENT_MENU_ID_L); }
        void setMenuCategory(uint8_t index) {
            assert(index < 8 && "Menu category index is out of range.");
            set8(MENU_CATEGORY, index);
        }
        uint8_t getMenuCategory(void) const { return get8(MENU_CATEGORY); }
        void setSelectIndex(uint8_t level, uint16_t index) { set16(SELECT_INDEX_LEVEL_0L + ((int8_t)level)*2, index); }
        uint16_t getSelectIndex(uint8_t level) const { return get16(SELECT_INDEX_LEVEL_0L + ((int8_t)level)*2); }
    }; 

    struct control_mapping_t {
        reg_command_mapping_t internal { def::hw::max_main_button };      // プレイボタンのマッピングテーブル
        reg_command_mapping_t slot     { def::hw::max_slot_button };      // スロットボタンのマッピングテーブル
        reg_command_mapping_t external { def::hw::max_button_mask };     // 外部機器ボタンのマッピングテーブル
        reg_command_mapping_t midinote { def::midi::max_note };    // MIDIノートへのコマンドマッピングテーブル
        void init(bool psram = false) {
            internal.init(psram);
            slot.init(psram);
            external.init(psram);
            midinote.init(psram);
        }
        uint32_t crc32(uint32_t crc = 0) const {
            crc = internal.crc32(crc);
            crc = slot.crc32(crc);
            crc = external.crc32(crc);
            crc = midinote.crc32(crc);
            return crc;
        }
        size_t saveJSON(uint8_t* data, size_t data_length);
        bool loadJSON(const uint8_t* data, size_t data_length);
        bool saveJSON(JsonVariant &json);
        bool loadJSON(const JsonVariant &json);
        bool empty(void) const {
            return internal.empty() && slot.empty() && external.empty() && midinote.empty();
        }
        void reset(void) {
            internal.reset();
            slot.reset();
            external.reset();
            midinote.reset();
            system_registry->updateControlMapping();
        }
    };

    reg_menu_status_t      menu_status;

    reg_task_status_t      task_status;         // タスクの動作状態
    reg_sub_button_t       sub_button;          // サブボタンのコマンド
    reg_internal_input_t   internal_input;      // かんぷれ本体ボタンの入力状態
    reg_internal_imu_t     internal_imu;        // かんぷれ本体のIMU情報
    reg_rgbled_control_t   rgbled_control;      // かんぷれ本体ボタンのカラーLED制御(操作状態が反映された色)

    reg_command_request_t  operator_command;    // コマンダーからオペレータへの全体的な動作指示
    reg_command_request_t  player_command;      // オペレータから演奏部への指示に限定したコマンド

    reg_chord_play_t       chord_play;          // コード演奏情報
    song_data_t            song_data;           // 演奏対象のソングデータ スロット1~8のデータ (保存用)
    kanplay_slot_t*        current_slot = &song_data.slot[0];        // 現在の操作対象スロット(編集中のスロット)
    progression_data_t*       current_progression = &song_data.progression;     // 現在のコード進行データへのポインタ

    // 仮シーケンス: メインのシーケンスが空の時にジャンルデータから読み込んだ進行データを一時保持する
    // current_progression がこちらを指している間はジャンルのシーケンスで演奏する
    progression_data_t     provisional_progression;

    bool hasProvisionalProgression(void) const {
        return current_progression == &provisional_progression;
    }

    // 仮シーケンスをメインシーケンスにコピーしてポインタをメインに戻す
    void promoteProvisionalProgression(void) {
        song_data.progression.assign(provisional_progression);
        provisional_progression.reset();
        current_progression = &song_data.progression;
    }

    // 仮シーケンスをクリアしてポインタをメインに戻す
    void clearProvisionalProgression(void) {
        provisional_progression.reset();
        current_progression = &song_data.progression;
    }

    // // 一時預かりデータ。ファイルから読込処理を行う際の一時利用や、編集モードに移行する前に元の状態を保持する
    song_data_t            backup_song_data;

    reg_color_setting_t    color_setting;       // GUIの各種カラー設定

    reg_external_input_t   external_input;      // 外部機器のボタン類の操作状態
    reg_midi_input_t       midi_input;          // 外部MIDIノート入力（Learn用）

    control_mapping_t      control_mapping[2];  // コントロールマッピング設定 (0:本体デフォルト, 1:ソングデータ)

    reg_command_mapping_t command_mapping_internal { def::hw::max_main_button };
    reg_command_mapping_t command_mapping_slot     { def::hw::max_slot_button };
    reg_command_mapping_t command_mapping_external { def::hw::max_button_mask };
    reg_command_mapping_t command_mapping_midinote { def::midi::max_note };

    reg_command_mapping_t command_mapping_current { def::hw::max_button_mask };      // 現在のボタンマッピングテーブル
    reg_command_mapping_t command_mapping_port_b { 4 };     // 外部機器ボタンのマッピングテーブル
    reg_command_mapping_t command_mapping_midicc15 { def::midi::max_note };    // MIDI CCへのコマンドマッピングテーブル
    reg_command_mapping_t command_mapping_midicc16 { def::midi::max_note };    // MIDI CCへのコマンドマッピングテーブル

    kanplay_slot_t       clipboard_slot;      // コピー/ペースト(クリップボード)データ。コピー/カットしたデータを一時的に保持する
    reg_arpeggio_table_t clipboard_arpeggio;  // コピー/ペースト(クリップボード)データ。コピー/カットしたデータを一時的に保持する

    enum clipboard_contetn_t : uint8_t {
        CLIPBOARD_CONTENT_NONE,
        CLIPBOARD_CONTENT_SLOT,
        CLIPBOARD_CONTENT_PART,
        CLIPBOARD_CONTENT_ARPEGGIO,
    };
    clipboard_contetn_t clipboard_content;    // コピー/ペースト(クリップボード)の内容

    registry_t drum_mapping { 16, 0, registry_t::DATA_SIZE_8 }; // ドラム演奏モードのコマンドとノートナンバーのマッピングテーブル

    // プレビュー演奏時は保存されたPlayModeを変更せず、仮のモードを返す
    def::playmode::playmode_t currentPlayMode(void) const {
        auto mode = runtime_info.getPlayMode();
        if (runtime_info.getPreviewPlay()) {
            // プレビュー演奏は強制的にオートソング扱いにする
            mode = def::playmode::pm_auto_song;
        }

        // ソングデータのコード進行がない場合、ガイド演奏やオートソングはできないのでビートプレイにフォールバックさせる
        if (current_progression->info.getLength() == 0) {
            if (mode == def::playmode::pm_auto_song
             || mode == def::playmode::pm_guide_play
             || mode == def::playmode::pm_free_guide) {
                mode = def::playmode::pm_beat_play;
            }
        }

        // オートプレイが発動している場合
        auto autoplay_state = runtime_info.getAutoplayState();
        if (autoplay_state != def::play::auto_play_state_t::auto_play_none
         && autoplay_state != def::play::auto_play_state_t::auto_play_beatmode) {
            if (mode == def::playmode::pm_free_play) {
                // フリープレイモードの場合はビート演奏モード扱いにする
                mode = def::playmode::pm_beat_play;
            } else
            if (mode == def::playmode::pm_guide_play) {
                // ガイドプレイモードの場合はオートソングモード扱いにする
                mode = def::playmode::pm_auto_song;
            }
        }

        return mode;
    }

    // コード進行シーケンスが有効なモードか判定
    bool isGuideActiveMode(void) const {
        auto mode = currentPlayMode();
        return mode == def::playmode::pm_guide_play
            || mode == def::playmode::pm_free_guide
            || mode == def::playmode::pm_auto_song;
    }

    void checkSongModified(void) const;

    static constexpr const size_t raw_wave_length = 320;
    std::pair<uint8_t, uint8_t> raw_wave[raw_wave_length] = { { 128, 128 },};
    uint16_t raw_wave_pos = 0;

protected:
    // 変更前のソングデータのCRC32値 (変更検出用)
    uint32_t unchanged_song_crc32 = 0;
    uint32_t unchanged_kmap_crc32 = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

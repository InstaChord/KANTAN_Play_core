#!/usr/bin/env python3
"""Static regression guards for KANTAN Sequencer compatibility boundaries."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_names_and_ota_identity() -> None:
    common = (ROOT / "main/common_define.hpp").read_text()
    catalog = json.loads((ROOT / "docs/firmware/catalog.json").read_text())
    manifest = json.loads((ROOT / "docs/manifest.json").read_text())

    assert 'firmware_display_name = "KANTAN Sequencer"' in common
    assert 'hardware_display_name = "KANTAN Play core"' in common
    assert 'ota_app_id = "kantanplay"' in common
    sequencer_entries = [item for item in catalog["firmware"]
                         if item.get("app") == "kantanplay"]
    assert sequencer_entries
    assert all(item["app"] == "kantanplay" for item in sequencer_entries)
    assert sequencer_entries[0]["name"] == "KANTAN Sequencer"
    assert manifest["name"] == "KANTAN Sequencer for KANTAN Play core"


def test_sd_recovery_is_sequencer_only() -> None:
    source = (ROOT / "main/file_manage.cpp").read_text()
    begin = source[source.index("bool storage_sd_t::beginStorage(void)"):
                   source.index("bool storage_sd_t::mountStorage(void)")]
    assert "#if !defined(KANPLAY_SAMPLER)" in begin
    assert "sd_media_state_t::missing" in begin
    assert "sd_media_state_t::error" in begin
    assert "return loadStorage();" in begin
    assert "sd_media_state_t::safe_to_remove" not in begin

    assert source.count("#if defined(KANPLAY_SAMPLER)\n   &&") >= 2


def test_check_build_has_no_publish_hook() -> None:
    config = (ROOT / "platformio.ini").read_text()
    start = config.index("[env:sequencer_check_s3]")
    end = config.index("; ==========================================================================", start)
    section = config[start:end]
    assert "extra_scripts" not in section
    assert "KANPLAY_SAMPLER" not in section

    sampler_start = config.index("[env:sampler_check_s3]")
    sampler_end = config.index("; CoreS3 サンプラー デバッグビルド", sampler_start)
    sampler_section = config[sampler_start:sampler_end]
    assert "KANPLAY_SAMPLER=1" in sampler_section
    assert "generate_user_custom.py" not in sampler_section


def test_external_device_matches_sampler_route_model() -> None:
    menu = (ROOT / "main/menu_data/menu_data_arrays.inl").read_text()
    midi = (ROOT / "main/menu_data/menu_data_midi.inl").read_text()
    registry = (ROOT / "main/system_registry.hpp").read_text()
    settings = (ROOT / "main/system_registry.cpp").read_text()

    assert "mi_external_input_source_t" in menu
    assert '{ "Input Source"' in menu
    assert "MENU_BUILDER(mi_ble_midi_t" not in menu
    assert "MENU_BUILDER(mi_usb_mode_t" not in menu
    assert "MENU_BUILDER(mi_usb_power_t" not in menu
    assert "MENU_BUILDER(mi_usb_midi_t" not in menu
    for label in ("Off", "USB MIDI Controller", "USB MIDI Computer", "BLE MIDI"):
        assert label in midi
    assert "setExternalInputSource" in registry
    assert "_reg_data_8[BLE_MIDI] = def::command::midi_off" in registry
    assert "_reg_data_8[USB_MIDI] = def::command::midi_off" in registry
    assert "external_input_source" in settings
    source_selector = midi[midi.index("struct mi_external_input_source_t"):midi.index("struct mi_ble_connection_t")]
    assert "changeSource(" in source_selector
    assert "setExternalInputSource(next)" not in source_selector
    assert '"Restart Required"' in source_selector
    assert '"Apply & Restart"' in source_selector
    assert "safe_default_row" in source_selector
    for label in ("Scan & Connect", "Forget Device", "Reset BLE Connection", "Device Info", "Input Assign"):
        assert label in menu


def test_radio_lifecycle_and_sampler_isolation() -> None:
    midi = (ROOT / "main/task_midi.cpp").read_text()
    wifi = (ROOT / "main/task_wifi.cpp").read_text()
    boot = (ROOT / "main/main.cpp").read_text()
    assert "prepareAtBoot" in boot
    assert boot.index("loadPreferredDevice") < boot.index("task_midi->start")
    gate = wifi[wifi.index("const bool radio_requested"):wifi.index("const bool setup_ap_waiting_for_auth")]
    assert "#if !defined(KANPLAY_SAMPLER)" in gate
    assert "isBLEStoppedForWiFi()" in gate
    assert "goal = prev_goal" in gate and "continue;" in gate
    assert "setWiFiAPInfo" not in gate and "setMidiPortStateBLE" not in gate
    assert "ble_wifi_stopped.store(true)" in midi
    assert midi.index("ble_midi_transport.setUseTxRx(ble_out, ble_in)") < midi.index("ble_wifi_stopped.store(true)")
    gui = (ROOT / "main/gui/gui_popup.inl").read_text()
    external = (ROOT / "main/sequencer_external.cpp").read_text()
    assert "ui_restart_notice_t" in gui
    assert "Please do not turn off the power" in gui
    assert "restart_not_before_msec = M5.millis() + 1800" in external
    operator = (ROOT / "main/task_operator.cpp").read_text()
    reset_case = operator[operator.index("case def::command::system_control_t::sc_reset:"):
                          operator.index("case def::command::system_control_t::sc_save:")]
    assert "requestRestart" in reset_case
    assert "firmware_update" in reset_case


def test_wifi_setup_uses_ap_ip_and_keeps_mdns_for_file_editor() -> None:
    common = (ROOT / "main/common_define.hpp").read_text()
    menu = (ROOT / "main/menu_data/menu_data_system.inl").read_text()
    popup = (ROOT / "main/gui/gui_popup.inl").read_text()
    sampler = (ROOT / "main/sampler/sampler_app.cpp").read_text()

    assert 'wifi_setup_url = "http://192.168.4.1"' in common
    assert "QRCODE_URL_WIFI_SETUP" in menu
    setup_case = popup[popup.index("case def::qrcode_type_t::QRCODE_URL_WIFI_SETUP:"):
                       popup.index("case def::qrcode_type_t::QRCODE_URL_DEVICE:")]
    assert "wifi_setup_url" in setup_case
    assert "wifi_mdns" not in setup_case
    file_editor_case = popup[popup.index("case def::qrcode_type_t::QRCODE_URL_DEVICE:"):
                             popup.index("case def::qrcode_type_t::QRCODE_URL_DEVICE_NO_WIFI:")]
    assert "wifi_mdns" in file_editor_case
    assert sampler.count("wifi_setup_url") >= 2


def test_simple_genre_uses_current_song_format() -> None:
    menu = (ROOT / "main/menu_data/menu_data_arrays.inl").read_text()
    genre_menu = menu[menu.index('{ "Genre"'):menu.index('{ "Song"')]
    assert genre_menu.index('"Simple"') < genre_menu.index('"Pop"')
    assert "data_song_preset_genre_simple" in genre_menu

    preset_dir = ROOT / "incbin/preset/song_genre/simple"
    expected = ["Simple_Guitar.json", "Simple_Guitarx2.json", "Simple_Piano.json"]
    listing = (preset_dir / "_list.inl").read_text()
    assert [name for name in expected if name in listing] == expected
    for name in expected:
        song = json.loads((preset_dir / name).read_text())
        assert song["format"] == "KANTANPlayCore"
        assert song["type"] == "Song"
        assert song["version"] == 3
        assert song["num_slot"] == 8
        assert len(song["slot"]) == 8
        assert all("play_mode" not in slot for slot in song["slot"])


if __name__ == "__main__":
    test_names_and_ota_identity()
    test_sd_recovery_is_sequencer_only()
    test_check_build_has_no_publish_hook()
    test_external_device_matches_sampler_route_model()
    test_radio_lifecycle_and_sampler_isolation()
    test_wifi_setup_uses_ap_ip_and_keeps_mdns_for_file_editor()
    test_simple_genre_uses_current_song_format()
    print("PASS: Sequencer identity, SD recovery, safe build, Sampler-compatible External Device routing")

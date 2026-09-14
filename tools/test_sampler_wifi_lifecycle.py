#!/usr/bin/env python3
"""Regression checks for the Sampler Wi-Fi memory/service lifecycle."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    while True:
        brace = source.index("{", start)
        semicolon = source.find(";", start, brace)
        if semicolon < 0:
            break
        start = source.index(signature, semicolon + 1)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def ordered(body: str, *needles: str) -> None:
    positions = [body.index(needle) for needle in needles]
    assert positions == sorted(positions), (needles, positions)


def test_wifi_worker() -> None:
    source = (ROOT / "main/task_wifi.cpp").read_text()
    start = function_body(source, "static bool start_wifi_web_services(")
    stop = function_body(source, "static void stop_wifi_web_services(")

    ordered(start, "start_webserver()", "dns_server_start(")
    ordered(start, "start_webserver()", "mdns_init()")
    ordered(stop, "stop_webserver(", "dns_server_stop()", "mdns_free()")

    worker = source[source.index("void task_wifi_t::task_func("):]
    assert "setup_ap_waiting_for_auth" in worker
    assert "_ap_station_count == 0" in worker
    assert "file_editor_waiting_for_sta" in worker
    assert "_sta_state != STA_CONNECTED" in worker
    assert "web_services_may_start && _ws && _ws->wifi_started" in worker
    assert "_ap_station_count > 0 && _setup_http_seen" in worker


def test_sampler_arena() -> None:
    source = (ROOT / "main/sampler/sampler_app.cpp").read_text()
    suspend = function_body(source, "static void suspend_performance_ui_arena(")
    resume = function_body(source, "static void service_performance_ui_arena(")

    ordered(suspend, "performance_ui_arena_suspended = true",
            "clear_menu_preview()", "stop_all_audio(false)",
            "releaseUnusedSynthSustainCacheMemory()")
    assert "loop_events.empty() && loop_events.capacity() != 0" in suspend
    assert "std::vector<loop_event_t>().swap(loop_events)" in suspend
    assert "recording_internal_dma_reserve" in suspend

    assert "getWiFiSTAInfo()" in resume and "wsi_off" in resume
    assert "getWiFiAPInfo()" in resume and "wai_off" in resume
    assert "performance_ui_arena_stable_since_msec" in resume
    ordered(resume, "retain_internal_mic_dma_reserve()",
            "loop_events.reserve(loop_event_max)", "create_ui_dirty_canvases()",
            "apply_synth_tones(false)")


def test_idempotent_state_model() -> None:
    state = {"suspended": False, "pending": False, "restores": 0}

    def suspend() -> None:
        state["pending"] = False
        if state["suspended"]:
            return
        state["suspended"] = True

    def request_resume() -> None:
        if not state["suspended"] or state["pending"]:
            return
        state["pending"] = True

    def restore(network_off: bool, stable: bool) -> None:
        if not state["pending"] or not network_off or not stable:
            return
        state["pending"] = False
        state["suspended"] = False
        state["restores"] += 1

    suspend()
    suspend()
    request_resume()
    request_resume()
    restore(network_off=False, stable=True)
    restore(network_off=True, stable=False)
    assert state == {"suspended": True, "pending": True, "restores": 0}
    restore(network_off=True, stable=True)
    restore(network_off=True, stable=True)
    assert state == {"suspended": False, "pending": False, "restores": 1}


if __name__ == "__main__":
    test_wifi_worker()
    test_sampler_arena()
    test_idempotent_state_model()
    print("PASS: deferred AP/File Editor services, ordered teardown, guarded one-shot arena restore")

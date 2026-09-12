// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#include "sampler_amy_engine.hpp"

#if defined(KANPLAY_AMY_INTEGRATION) && !defined(M5UNIFIED_PC_BUILD)

#include <AMY-Arduino.h>
#include <Arduino.h>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>

// KANTAN Play owns all physical MIDI interfaces. AMY receives selected MIDI
// messages through the queue below and must not initialize or poll a UART.
extern "C" void run_midi() {}
extern "C" void stop_midi() {}
extern "C" void esp_poll_midi() {}
extern "C" void midi_out(uint8_t*, uint16_t) {}

namespace sampler_ns::sampler_amy_engine {
namespace {

// Nine performance voices use at most 18 oscillators. Two isolated audition
// oscillators keep menu previews from stealing or retuning a held note.
constexpr uint16_t max_oscillators = 20;
constexpr uint8_t musical_voices = 10;
constexpr uint32_t ring_frames = AMY_BLOCK_SIZE * 2u;
constexpr uint32_t ring_mask = ring_frames - 1u;
constexpr int32_t output_gain_numerator = 5;
constexpr int32_t output_gain_denominator = 2;
static_assert((ring_frames & ring_mask) == 0, "AMY ring must be a power of two");
static_assert(AMY_SAMPLE_RATE == 48000, "AMY integration must render at 48 kHz");

struct midi_message_t {
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

QueueHandle_t midi_queue = nullptr;
QueueHandle_t preview_queue = nullptr;
SemaphoreHandle_t ready_semaphore = nullptr;
TaskHandle_t render_task_handle = nullptr;
int16_t* output_ring = nullptr;
volatile uint32_t read_position = 0;
volatile uint32_t write_position = 0;
volatile uint32_t underruns = 0;
volatile bool engine_ready = false;
volatile bool radio_connection_paused = false;
volatile uint8_t active_voice_count = 0;
uint8_t rpn_msb[3] = { 127, 127, 127 };
uint8_t rpn_lsb[3] = { 127, 127, 127 };
float fine_tuning_cents[3] = {};
uint8_t pitch_bend_range[3] = { 2, 2, 2 };
uint16_t pitch_bend_value[3] = { 8192, 8192, 8192 };
uint8_t channel_volume[4] = { 127, 127, 127, 110 };
uint8_t channel_tone[3] = {};
uint8_t preview_part = 0;
uint8_t preview_tone = 0;
uint32_t voice_age = 0;

struct voice_slot_t {
  uint8_t note = 0;
  bool active = false;
  uint32_t age = 0;
};

voice_slot_t melody_voices[4];
voice_slot_t chord_voices[4];
voice_slot_t bass_voice[1];
voice_slot_t preview_voice[1];

constexpr uint8_t melody_osc_base = 0;
constexpr uint8_t bass_osc_base = 8;
constexpr uint8_t chord_osc_base = 10;
constexpr uint8_t preview_osc_base = 18;

struct tone_spec_t {
  const char* name;
  uint16_t wave[2];
  float gain[2];
  float detune[2];
  float duty[2];
  uint16_t attack_ms;
  uint16_t decay_ms;
  float sustain;
  uint16_t release_ms;
  float filter_hz;
  float filter_env_octaves;
  float resonance;
  uint8_t layers;
};

// Eight immediately distinct choices per part. They use only AMY's basic
// oscillators and filters: no upstream PCM or general patch bank. The second
// oscillator supplies audible harmonics/character rather than inaudible sub
// energy, which is important on the phone-sized built-in speaker.
static constexpr tone_spec_t tones[3][tone_count] = {
  {
    { "Razor Lead",  { SAW_DOWN, PULSE },    { .76f, .38f }, { 0.0f,  .05f }, { .50f, .32f }, 2,  85, .72f, 105, 1500, 1.75f, 1.2f, 2 },
    { "Pulse Bite",  { PULSE, PULSE },       { .72f, .34f }, { 0.0f, 12.00f }, { .22f, .62f }, 1,  75, .68f,  90, 1200, 2.15f, 1.8f, 2 },
    { "Twin Saw",    { SAW_DOWN, SAW_DOWN }, { .68f, .48f }, { -.06f,  .06f }, { .50f, .50f }, 4, 120, .70f, 150, 1350, 1.65f, 1.0f, 2 },
    { "Octave Lead", { SAW_DOWN, PULSE },    { .73f, .31f }, { 0.0f, 12.00f }, { .50f, .38f }, 2,  95, .66f, 110, 1450, 1.85f, 1.3f, 2 },
    { "Acid Pluck",  { SAW_DOWN, PULSE },    { .74f, .32f }, { 0.0f,  0.02f }, { .50f, .26f }, 1, 145, .16f,  75,  520, 3.10f, 3.6f, 2 },
    { "Brass Stab",  { SAW_DOWN, TRIANGLE }, { .72f, .36f }, { 0.0f,  0.00f }, { .50f, .50f }, 7, 115, .42f, 130,  780, 2.55f, 2.0f, 2 },
    { "Arcade Lead", { PULSE, SAW_DOWN },    { .72f, .34f }, { 0.0f,  7.00f }, { .18f, .50f }, 1, 105, .56f,  80, 1100, 2.20f, 1.5f, 2 },
    { "Neon Lead",   { PULSE, SAW_DOWN },    { .68f, .44f }, { -.04f,  .04f }, { .40f, .50f }, 3, 135, .62f, 170, 1700, 1.45f, 1.1f, 2 },
  },
  {
    { "Wide Saw",    { SAW_DOWN, SAW_DOWN }, { .48f, .34f }, { -.05f,  .05f }, { .50f, .50f }, 12, 190, .68f, 260, 1050, 1.55f, 1.0f, 2 },
    { "Pulse Chord", { PULSE, PULSE },       { .47f, .29f }, { 0.0f, 12.00f }, { .28f, .58f },  5, 125, .70f, 190,  900, 1.95f, 1.5f, 2 },
    { "Synth Brass", { SAW_DOWN, TRIANGLE }, { .49f, .31f }, { 0.0f,  0.00f }, { .50f, .50f }, 10, 150, .55f, 220,  650, 2.45f, 2.2f, 2 },
    { "House Stab",  { SAW_DOWN, PULSE },    { .51f, .28f }, { 0.0f, 12.00f }, { .50f, .24f },  1, 125, .18f,  90,  620, 2.85f, 2.8f, 2 },
    { "Fifth Stack", { SAW_DOWN, PULSE },    { .46f, .30f }, { 0.0f,  7.00f }, { .50f, .35f },  7, 160, .62f, 230, 1100, 1.60f, 1.2f, 2 },
    { "Neon Pad",    { PULSE, SAW_DOWN },    { .43f, .33f }, { -.04f,  .04f }, { .42f, .50f }, 22, 230, .66f, 330, 1350, 1.35f, 1.0f, 2 },
    { "Organ Stack", { PULSE, PULSE },       { .45f, .27f }, { 0.0f, 12.00f }, { .50f, .25f },  3,  85, .82f, 170, 1550, 1.15f, 0.8f, 2 },
    { "Dark Pulse",  { PULSE, TRIANGLE },    { .50f, .28f }, { 0.0f,  0.00f }, { .20f, .50f },  8, 175, .64f, 250,  520, 1.90f, 2.1f, 2 },
  },
  {
    { "Saw Bass",    { SAW_DOWN, SAW_DOWN }, { .82f, .42f }, { 0.0f,  0.03f }, { .50f, .50f }, 1, 105, .68f,  90,  420, 2.35f, 1.7f, 2 },
    { "Pulse Bass",  { PULSE, PULSE },       { .82f, .38f }, { 0.0f, 12.00f }, { .22f, .52f }, 1,  90, .66f,  80,  380, 2.55f, 2.0f, 2 },
    { "Knock Bass",  { SAW_DOWN, TRIANGLE }, { .84f, .36f }, { 0.0f, 12.00f }, { .50f, .50f }, 1, 115, .22f,  70,  360, 2.85f, 2.5f, 2 },
    { "Acid Bass",   { SAW_DOWN, PULSE },    { .80f, .40f }, { 0.0f,  0.00f }, { .50f, .18f }, 1, 145, .28f,  90,  300, 3.25f, 4.2f, 2 },
    { "Reese Bass",  { SAW_DOWN, SAW_DOWN }, { .72f, .58f }, { -.08f,  .08f }, { .50f, .50f }, 3, 135, .72f, 140,  500, 2.10f, 1.4f, 2 },
    { "Octave Bass", { SAW_DOWN, PULSE },    { .80f, .35f }, { 0.0f, 12.00f }, { .50f, .34f }, 1, 100, .62f,  85,  460, 2.30f, 1.6f, 2 },
    { "Growl Bass",  { PULSE, SAW_DOWN },    { .78f, .44f }, { 0.0f,  7.00f }, { .16f, .50f }, 2, 125, .58f, 105,  340, 2.80f, 3.2f, 2 },
    { "Rubber Bass", { PULSE, TRIANGLE },    { .82f, .38f }, { 0.0f, 12.00f }, { .38f, .50f }, 2, 170, .30f, 120,  330, 2.65f, 2.7f, 2 },
  },
};

const tone_spec_t& tone_spec(uint8_t part, uint8_t tone)
{
  return tones[std::min<uint8_t>(part, 2)][std::min<uint8_t>(tone, tone_count - 1)];
}

float oscillator_gain(uint8_t channel, uint8_t layer, const tone_spec_t& spec)
{
  return spec.gain[layer] * ((float)channel_volume[channel] / 127.0f);
}

void configure_osc(uint8_t osc, uint8_t channel, uint8_t layer,
                   const tone_spec_t& spec, float pan)
{
  amy_event event = amy_default_event();
  event.osc = osc;
  event.wave = spec.wave[layer];
  event.amp_coefs[COEF_CONST] = layer < spec.layers
    ? oscillator_gain(channel, layer, spec) : 0.0f;
  event.duty_coefs[COEF_CONST] = spec.duty[layer];
  event.pan_coefs[COEF_CONST] = pan;
  event.filter_type = FILTER_LPF;
  event.filter_freq_coefs[COEF_CONST] = spec.filter_hz;
  event.filter_freq_coefs[COEF_EG0] = spec.filter_env_octaves;
  event.resonance = spec.resonance;
  event.eg0_times[0] = spec.attack_ms;
  event.eg0_values[0] = 1.0f;
  event.eg0_times[1] = spec.decay_ms;
  event.eg0_values[1] = spec.sustain;
  event.eg0_times[2] = spec.release_ms;
  event.eg0_values[2] = 0.0f;
  amy_add_event(&event);
}

uint8_t oscillator_number(uint8_t channel, uint8_t voice, uint8_t layer)
{
  if (channel == 0) { return melody_osc_base + voice * 2 + layer; }
  if (channel == 1) { return chord_osc_base + voice * 2 + layer; }
  if (channel == 2) { return bass_osc_base + layer; }
  return preview_osc_base + layer;
}

void configure_channel(uint8_t channel, uint8_t part, uint8_t tone)
{
  const auto& spec = tone_spec(part, tone);
  const uint8_t voices = channel == 2 || channel == 3 ? 1 : 4;
  for (uint8_t voice = 0; voice < voices; ++voice) {
    configure_osc(oscillator_number(channel, voice, 0), channel, 0, spec, .44f);
    configure_osc(oscillator_number(channel, voice, 1), channel, 1, spec, .56f);
  }
}

void configure_instruments()
{
  for (uint8_t channel = 0; channel < 3; ++channel) {
    configure_channel(channel, channel, channel_tone[channel]);
  }
  configure_channel(3, 0, 0);
}

voice_slot_t* channel_slots(uint8_t channel, uint8_t* count)
{
  if (channel == 0) { *count = 4; return melody_voices; }
  if (channel == 1) { *count = 4; return chord_voices; }
  *count = 1;
  return channel == 2 ? bass_voice : preview_voice;
}

uint8_t oscillator_count(uint8_t channel)
{
  if (channel >= 3) { return tone_spec(preview_part, preview_tone).layers; }
  return tone_spec(channel, channel_tone[channel]).layers;
}

float channel_pitch_offset(uint8_t channel)
{
  if (channel >= 3) { return 0.0f; }
  const float bend = ((int32_t)pitch_bend_value[channel] - 8192)
                   / (pitch_bend_value[channel] >= 8192 ? 8191.0f : 8192.0f);
  return fine_tuning_cents[channel] / 100.0f
       + bend * (float)pitch_bend_range[channel];
}

float layer_note_offset(uint8_t channel, uint8_t layer)
{
  if (channel >= 3) { return tone_spec(preview_part, preview_tone).detune[layer]; }
  return tone_spec(channel, channel_tone[channel]).detune[layer];
}

void update_voice_pitch(uint8_t channel, uint8_t voice)
{
  uint8_t count = 0;
  voice_slot_t* slots = channel_slots(channel, &count);
  if (voice >= count || !slots[voice].active) { return; }
  for (uint8_t layer = 0; layer < oscillator_count(channel); ++layer) {
    amy_event event = amy_default_event();
    event.osc = oscillator_number(channel, voice, layer);
    event.midi_note = (float)slots[voice].note + channel_pitch_offset(channel)
                    + layer_note_offset(channel, layer);
    amy_add_event(&event);
  }
}

void apply_channel_pitch(uint8_t channel)
{
  if (channel >= 3) { return; }
  uint8_t count = 0;
  (void)channel_slots(channel, &count);
  for (uint8_t voice = 0; voice < count; ++voice) {
    update_voice_pitch(channel, voice);
  }
}

void set_channel_level(uint8_t channel, uint8_t midi_value)
{
  if (channel >= 3) { return; }
  channel_volume[channel] = midi_value;
  uint8_t voice_count = 0;
  (void)channel_slots(channel, &voice_count);
  for (uint8_t voice = 0; voice < voice_count; ++voice) {
    for (uint8_t layer = 0; layer < oscillator_count(channel); ++layer) {
      amy_event event = amy_default_event();
      event.osc = oscillator_number(channel, voice, layer);
      event.amp_coefs[COEF_CONST] = oscillator_gain(
        channel, layer, tone_spec(channel, channel_tone[channel]));
      amy_add_event(&event);
    }
  }
}

void note_off(uint8_t channel, uint8_t note)
{
  uint8_t count = 0;
  voice_slot_t* slots = channel_slots(channel, &count);
  for (uint8_t voice = 0; voice < count; ++voice) {
    if (!slots[voice].active || slots[voice].note != note) { continue; }
    for (uint8_t layer = 0; layer < oscillator_count(channel); ++layer) {
      amy_event event = amy_default_event();
      event.osc = oscillator_number(channel, voice, layer);
      event.velocity = 0.0f;
      amy_add_event(&event);
    }
    slots[voice].active = false;
    __atomic_sub_fetch(&active_voice_count, 1u, __ATOMIC_RELAXED);
  }
}

void all_notes_off(uint8_t channel)
{
  uint8_t count = 0;
  voice_slot_t* slots = channel_slots(channel, &count);
  for (uint8_t voice = 0; voice < count; ++voice) {
    if (slots[voice].active) { note_off(channel, slots[voice].note); }
  }
}

void note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
  uint8_t count = 0;
  voice_slot_t* slots = channel_slots(channel, &count);
  uint8_t selected = count;
  for (uint8_t voice = 0; voice < count; ++voice) {
    if (slots[voice].active && slots[voice].note == note) {
      selected = voice;
      break;
    }
    if (selected == count && !slots[voice].active) { selected = voice; }
  }
  if (selected == count) {
    selected = 0;
    for (uint8_t voice = 1; voice < count; ++voice) {
      if (slots[voice].age < slots[selected].age) { selected = voice; }
    }
    note_off(channel, slots[selected].note);
  }
  const bool was_active = slots[selected].active;
  slots[selected].note = note;
  slots[selected].active = true;
  if (!was_active) {
    __atomic_add_fetch(&active_voice_count, 1u, __ATOMIC_RELAXED);
  }
  slots[selected].age = ++voice_age;
  const float normalized_velocity = (float)velocity / 127.0f;
  for (uint8_t layer = 0; layer < oscillator_count(channel); ++layer) {
    amy_event event = amy_default_event();
    event.osc = oscillator_number(channel, selected, layer);
    event.midi_note = (float)note + channel_pitch_offset(channel)
                    + layer_note_offset(channel, layer);
    event.velocity = normalized_velocity;
    amy_add_event(&event);
  }
}

void apply_rpn_data_entry(uint8_t channel, uint8_t data_entry)
{
  if (channel >= 3 || rpn_msb[channel] != 0) { return; }
  if (rpn_lsb[channel] == 0) {
    pitch_bend_range[channel] = std::min<uint8_t>(24, data_entry);
  } else if (rpn_lsb[channel] == 1) {
    // SAM2695 compatibility path sends only the Data Entry MSB.
    fine_tuning_cents[channel] = ((float)data_entry - 63.5f) * (100.0f / 63.5f);
  } else {
    return;
  }
  apply_channel_pitch(channel);
}

void process_midi(const midi_message_t& message)
{
  if (message.status == 0xF1u) {
    all_notes_off(3);
    preview_part = std::min<uint8_t>(message.data1 >> 4, 2);
    preview_tone = std::min<uint8_t>(message.data1 & 0x0Fu, tone_count - 1);
    configure_channel(3, preview_part, preview_tone);
    // Preview velocity is fixed at the UI boundary and the note is carried in
    // data2, keeping this private command allocation-free.
    note_on(3, message.data2, 108);
    return;
  }
  if (message.status == 0xF2u) {
    all_notes_off(3);
    return;
  }
  const uint8_t command = message.status & 0xF0u;
  const uint8_t channel = message.status & 0x0Fu;
  if (channel >= 3) { return; }

  if (command == 0xC0u) {
    all_notes_off(channel);
    channel_tone[channel] = std::min<uint8_t>(message.data1, tone_count - 1);
    configure_channel(channel, channel, channel_tone[channel]);
    return;
  }
  if (command == 0xB0u) {
    if (message.data1 == 7) {
      set_channel_level(channel, message.data2);
      return;
    }
    if (message.data1 == 120 || message.data1 == 123) {
      all_notes_off(channel);
      return;
    }
    if (message.data1 == 101) { rpn_msb[channel] = message.data2; return; }
    if (message.data1 == 100) { rpn_lsb[channel] = message.data2; return; }
    if (message.data1 == 6) { apply_rpn_data_entry(channel, message.data2); return; }
  }
  if (command == 0xE0u) {
    pitch_bend_value[channel] = (uint16_t)message.data1
                              | ((uint16_t)message.data2 << 7);
    apply_channel_pitch(channel);
    return;
  }
  if (command == 0x90u && message.data2 != 0) {
    note_on(channel, message.data1, message.data2);
  } else if (command == 0x80u || command == 0x90u) {
    note_off(channel, message.data1);
  }
}

void render_task(void*)
{
  amy_config_t config = amy_default_config();
  config.features.reverb = 0;
  config.features.echo = 0;
  config.features.chorus = 0;
  config.features.partials = 0;
  config.features.custom = 0;
  config.features.audio_in = 0;
  config.features.default_synths = 0;
  config.features.startup_bleep = 0;
  config.platform.multicore = 0;
  config.platform.multithread = 0;
  config.midi = AMY_MIDI_IS_NONE;
  config.audio = AMY_AUDIO_IS_NONE;
  config.max_oscs = max_oscillators;
  config.ks_oscs = 0;
  config.max_sequencer_tags = 32;
  config.max_voices = musical_voices;
  // Instrument IDs follow MIDI's 1-based channels, so channels 1..3 require
  // slots 0..3 even though only three musical instruments are configured.
  config.max_synths = 4;
  config.max_memory_patches = 4;
  config.overload_threshold = 0;
  config.ram_caps_events = MALLOC_CAP_SPIRAM;
  config.ram_caps_sysex = MALLOC_CAP_SPIRAM;
  config.ram_caps_synth = MALLOC_CAP_SPIRAM;
  config.ram_caps_delay = MALLOC_CAP_SPIRAM;
  config.ram_caps_sample = MALLOC_CAP_SPIRAM;
  config.ram_caps_block = MALLOC_CAP_SPIRAM;
  // Feedback lines and algorithm scratch are block-local and tolerate cached
  // PSRAM. Keeping them out of scarce DRAM leaves enough contiguous memory for
  // the Bluedroid work queues while the final output blocks stay internal.
  config.ram_caps_fbl = MALLOC_CAP_SPIRAM;

  amy_start(config);
  configure_instruments();
  // Apply the allocation/configuration events before accepting MIDI.
  (void)amy_update();
  (void)amy_update();
  engine_ready = true;
  xSemaphoreGive(ready_semaphore);

  for (;;) {
    if (__atomic_load_n(&radio_connection_paused, __ATOMIC_ACQUIRE)) {
      // BLE link establishment has a short, high-priority controller burst.
      // Yield Core 0 and discard queued audio so resume never emits stale AMY
      // frames after the connection transaction.
      const uint32_t write = __atomic_load_n(&write_position, __ATOMIC_ACQUIRE);
      __atomic_store_n(&read_position, write, __ATOMIC_RELEASE);
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    midi_message_t message{};
    // Tone browsing can produce many preview changes faster than one audio
    // block. Keep only the newest request so AMY is never asked to rebuild a
    // backlog of filtered preview oscillators after a BLE connection pause.
    if (preview_queue != nullptr
     && xQueueReceive(preview_queue, &message, 0) == pdPASS) {
      process_midi(message);
    }
    while (xQueueReceive(midi_queue, &message, 0) == pdPASS) {
      process_midi(message);
    }

    const uint32_t read = __atomic_load_n(&read_position, __ATOMIC_ACQUIRE);
    const uint32_t write = __atomic_load_n(&write_position, __ATOMIC_RELAXED);
    // Keep exactly one AMY block ready. More buffering only adds Note On
    // latency; less leaves no protection from brief Core 0 display work.
    if (write - read >= AMY_BLOCK_SIZE) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
      continue;
    }

    int16_t* block = amy_update();
    if (block == nullptr) {
      vTaskDelay(1);
      continue;
    }
    for (uint32_t frame = 0; frame < AMY_BLOCK_SIZE; ++frame) {
      const uint32_t destination = ((write + frame) & ring_mask) * 2u;
      output_ring[destination] = block[frame * 2u];
      output_ring[destination + 1u] = block[frame * 2u + 1u];
    }
    __atomic_store_n(&write_position, write + AMY_BLOCK_SIZE, __ATOMIC_RELEASE);
  }
}

} // namespace

bool start()
{
  if (engine_ready) { return true; }
  if (midi_queue == nullptr) {
    midi_queue = xQueueCreate(128, sizeof(midi_message_t));
  }
  if (preview_queue == nullptr) {
    preview_queue = xQueueCreate(1, sizeof(midi_message_t));
  }
  if (ready_semaphore == nullptr) {
    ready_semaphore = xSemaphoreCreateBinary();
  }
  if (output_ring == nullptr) {
    output_ring = static_cast<int16_t*>(heap_caps_calloc(
      ring_frames * 2u, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (midi_queue == nullptr || preview_queue == nullptr
   || ready_semaphore == nullptr || output_ring == nullptr) {
    return false;
  }
  if (render_task_handle == nullptr) {
    // AMY never calls BLE/Wi-Fi or flash-cache-disabled APIs. Its stack can
    // therefore live in PSRAM, preserving 8 KB of scarce internal RAM for
    // Bluedroid's GATT registration/open transaction. Keep the TCB internal,
    // as required by ESP-IDF.
    BaseType_t task_result = xTaskCreatePinnedToCoreWithCaps(
      render_task, "amy_render", 8192, nullptr, 4, &render_task_handle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_result != pdPASS) {
      // A PSRAM allocation failure must not disable the synthesizer entirely.
      task_result = xTaskCreatePinnedToCore(render_task, "amy_render", 8192,
                                            nullptr, 4, &render_task_handle, 0);
    }
    if (task_result != pdPASS) {
      render_task_handle = nullptr;
      return false;
    }
  }
  return xSemaphoreTake(ready_semaphore, pdMS_TO_TICKS(3000)) == pdTRUE;
}

bool handlesMidi(uint8_t status)
{
  const uint8_t command = status & 0xF0u;
  const uint8_t channel = status & 0x0Fu;
  return engine_ready && channel < 3 && command >= 0x80u && command <= 0xE0u;
}

bool sendMidi(uint8_t status, uint8_t data1, uint8_t data2)
{
  if (!handlesMidi(status) || midi_queue == nullptr) { return false; }
  // BLE discovery may temporarily yield AMY's Core-0 renderer. A local or
  // received musical event is a stronger real-time priority than continuing
  // that pause, and the event itself must not be lost as the wake-up trigger.
  if (__atomic_load_n(&radio_connection_paused, __ATOMIC_ACQUIRE)) {
    setRadioConnectionPaused(false);
  }
  const midi_message_t message{ status, data1, data2 };
  return xQueueSend(midi_queue, &message, 0) == pdPASS;
}

const char* toneName(uint8_t part, uint8_t tone)
{
  return tone_spec(part, tone).name;
}

bool previewTone(uint8_t part, uint8_t tone, uint8_t note, uint8_t velocity)
{
  if (!engine_ready || preview_queue == nullptr || part >= 3 || tone >= tone_count) {
    return false;
  }
  if (__atomic_load_n(&radio_connection_paused, __ATOMIC_ACQUIRE)) {
    setRadioConnectionPaused(false);
  }
  (void)velocity;
  const midi_message_t message{
    0xF1u, (uint8_t)((part << 4) | tone), note
  };
  return xQueueOverwrite(preview_queue, &message) == pdPASS;
}

void stopPreview()
{
  if (!engine_ready || preview_queue == nullptr) { return; }
  const midi_message_t message{ 0xF2u, 0, 0 };
  (void)xQueueOverwrite(preview_queue, &message);
}

bool pcmSample(uint8_t preset, const int16_t** data, uint32_t* frames,
               uint32_t* sample_rate)
{
  if (!data || !frames || !sample_rate || preset >= pcm_samples) { return false; }
  const pcm_map_t source = pcm_map[preset];
  if (source.length == 0) { return false; }
  *data = pcm + source.offset;
  *frames = source.length;
  // pcm_tiny.h in the pinned AMY dependency is a 22.05 kHz bank.
  *sample_rate = 22050;
  return true;
}

bool readFrame(int32_t* left, int32_t* right)
{
  if (left) { *left = 0; }
  if (right) { *right = 0; }
  if (!engine_ready || output_ring == nullptr) { return false; }
  const uint32_t read = __atomic_load_n(&read_position, __ATOMIC_RELAXED);
  const uint32_t write = __atomic_load_n(&write_position, __ATOMIC_ACQUIRE);
  if (read == write) {
    __atomic_add_fetch(&underruns, 1u, __ATOMIC_RELAXED);
    return false;
  }
  const uint32_t source = (read & ring_mask) * 2u;
  const auto scale_sample = [](int16_t sample) {
    const int64_t scaled = (int64_t)sample * 65536 * output_gain_numerator
                         / output_gain_denominator;
    return (int32_t)std::clamp<int64_t>(scaled, INT32_MIN, INT32_MAX);
  };
  if (left) { *left = scale_sample(output_ring[source]); }
  if (right) { *right = scale_sample(output_ring[source + 1u]); }
  __atomic_store_n(&read_position, read + 1u, __ATOMIC_RELEASE);
  if (((read + 1u) % 48u) == 0 && render_task_handle != nullptr) {
    xTaskNotifyGive(render_task_handle);
  }
  return true;
}

uint32_t underrunCount()
{
  return __atomic_load_n(&underruns, __ATOMIC_RELAXED);
}

void setRadioConnectionPaused(bool paused)
{
  if (paused) {
    __atomic_store_n(&radio_connection_paused, true, __ATOMIC_RELEASE);
    return;
  }
  const uint32_t write = __atomic_load_n(&write_position, __ATOMIC_ACQUIRE);
  __atomic_store_n(&read_position, write, __ATOMIC_RELEASE);
  __atomic_store_n(&radio_connection_paused, false, __ATOMIC_RELEASE);
  if (render_task_handle != nullptr) { xTaskNotifyGive(render_task_handle); }
}

bool radioConnectionPaused()
{
  return __atomic_load_n(&radio_connection_paused, __ATOMIC_ACQUIRE);
}

bool hasActiveVoices()
{
  return __atomic_load_n(&active_voice_count, __ATOMIC_RELAXED) != 0;
}

} // namespace sampler_ns::sampler_amy_engine

#endif

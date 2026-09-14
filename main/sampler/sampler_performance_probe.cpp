// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#if defined(KANPLAY_SAMPLER) && defined(KANPLAY_SAMPLER_LATENCY_PROBE)

#include <M5Unified.h>
#include <stdio.h>

#include "sampler_performance_probe.hpp"

namespace sampler_ns::performance_probe {

static constexpr uint8_t histogram_bins = 33;
struct metric_state_t {
  volatile uint32_t count = 0;
  volatile uint32_t sum_usec = 0;
  volatile uint32_t sum_high = 0;
  volatile uint32_t max_usec = 0;
  volatile uint32_t histogram[histogram_bins] = {};
};
static metric_state_t metrics[(uint8_t)metric_t::count];
static volatile uint32_t counters[(uint8_t)counter_t::count] = {};

static uint32_t bin_width_usec(metric_t metric)
{
  return metric >= metric_t::rec_event_store ? 100u : 1000u;
}

void count(counter_t counter)
{
  __atomic_fetch_add(&counters[(uint8_t)counter], 1u, __ATOMIC_RELAXED);
}

static const char* metric_name(metric_t metric)
{
  static constexpr const char* names[] = {
    "input-on-queue", "input-off-queue", "live-on-audio", "live-off-audio",
    "rec-on-audio", "rec-off-audio", "rec-store", "rec-batch", "voice-alloc",
    "i2s-block", "i2s-1-2-voices", "i2s-3-4-voices",
    "i2s-5-6-voices", "i2s-7plus-voices"
  };
  return names[(uint8_t)metric];
}

void record(metric_t metric, uint32_t usec)
{
  auto& state = metrics[(uint8_t)metric];
  __atomic_fetch_add(&state.count, 1u, __ATOMIC_RELAXED);
  const uint32_t previous = __atomic_fetch_add(&state.sum_usec, usec, __ATOMIC_RELAXED);
  if (previous > UINT32_MAX - usec) {
    __atomic_fetch_add(&state.sum_high, 1u, __ATOMIC_RELAXED);
  }
  uint32_t maximum = __atomic_load_n(&state.max_usec, __ATOMIC_RELAXED);
  while (usec > maximum
      && !__atomic_compare_exchange_n(&state.max_usec, &maximum, usec, false,
                                      __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
  const uint32_t width = bin_width_usec(metric);
  const uint8_t bin = (uint8_t)(usec >= width * 32u ? 32u : usec / width);
  __atomic_fetch_add(&state.histogram[bin], 1u, __ATOMIC_RELAXED);
}

uint32_t eventUsec(uint32_t event_msec)
{
  const uint32_t now_msec = M5.millis();
  const uint32_t age_msec = now_msec - event_msec;
  return M5.micros() - age_msec * 1000u;
}

uint32_t clockUsec(void)
{
  return M5.micros();
}

void recordInputEdge(bool pressed, uint32_t event_msec)
{
  record(pressed ? metric_t::input_press_queue : metric_t::input_release_queue,
         (M5.millis() - event_msec) * 1000u);
}

static uint32_t percentile95(const metric_state_t& state, uint32_t count,
                              uint32_t width)
{
  const uint32_t target = (uint32_t)(((uint64_t)count * 95u + 99u) / 100u);
  uint32_t accumulated = 0;
  for (uint8_t bin = 0; bin < histogram_bins; ++bin) {
    accumulated += __atomic_load_n(&state.histogram[bin], __ATOMIC_RELAXED);
    if (accumulated >= target) { return bin == 32 ? UINT32_MAX : (bin + 1u) * width; }
  }
  return 0;
}

void reportIfDue(uint32_t now_msec)
{
  static uint32_t previous_msec = 0;
  if (now_msec - previous_msec < 5000u) { return; }
  previous_msec = now_msec;
  for (uint8_t index = 0; index < (uint8_t)metric_t::count; ++index) {
    const auto& state = metrics[index];
    const uint32_t count = __atomic_load_n(&state.count, __ATOMIC_RELAXED);
    if (!count) { continue; }
    const uint64_t sum = ((uint64_t)__atomic_load_n(&state.sum_high, __ATOMIC_RELAXED) << 32)
                       | __atomic_load_n(&state.sum_usec, __ATOMIC_RELAXED);
    const uint32_t maximum = __atomic_load_n(&state.max_usec, __ATOMIC_RELAXED);
    const uint32_t width = bin_width_usec((metric_t)index);
    const uint32_t p95 = percentile95(state, count, width);
    printf("PERF %s n=%u avg=%uus p95%s%uus max=%uus\n",
           metric_name((metric_t)index), (unsigned)count,
           (unsigned)(sum / count), p95 == UINT32_MAX ? ">=" : "<",
           (unsigned)(p95 == UINT32_MAX ? width * 32u : p95),
           (unsigned)maximum);
  }
  printf("PERF deadline-miss=%u read-error=%u write-error=%u\n",
         (unsigned)__atomic_load_n(&counters[0], __ATOMIC_RELAXED),
         (unsigned)__atomic_load_n(&counters[1], __ATOMIC_RELAXED),
         (unsigned)__atomic_load_n(&counters[2], __ATOMIC_RELAXED));
}

} // namespace sampler_ns::performance_probe

#endif

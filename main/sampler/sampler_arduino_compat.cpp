// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#if defined(KANPLAY_CUSTOM_ARDUINO_SDK) && defined(ARDUINO_ARCH_ESP32)

#include <cstdarg>

extern "C" int log_printfv(const char* format, va_list args);

// Pioarduino's custom SDK link flags wrap log_printf, but the generated
// framework does not provide the forwarding symbol included in stock builds.
extern "C" __attribute__((weak)) int __wrap_log_printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  const int result = log_printfv(format, args);
  va_end(args);
  return result;
}

#endif

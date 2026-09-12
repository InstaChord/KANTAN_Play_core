Import("env")

import os

# AMY ships src/usb.h while Arduino-ESP32 ships cores/esp32/USB.h. On the
# default case-insensitive macOS filesystem, a project-wide <USB.h> can resolve
# to AMY's unrelated header. Keep this workaround local to the probe build.
platform = env.PioPlatform()
framework_dir = platform.get_package_dir("framework-arduinoespressif32")
if framework_dir:
    core_include = env.Dir("cores/esp32", framework_dir).get_abspath()
    env.Prepend(CCFLAGS=["-I" + core_include])

# AMY 1.2.108 hard-codes 44.1 kHz for ESP targets. The audible integration
# prototype must share KANTAN Sampler's native 48 kHz I2S clock. Patch only the
# environment-local dependency copy; the probe deliberately stays at 44.1 kHz.
if env["PIOENV"] == "sampler_s3_amy_integration":
    amy_header = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],
        "AMY Synthesizer", "src", "amy.h")
    if os.path.isfile(amy_header):
        with open(amy_header, "r", encoding="utf-8") as source:
            contents = source.read()
        old = "#define AMY_SAMPLE_RATE 44100 "
        new = "#define AMY_SAMPLE_RATE 48000 "
        if old in contents:
            with open(amy_header, "w", encoding="utf-8") as destination:
                destination.write(contents.replace(old, new, 1))
        elif new not in contents:
            raise RuntimeError("Unable to apply AMY 48 kHz integration patch")

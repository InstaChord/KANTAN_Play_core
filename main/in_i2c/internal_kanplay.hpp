// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_INTERNAL_KANPLAY_HPP
#define KANPLAY_INTERNAL_KANPLAY_HPP

#include "../system_registry.hpp"

#include "internal_es8388.hpp"
#include "internal_si5351.hpp"
#include "internal_bmi270.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// かんぷれ本体制御用クラスの基本形。本体のハードウェアのバリエーションに応じて派生型を作る
class interface_internal_kanplay_t {
public:
    virtual bool init(void) = 0;
    virtual bool update(void) = 0;
    virtual void setupInterrupt(void) {}

    virtual uint8_t getFirmwareVersion(void) { return 0; }
    virtual bool checkUpdate(void) { return 0; }
    virtual bool execFirmwareUpdate(void) { return 0; }
    virtual void mute(void) {}
};

class internal_kanplay_t : public interface_internal_kanplay_t {
public:
    bool init(void) override;
    bool update(void) override;
    void setupInterrupt(void) override;

    uint8_t getFirmwareVersion(void) override;
    bool checkUpdate(void) override;
    bool execFirmwareUpdate(void) override;
    void mute(void) override;
protected:
    uint32_t calcImuStandardDeviation(void);
    void updateImuVelocity(void);

    internal_es8388_t internal_es8388;
    internal_si5351_t internal_si5351;
    internal_bmi270_t internal_bmi270;

    registry_t::history_code_t rgbled_history_code = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

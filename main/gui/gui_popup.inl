// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

struct ui_background_t : public ui_container_t {
  void loop(draw_param_t* param);
  void showOverlay(draw_param_t* param, uint32_t msec, float textsize, const char* text0, const char* text1 = nullptr,
            const char* text2 = nullptr, const char* text3 = nullptr,
            const char* text4 = nullptr, const char* text5 = nullptr);
};
static ui_background_t ui_background;

struct ui_timer_popup_t : public ui_base_t
{
protected:
  rect_t _show_rect;
  rect_t _hide_rect;
  int16_t _remain_time = 0;
public:
  void setShowRect(const rect_t &rect) {
    _show_rect = rect;
  }
  void setHideRect(const rect_t &rect) {
    _hide_rect = rect;
  }

  // void update(draw_param_t *param, int offset_x, int offset_y) override {
  //   ui_base_t::update(param, offset_x, offset_y);
  // }

  // 時間経過を管理し表示状態を変更する関数
  void procTime(int step) {
    if (step < 0) {
      if (_remain_time >= 0) {
        _remain_time += step;
        if (_remain_time < 0) {
          setTargetRect(_hide_rect);
        }
      }
    } else if (step) {
      _remain_time = step;
      setTargetRect(_show_rect);
    }
  }
  void close(void) {
    if (_remain_time) {
      _remain_time = 0;
      setTargetRect(_hide_rect);
    }
  }
};

struct ui_popup_notify_t : public ui_timer_popup_t
{
protected:
  std::string _text;
  uint32_t _text_color;
  registry_t::history_code_t _history_code;
  def::notify_type_t _notify_type;
  system_registry_t::reg_popup_notify_t::category_t _category;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool updated = false;
    if (system_registry->popup_notify.getPopupHistory(_history_code, _notify_type, _category))
    {
      auto text = def::notify_name_array.at(_notify_type);
      const char* t = text->get();

      _text_color = 0xFFFF00u;
      if (t) {
        _text = t;
        switch (_category) {
        case system_registry_t::reg_popup_notify_t::category_t::SUCCESS_NOTIFY:
          _text += " : OK";
          _text_color = 0x20FF20u;
          break;
        case system_registry_t::reg_popup_notify_t::category_t::ERROR_NOTIFY:
          _text += " : Error";
          _text_color = 0xFF8080u;
          break;
        default:
          break;
        }
        _gfx->setTextSize(1, 2);
        int w = _gfx->textWidth(_text.c_str()) + 16;
        int h = _gfx->fontHeight() + 12;
        int x = (disp_width - w) >> 1;
        int y = (disp_height - h) >> 1;
        setTargetRect({ x, y, w, h });
        setShowRect(getTargetRect());
      }
      updated = true;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    procTime(updated ? 1500 : -param->smooth_step);
    ui_timer_popup_t::update_impl(param, offset_x, offset_y);
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    int x = offset_x + (_client_rect.w >> 1);
    int y = offset_y + (_client_rect.h >> 1);
// M5_LOGD("x:%d y:%d offset_x:%d offset_y:%d", x, y, offset_x, offset_y);
    canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, 0);
    // image_dark_shift(canvas, offset_x, offset_y, _client_rect.w, _client_rect.h, 2);
    canvas->drawRect(offset_x+2, offset_y+2, _client_rect.w-4, _client_rect.h-4, 0xFFFFFFu);

    canvas->setTextDatum(m5gfx::datum_t::middle_center);
    canvas->setTextColor(_text_color);
    canvas->setTextSize(1, 2);
    canvas->drawString(_text.c_str(), x, y);
  }
};
ui_popup_notify_t ui_popup_notify;

// A restart is a user-visible operation, not an abrupt side effect of a menu
// choice. This modal covers every page while settings are saved and the USB
// power/radio owners shut down. Inputs are ignored by task_operator meanwhile.
struct ui_restart_notice_t : public ui_base_t {
  bool _was_active = false;
  uint32_t _last_animation_msec = 0;
public:
  ui_restart_notice_t() {
    setClientRect({0, 0, disp_width, disp_height});
    setTargetRect(getClientRect());
  }
  ui_base_t* searchUI(int32_t x, int32_t y) override {
    return sequencer_external::restartNoticeActive() ? ui_base_t::searchUI(x, y) : nullptr;
  }
protected:
  void update_impl(draw_param_t* param, int offset_x, int offset_y) override {
    const bool active = sequencer_external::restartNoticeActive();
    const uint32_t now = M5.millis();
    if (active != _was_active || (active && uint32_t(now - _last_animation_msec) >= 120)) {
      _was_active = active;
      _last_animation_msec = now;
      param->addInvalidatedRect({offset_x, offset_y, disp_width, disp_height});
    }
  }
  void draw_impl(draw_param_t*, M5Canvas* canvas, int32_t offset_x,
                 int32_t offset_y, const rect_t*) override {
    if (!sequencer_external::restartNoticeActive()) { return; }
    canvas->fillRect(offset_x, offset_y, disp_width, disp_height, 0x08080Cu);
    canvas->drawRect(offset_x + 4, offset_y + 4, disp_width - 8, disp_height - 8, 0x40A0FFu);
    canvas->setFont(&fonts::efontJA_16_b);
    canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
    canvas->setTextColor(TFT_WHITE, 0x08080Cu);
    canvas->setTextSize(1, 1);
    canvas->drawString(sequencer_external::restartNoticeTitle(), offset_x + disp_width / 2, offset_y + 76);
    canvas->setTextColor(0xA0D0FFu, 0x08080Cu);
    canvas->drawString(sequencer_external::restartNoticeTarget(), offset_x + disp_width / 2, offset_y + 122);
    canvas->setTextColor(TFT_WHITE, 0x08080Cu);
    canvas->drawString(sequencer_external::restartNoticeDetail(), offset_x + disp_width / 2, offset_y + 170);
    canvas->setTextColor(0xA0D0FFu, 0x08080Cu);
    canvas->drawString(localize_text_t{"Restarting safely...", "安全に再起動しています..."}.get(),
                       offset_x + disp_width / 2, offset_y + 198);

    // Indeterminate animation communicates progress without promising a fake percentage.
    const int active_dot = (M5.millis() / 240) % 3;
    for (int i = 0; i < 3; ++i) {
      const uint32_t color = i == active_dot ? 0x40A0FFu : 0x203050u;
      canvas->fillRoundRect(offset_x + 92 + i * 20, offset_y + 226, 12, 12, 3, color);
    }
    canvas->setTextColor(0xFFD080u, 0x08080Cu);
    canvas->drawString(localize_text_t{"Please do not turn off the power", "電源を切らないでください"}.get(),
                       offset_x + disp_width / 2, offset_y + 270);
  }
};
static ui_restart_notice_t ui_restart_notice;

struct ui_popup_qr_t : public ui_base_t
{
protected:
  M5Canvas _qr_canvas;
  uint32_t _caption_color;
  def::qrcode_type_t _qr_type = def::qrcode_type_t::QRCODE_NONE;
  const char* _caption = nullptr;
  const char* _caption2 = nullptr;
  uint32_t _caption2_color = TFT_BLACK;
  float _zoom = 0.0f;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool updated = _client_rect != _target_rect;
    ui_base_t::update_impl(param, offset_x, offset_y);
    def::qrcode_type_t qr_type = system_registry->popup_qr.getQRCodeType();
    if (_qr_type != qr_type) {
      if (!_target_rect.empty()) {
        int x = disp_width >> 1;
        // int y = disp_height >> 1;
        _target_rect = { x, x, 0, 0 };
      } else if (_client_rect.empty()) {
        _qr_type = qr_type;
        _qr_canvas.deleteSprite();
        _qr_canvas.setPsram(true);
        _qr_canvas.setColorDepth(1);
        if (qr_type != def::qrcode_type_t::QRCODE_NONE) {
          _qr_canvas.createSprite(39, 39);
          _qr_canvas.fillScreen(TFT_WHITE);

          static constexpr int qr_width = 39 * 5;
          static constexpr int width = qr_width + 10;
          static constexpr int height = qr_width + 48;
          _target_rect = { (disp_width - width) >> 1, (disp_height - height) >> 1, width, height };

          uint32_t color = 0xC0C0C0;
          char buf[64] = {0,};
          _caption2 = nullptr;
          _caption2_color = TFT_BLACK;
          switch (qr_type) {
          default:
          case def::qrcode_type_t::QRCODE_URL_MANUAL:
            _caption = "Scan for User Manual";
            _caption2 = "kantan-play.com/core/manual/";
            snprintf(buf, sizeof(buf), "%s", def::app::url_manual);
            break;
          case def::qrcode_type_t::QRCODE_AP_SSID:
            _caption = "Scan for WiFi Setup";
            snprintf(buf, sizeof(buf), "WIFI:S:%s;T:%s;P:%s;;", def::app::wifi_ap_ssid, def::app::wifi_ap_type, def::app::wifi_ap_pass);
            break;
          case def::qrcode_type_t::QRCODE_URL_WIFI_SETUP:
            // The setup AP does not advertise mDNS. iOS treats .local as an
            // mDNS-only name, so both the visible fallback and QR must use
            // the AP gateway address.
            _caption = def::app::wifi_setup_url;
            _caption2 = "Scan QR or type IP address";
            snprintf(buf, sizeof(buf), "%s/", def::app::wifi_setup_url);
            color = 0xFFFF00u;
            break;
          case def::qrcode_type_t::QRCODE_URL_DEVICE:
            _caption = "http://kanplay.local";
            _caption2 = "Scan QR or type URL";
            snprintf(buf, sizeof(buf), "http://%s.local/", def::app::wifi_mdns);
            color = 0xFFFF00u;
            break;
          case def::qrcode_type_t::QRCODE_URL_DEVICE_NO_WIFI:
            _caption = "http://kanplay.local";
            _caption2 = "Connect WiFi first";
            _caption2_color = TFT_RED;
            snprintf(buf, sizeof(buf), "http://%s.local/", def::app::wifi_mdns);
            color = 0x606060u;
            break;
          case def::qrcode_type_t::QRCODE_URL_SYSTEM_INFO:
            _caption = def::app::url_system_info;
            snprintf(buf, sizeof(buf), "%s", def::app::url_system_info);
            break;
          }
          _caption_color = color;
          _qr_canvas.qrcode(buf);
        }
      }
    }
    if (updated) {
      _zoom = ((float)39 * 5) / _qr_canvas.width();
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    auto cr = getClientRect();
    canvas->fillScreen(_caption_color);
    int qr_width = _qr_canvas.width() * _zoom;
    _qr_canvas.pushRotateZoom(canvas, offset_x + (cr.w >> 1), offset_y + (qr_width >> 1), 0.0f, _zoom, _zoom);
    canvas->drawRect(offset_x + 1, offset_y + 1, cr.w - 2, cr.h - 2, TFT_DARKGRAY);
    int cx = offset_x + (cr.w >> 1);
    canvas->setTextSize(_zoom / 5, _zoom / 2.5f);
    canvas->setTextDatum(m5gfx::datum_t::bottom_center);
    if (_caption2) {
      canvas->setTextColor(TFT_BLACK);
      canvas->drawString(_caption, cx, offset_y + cr.h - 26);
      canvas->setTextColor(_caption2_color);
      canvas->drawString(_caption2, cx, offset_y + cr.h - 2);
    } else if (_caption) {
      canvas->setTextColor(TFT_BLACK);
      canvas->drawString(_caption, cx, offset_y + cr.h - 2);
    }
  }
};
ui_popup_qr_t ui_popup_qr;

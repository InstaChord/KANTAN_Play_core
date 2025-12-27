// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <memory>

#include "gui.hpp"
#include "resource_icon.hpp"

#include "system_registry.hpp"
#include "file_manage.hpp"
#include "menu_data.hpp"

#if CORE_DEBUG_LEVEL > 3
// #define DEBUG_GUI
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
// extern instance
gui_t gui;

static M5GFX* _gfx;

void gui_t::startWrite(void) { _gfx->startWrite(); }
void gui_t::endWrite(void) { _gfx->endWrite(); }


static constexpr const int32_t header_height = 24;
static constexpr const int32_t main_btns_height = 96;
static constexpr const int32_t sub_btns_height = 32;
static constexpr const size_t menu_header_height = 32;
static constexpr const int32_t disp_width = 240;
static constexpr const int32_t disp_height = 320;
static constexpr const int32_t main_area_width = disp_width;
static constexpr const int32_t main_area_height = disp_height - header_height - main_btns_height - sub_btns_height;

static int smooth_move(int dst, int src, int step) {
  static constexpr const uint8_t mul_table[] = {
    22, 31, 38, 44, 50, 54, 59, 63, 67, 71, 74, 77, 81, 84, 87, 90, 92, 95, 98, 100, 103, 105, 108, 110, 112, 114, 117, 119, 121, 123, 125, 127, 129, 131, 133, 135, 137, 138, 140, 142, 144, 146, 147, 149, 151, 152, 154, 156, 157, 159, 161, 162, 164, 165, 167, 168, 170, 171, 173, 174, 176, 177, 179, 180, 181, 183, 184, 186, 187, 188, 190, 191, 192, 194, 195, 196, 198, 199, 200, 201, 203, 204, 205, 206, 208, 209, 210, 211, 212, 214, 215, 216, 217, 218, 220, 221, 222, 223, 224, 225, 226, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
  };
  if (dst != src) {
    if (step >= (int)(sizeof(mul_table)/sizeof(mul_table[0]))) {
      src = dst;
    } else {
      int diff = abs(dst - src);
      int mul = mul_table[step];
      diff = (255 + diff * mul) >> 8;
      src += (dst < src) ? -(diff) : diff;
    }
  }
  return src;
/*/
  if (dst != src && step > 0)
  {
    src += ((dst - src) * step + (dst < src ? 0 : 64)) >> 6;
    // src += ((dst - src) * step + (dst < src ? 0 : 256)) >> 8;
  }
  return src;
//*/
}

struct rect_t
{
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  constexpr rect_t(void) : x{0}, y{0}, w{0}, h{0}
  {
  }
  constexpr rect_t(int x_, int y_, int w_, int h_)
      : x{(int16_t)x_}, y{(int16_t)y_}, w{(int16_t)w_}, h{(int16_t)h_}
  {
  }
  inline constexpr int top(void) const
  {
    return y;
  }
  inline constexpr int left(void) const
  {
    return x;
  }
  inline constexpr int right(void) const
  {
    return x + w;
  }
  inline constexpr int bottom(void) const
  {
    return y + h;
  }
  inline constexpr bool empty(void) const
  {
    return w <= 0 || h <= 0;
  }
  void clear(void) { x = 0; y = 0; w = 0; h = 0; }

  inline bool operator==(const rect_t &src) const
  {
    return x == src.x && y == src.y && w == src.w && h == src.h;
  }
  inline bool operator!=(const rect_t &src) const
  {
    return !operator==(src);
  }
  inline bool isContain(int32_t target_x, int32_t target_y) const
  {
    return x <= target_x && target_x < x + w && y <= target_y && target_y < y + h;
  }
  // 矩形同士が重なっているか判定する
  inline bool isIntersect(const rect_t &src) const
  {
    return (x < src.right() && src.x < right() && y < src.bottom() && src.y < bottom());
  }

  bool smooth_move(const rect_t &src, int step) {
    if (operator==(src)) return false;
    int new_h = kanplay_ns::smooth_move(src.bottom(), bottom(), step);
    int new_y = kanplay_ns::smooth_move(src.y, y, step);
    new_h -= new_y;
    h = new_h;
    y = new_y;
    int new_w = kanplay_ns::smooth_move(src.right(), right(), step);
    int new_x = kanplay_ns::smooth_move(src.x, x, step);
    new_w -= new_x;
    w = new_w;
    x = new_x;
    return true;
  }
};

// 矩形同士のAND
static rect_t rect_and(const rect_t &a, const rect_t &b)
{
  int new_x = std::max(a.x, b.x);
  int new_y = std::max(a.y, b.y);
  int new_r = std::min(a.right(), b.right());
  int new_b = std::min(a.bottom(), b.bottom());
  return {new_x, new_y, new_r - new_x, new_b - new_y};
}

// 矩形同士の OR
static rect_t rect_or(const rect_t &a, const rect_t &b)
{
  int new_x = std::min(a.x, b.x);
  int new_y = std::min(a.y, b.y);
  int new_r = std::max(a.right(), b.right());
  int new_b = std::max(a.bottom(), b.bottom());
  return {new_x, new_y, new_r - new_x, new_b - new_y};
}

struct draw_param_t
{
  static constexpr const int max_clip_rect = 23;
  rect_t clip_rect[max_clip_rect];
  rect_t invalidated_rect[max_clip_rect];
  uint32_t current_msec = 0;
  uint32_t prev_msec = 0;
  uint8_t smooth_step = 0;
  bool hasInvalidated;
  void resetClipRect(void)
  {
    // 描画時の画面分割単位を設定する

    // ※ ここの分割面積を変更する場合は、max_disp_buf_pixels の値も変更すること
    // max_disp_buf_pixels の値は分割面積のうち最大のものが収まるように設定する

    static constexpr const int part_width = main_area_width / 3;
    static constexpr const int part_height = main_area_height >> 1;
    static constexpr const int main_btn_width = 48;

    static constexpr const int y0 = header_height;
    static constexpr const int y1 = y0 + part_height;
    static constexpr const int y2 = y1 + part_height;
    static constexpr const int y3 = y2 + sub_btns_height;
    static constexpr const int w0 = main_btn_width;

    // ヘッダ部
    clip_rect[0] = {               0, 0, disp_width>>1, header_height };  // 120 x 24 = 2880
    clip_rect[1] = { disp_width >> 1, 0, disp_width>>1, header_height };

    auto pw_half = part_width >> 1;
    // ６個のパート
    clip_rect[ 2] = {          0, y0, pw_half, part_height }; // 40 x 84 = 3360
    clip_rect[ 3] = {    pw_half, y0, pw_half, part_height };
    clip_rect[ 4] = {          0, y1, pw_half, part_height };
    clip_rect[ 5] = {    pw_half, y1, pw_half, part_height };
    clip_rect[ 6] = {2 * pw_half, y0, pw_half, part_height };
    clip_rect[ 7] = {3 * pw_half, y0, pw_half, part_height };
    clip_rect[ 8] = {2 * pw_half, y1, pw_half, part_height };
    clip_rect[ 9] = {3 * pw_half, y1, pw_half, part_height };
    clip_rect[10] = {4 * pw_half, y0, pw_half, part_height };
    clip_rect[11] = {5 * pw_half, y0, pw_half, part_height };
    clip_rect[12] = {4 * pw_half, y1, pw_half, part_height };
    clip_rect[13] = {5 * pw_half, y1, pw_half, part_height };

    // サブボタン部
    clip_rect[14] = {                0, y2, disp_width >> 2, sub_btns_height }; // 60 x 32 = 1920
    clip_rect[16] = { disp_width  >> 2, y2, disp_width >> 2, sub_btns_height };
    clip_rect[19] = { disp_width  >> 1, y2, disp_width >> 2, sub_btns_height };
    clip_rect[21] = { disp_width*3>> 2, y2, disp_width >> 2, sub_btns_height };

    // メインボタン部
    clip_rect[15] = { 0 * w0, y3, w0, main_btns_height }; // 48 x 96 = 4608
    clip_rect[17] = { 1 * w0, y3, w0, main_btns_height };
    clip_rect[18] = { 2 * w0, y3, w0, main_btns_height };
    clip_rect[20] = { 3 * w0, y3, w0, main_btns_height };
    clip_rect[22] = { 4 * w0, y3, w0, main_btns_height };
  }
  void resetInvalidatedRect(void)
  {
    hasInvalidated = false;
    for (auto& r : invalidated_rect) {
      r.clear();
    }
  }
  void addInvalidatedRect(const rect_t &rect)
  {
    if (rect.empty()) return;
    for (int i = 0; i < max_clip_rect; ++i) {
      auto clipped_rect = rect_and(clip_rect[i], rect);
      if (clipped_rect.empty()) continue;
      hasInvalidated = true;
      if (invalidated_rect[i].empty()) {
        invalidated_rect[i] = clipped_rect;
      }
      else
      {
        invalidated_rect[i] = rect_or(invalidated_rect[i], clipped_rect);
      }
    }
  }
};

static draw_param_t _draw_param;

static uint32_t add_color(uint32_t base_color, uint32_t table_color)
{
  uint32_t result = 0;
  for (int i = 0; i < 3; ++i) {
    int_fast16_t c = (base_color & 0xFF) + (int_fast16_t)((int8_t)table_color);
    result |= ( (c > 255) ? 255 : (c < 0) ? 0 : c ) << (i*8);
    base_color >>= 8;
    table_color >>= 8;
  }
  return result;
}

void image_dark_shift(M5Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t shift = 1)
{
  auto canvas_width = canvas->width();
  auto canvas_height = canvas->height();
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w > canvas_width - x) {  w = canvas_width - x; }
  if (h > canvas_height - y) {  h = canvas_height - y; }
  if (w <= 0 || h <= 0) return;

  // RGB565 の各色ビットを指定ビット分シフトするためのマスク
  static constexpr const uint16_t masks[] = {
    0x7BEF, // shift 1
    0x39E7, // shift 2
    0x18E3, // shift 3
    0x0861, // shift 4
    0x0020, // shift 5
  };
  uint16_t mask = masks[shift - 1];

  auto framebuffer = (m5gfx::swap565_t*)(canvas->getBuffer());
  framebuffer += x + canvas_width * y;
  for (int j = 0; j < h; ++j) {
    auto buf = framebuffer;
    framebuffer += canvas_width;
    for (int i = 0; i < w; ++i) {
      auto raw = __builtin_bswap16(buf[0].raw);
      raw = ((raw >> shift) & mask);
      buf[0].raw = __builtin_bswap16(raw);
      ++buf;
    }
  }
}

void image_color_or(M5Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
  auto canvas_width = canvas->width();
  auto canvas_height = canvas->height();
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w > canvas_width - x) {  w = canvas_width - x; }
  if (h > canvas_height - y) {  h = canvas_height - y; }
  if (w <= 0 || h <= 0) return;

  color = __builtin_bswap16(color);
  auto framebuffer = (m5gfx::swap565_t*)(canvas->getBuffer());
  framebuffer += x + canvas_width * y;
  for (int j = 0; j < h; ++j) {
    auto buf = framebuffer;
    framebuffer += canvas_width;
    for (int i = 0; i < w; ++i) {
      buf[0].raw |= color;
      ++buf;
    }
  }
}

struct ui_base_t
{
  void draw(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
            int32_t offset_y, const rect_t *clip_rect);

  void update(draw_param_t *param, int offset_x, int offset_y);

  virtual bool smoothMove(int step)
  {
    return _client_rect.smooth_move(_target_rect, step);
  }

  virtual void setTargetRect(const rect_t &rect)
  {
    _target_rect = rect;
  }

  // 該当座標にコントロールが存在するか判定する
  virtual ui_base_t* searchUI(int32_t x, int32_t y)
  {
    // 移動中のコントロールは処理対象外とする
    if (_client_rect == _target_rect && _target_rect.isContain(x, y)) {
      return this;
    }
    return nullptr;
  }

  void setClientRect(const rect_t &rect)
  {
    _client_rect = rect;
  }

  const rect_t &getTargetRect(void) const
  {
    return _target_rect;
  }

  const rect_t &getClientRect(void) const
  {
    return _client_rect;
  }


  virtual void touchControl(draw_param_t *param, const m5::touch_detail_t& td)
  {
  }

  void setParent(ui_base_t *parent)
  {
    _parent = parent;
  }
protected:
  ui_base_t* _parent = nullptr;
  rect_t _client_rect;
  rect_t _target_rect;
  uint8_t _prev_update_count;

  virtual void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) = 0;

  virtual void update_impl(draw_param_t *param, int offset_x, int offset_y)
  {
    if (_client_rect != _target_rect)
    {
      int px = offset_x - _client_rect.x;
      int py = offset_y - _client_rect.y;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      smoothMove(param->smooth_step);
      param->addInvalidatedRect({px + _client_rect.x, py + _client_rect.y, _client_rect.w, _client_rect.h});
    }
  }
};

void ui_base_t::draw(draw_param_t* param, M5Canvas* canvas, int32_t offset_x, 
      int32_t offset_y, const rect_t* clip_rect) {
  auto rect = getClientRect();
  if (rect.empty()) {
      return;
  }
  offset_y += rect.y;
  rect.y = offset_y;
  offset_x += rect.x;
  rect.x = offset_x;

  int32_t right  = std::min(rect.right(), clip_rect->right());
  int32_t bottom = std::min(rect.bottom(), clip_rect->bottom());
  int32_t x = std::max(rect.x, clip_rect->x);
  int32_t y = std::max(rect.y, clip_rect->y);
  rect.x = x;
  rect.y = y;
  rect.w = right - x;
  rect.h = bottom - y;
  if (rect.empty()) {
      return;
  }
  canvas->setClipRect(x, y, rect.w, rect.h);
  // int32_t dummy;
  // canvas->getClipRect(&dummy, &dummy, &wid, &hei);
  // if (wid > 0 && hei > 0) {
  draw_impl(param, canvas, offset_x, offset_y, &rect);
  // }
#if defined ( DEBUG_GUI )
  canvas->drawRect(rect.x, rect.y, rect.w, rect.h, rand());
#endif
  canvas->clearClipRect();
}

void ui_base_t::update(draw_param_t* param, int offset_x, int offset_y) {
  auto rect = getClientRect();
  offset_y += rect.y;
  offset_x += rect.x;
  update_impl(param, offset_x, offset_y);
}

struct ui_container_t : public ui_base_t {
  void addChild(ui_base_t* ui) {
    ui->setParent(this);
    _ui_child.push_back(ui);
  }
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    for (auto ui : _ui_child) {
      ui->update(param, offset_x, offset_y);
    }
  }

  ui_base_t* searchUI(int32_t x, int32_t y) override
  {
    if (this == ui_base_t::searchUI(x, y)) {
      // _ui_childを逆順に処理する
      for (auto it = _ui_child.rbegin(); it != _ui_child.rend(); ++it) {
        auto child = (*it)->searchUI(x - _client_rect.x, y - _client_rect.y);
        if (child != nullptr) {
          return child;
        }
      }
      return this;
    }
    return nullptr;
  }

  void shrink_to_fit(void) {
    _ui_child.shrink_to_fit();
  }

protected:
  std::vector<ui_base_t*> _ui_child;

  void draw_impl(draw_param_t* param, M5Canvas* canvas, int32_t offset_x, 
      int32_t offset_y, const rect_t* clip_rect) override {
    for (auto ui : _ui_child) {
      ui->draw(param, canvas, offset_x, offset_y, clip_rect);
    }
  }
};

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

struct ui_popup_qr_t : public ui_base_t
{
protected:
  M5Canvas _qr_canvas;
  uint32_t _caption_color;
  def::qrcode_type_t _qr_type = def::qrcode_type_t::QRCODE_NONE;
  const char* _caption = nullptr;
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

          static constexpr int width = 39 * 5;
          static constexpr int height = width + 32;
          _target_rect = { (disp_width - width) >> 1, (disp_height - height) >> 1, width, height };

          uint32_t color = 0xC0C0C0;
          char buf[64] = {0,};
          switch (qr_type) {
          default:
          case def::qrcode_type_t::QRCODE_URL_MANUAL:
            _caption = "Scan for User Manual";
            snprintf(buf, sizeof(buf), "%s", def::app::url_manual);
            break;
          case def::qrcode_type_t::QRCODE_AP_SSID:
            _caption = "Scan for WiFi Setup";
            snprintf(buf, sizeof(buf), "WIFI:S:%s;T:%s;P:%s;;", def::app::wifi_ap_ssid, def::app::wifi_ap_type, def::app::wifi_ap_pass);
            break;
          case def::qrcode_type_t::QRCODE_URL_DEVICE:
            _caption = "Please Scan again";
            snprintf(buf, sizeof(buf), "http://%s.local/", def::app::wifi_mdns);
            color = 0xFFFF00u;
            break;
          }
          _caption_color = color;
          _qr_canvas.qrcode(buf);
        }
      }
    }
    if (updated) {
      _zoom = ((float)_client_rect.w) / _qr_canvas.width();
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    auto cr = getClientRect();
    canvas->fillScreen(_caption_color);
    _qr_canvas.pushRotateZoom(canvas, offset_x + (cr.w >> 1), offset_y + (cr.w >> 1), 0.0f, _zoom, _zoom);
    canvas->drawRect(offset_x + 1, offset_y + 1, cr.w - 2, cr.h - 2, TFT_DARKGRAY);
    if (_caption) {
      canvas->setTextSize(_zoom / 5, _zoom / 2.5f);
      canvas->setTextDatum(m5gfx::datum_t::bottom_center);
      canvas->setTextColor(TFT_BLACK);
      canvas->drawString(_caption, offset_x + (cr.w >> 1), offset_y + cr.h - 2 );
    }
  }
};
ui_popup_qr_t ui_popup_qr;

struct ui_volume_info_t : public ui_base_t
{
protected:
    uint8_t _volume = 0;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto volume = system_registry->user_setting.getMasterVolume();
    if (_volume != volume) {
      _volume = volume;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    int r = 10;
    int x = offset_x + _client_rect.w - 12;
    {
      int y = offset_y + 11;
      int v = _volume * 36 / 10;
      canvas->fillCircle(x, y, r, 0x303030u);
      canvas->drawCircle(x, y, r, TFT_LIGHTGRAY);
      canvas->setColor(TFT_WHITE);
      canvas->fillArc(x, y, r, 0, 90, 90 + v);
    }
  }
};
static ui_volume_info_t ui_volume_info;

struct ui_battery_info_t : public ui_base_t
{
protected:
    uint8_t _battery = 0;
    bool _is_charging = false;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto battery = system_registry->runtime_info.getBatteryLevel();
    auto is_charging = system_registry->runtime_info.getBatteryCharging();

    if (_is_charging != is_charging || _battery != battery) {
      _is_charging = is_charging;
      _battery = battery;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  { // バッテリーアイコン
    int x = offset_x;
    int w = _client_rect.w;

    int y = offset_y + 2;
    int h = _client_rect.h - 4;
    canvas->fillRect(x + (w>>2), y, w - (w>>1), -2, TFT_WHITE);
    canvas->drawRect(x, y, w, h, TFT_WHITE);
    x += 2;
    w -= 4;
    y += 2;
    h -= 4;
    int v = h * _battery / 100;
    canvas->fillRect(x, y + h - v, w, v, _is_charging ? TFT_GREEN : TFT_WHITE);
  }
};
static ui_battery_info_t ui_battery_info;

struct ui_wifi_sta_info_t : public ui_base_t
{
protected:
    def::command::wifi_sta_info_t _wifi_status = (def::command::wifi_sta_info_t)0xFF;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto wifi_status = system_registry->runtime_info.getWiFiSTAInfo();

    if (_wifi_status != wifi_status) {
      _wifi_status = wifi_status;
      int w = (wifi_status == def::command::wifi_sta_info_t::wsi_off)
            ? 0 : 23;
      if (w != _target_rect.w) {
        _target_rect.x -= w - _target_rect.w;
        _target_rect.w = w;
      }
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  { // WiFi STAアイコン
// canvas->fillScreen(rand());
// canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, rand());
    int level = -1;
    switch (_wifi_status) {
    case def::command::wifi_sta_info_t::wsi_off:
      level = 0;
      break;
    case def::command::wifi_sta_info_t::wsi_waiting:
      level = 1;
      break;
    case def::command::wifi_sta_info_t::wsi_signal_1:
      level = 2;
      break;
    case def::command::wifi_sta_info_t::wsi_signal_2:
      level = 3;
      break;
    case def::command::wifi_sta_info_t::wsi_signal_3:
      level = 4;
      break;
    case def::command::wifi_sta_info_t::wsi_signal_4:
      level = 5;
      break;
    default:
      break;
    }

    int x = offset_x;
    int w = _client_rect.w;

    int y = offset_y;
    int h = _client_rect.h;

    int xc = x + (w >> 1);
    int yc = y + h - 5;
    canvas->fillCircle(xc, yc, 2, level > 0 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc,  7,  6, 230, 310, level > 1 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc, 11, 10, 230, 310, level > 2 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc, 15, 14, 230, 310, level > 3 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc, 19, 18, 230, 310, level > 4 ? 0xFFFFFFu : 0x3F3F3Fu);
    if (level < 0) {
      canvas->drawLine(offset_x, offset_y, offset_x+w, offset_y+h, TFT_RED);
      canvas->drawLine(offset_x, offset_y+h, offset_x+w, offset_y, TFT_RED);
    }
/*
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(TFT_WHITE);
    canvas->drawNumber((uint8_t)_wifi_status, x, y);
*/
  }
};
static ui_wifi_sta_info_t ui_wifi_sta_info;

struct ui_wifi_ap_info_t : public ui_base_t
{
protected:
    def::command::wifi_ap_info_t _wifi_status = (def::command::wifi_ap_info_t)0xFF;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto wifi_status = system_registry->runtime_info.getWiFiAPInfo();
    if (_wifi_status != wifi_status) {
      _wifi_status = wifi_status;
      int w = (wifi_status == def::command::wifi_ap_info_t::wai_off)
            ? 0 : 23;
      if (w != _target_rect.w) {
        _target_rect.x -= w - _target_rect.w;
        _target_rect.w = w;
      }
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  { // WiFi APアイコン
    int level = -1;
    switch (_wifi_status) {
    case def::command::wifi_ap_info_t::wai_off:
      level = 0;
      break;
    case def::command::wifi_ap_info_t::wai_waiting:
      level = 1;
      break;
    case def::command::wifi_ap_info_t::wai_enabled:
      level = 4;
      break;
    default:
      break;
    }

    int x = offset_x;
    int w = _client_rect.w;

    int y = offset_y;
    int h = _client_rect.h;

    int xc = x + (w >> 1);
    int yc = y + (h >> 1) - 2;
    canvas->fillCircle(xc, yc, 2, level > 0 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillRoundRect(xc - 2, yc + 4, 5, 8, 2, level > 0 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc,  5,  6, 150, 390, level > 1 ? 0xFFFFFFu : 0x3F3F3Fu);
    canvas->fillArc(xc, yc,  9, 10, 130, 410, level > 2 ? 0xFFFFFFu : 0x3F3F3Fu);
    if (level < 0) {
      canvas->drawLine(offset_x, offset_y, offset_x+w, offset_y+h, TFT_RED);
      canvas->drawLine(offset_x, offset_y+h, offset_x+w, offset_y, TFT_RED);
    }
/*
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(TFT_WHITE);
    canvas->drawNumber((uint8_t)_wifi_status, x, y);
*/
  }
};
static ui_wifi_ap_info_t ui_wifi_ap_info;

struct ui_midiport_info_t : public ui_base_t
{
protected:
    static constexpr uint32_t images[3][5] = {
   // { 0b11111110000000001111111100000000
      { __builtin_bswap32(0b11110000000000000000011111000000)
      , __builtin_bswap32(0b11010000000000001000110001100000)
      , __builtin_bswap32(0b11110111101111011110110000000000)
      , __builtin_bswap32(0b10000100101000001000110001100000)
      , __builtin_bswap32(0b10000111101000001110011111000000)
      },
      { __builtin_bswap32(0b01111110001100000001111111000000)
      , __builtin_bswap32(0b01100011001100000001100000000000)
      , __builtin_bswap32(0b01111110001100000001111110000000)
      , __builtin_bswap32(0b01100011001100000001100000000000)
      , __builtin_bswap32(0b01111110001111111001111111000000)
      },
      { __builtin_bswap32(0b01100011000111111001111110000000)
      , __builtin_bswap32(0b01100011001100000001100011000000)
      , __builtin_bswap32(0b01100011000111110001111110000000)
      , __builtin_bswap32(0b01100011000000011001100011000000)
      , __builtin_bswap32(0b00111110001111110001111110000000)
      },
    };
    uint32_t _color[3] = { 0, 0, 0 };
    uint8_t _tx_couunt[3] = { 0, 0, 0 };
    uint8_t _rx_couunt[3] = { 0, 0, 0 };
    uint16_t _tx_dots[3];
    uint16_t _rx_dots[3];
    bool _visible = false;

public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    bool updated = false;

    def::command::midiport_info_t info[3];
    info[0] = system_registry->runtime_info.getMidiPortStatePC();
    info[1] = system_registry->runtime_info.getMidiPortStateBLE();
    info[2] = system_registry->runtime_info.getMidiPortStateUSB();

    bool visible = false;
    for (int i = 0; i < 3; ++i) {
      uint32_t color = 0x303030u;
      if (info[i] != def::command::midiport_info_t::mp_off) {
        visible = true;
        switch (info[i]) {
        case def::command::midiport_info_t::mp_enabled:
          color = 0x606060u; break;
        case def::command::midiport_info_t::mp_connected:
          color = 0xFFFFFFu; break;
        case def::command::midiport_info_t::mp_connecting:
          color = ((param->current_msec >> 8)&1) ? 0xFFFFFFu : 0x606060u; break;
        default: break;
        }
      }
      if (_color[i] != color) {
        _color[i] = color;
        updated = true;
      }
    }
    if (_visible != visible) {
      _visible = visible;
      int w = (visible)
            ? 26 : 0;
      if (w != _target_rect.w) {
        _target_rect.x -= w - _target_rect.w;
        _target_rect.w = w;
      }
    }
    uint8_t tx_couunt[3] = {
      system_registry->runtime_info.getMidiTxCountPC(),
      system_registry->runtime_info.getMidiTxCountBLE(),
      system_registry->runtime_info.getMidiTxCountUSB(),
    };
    uint8_t rx_couunt[3] = {
      system_registry->runtime_info.getMidiRxCountPC(),
      system_registry->runtime_info.getMidiRxCountBLE(),
      system_registry->runtime_info.getMidiRxCountUSB(),
    };

    // 通信カウンタのドット移動増分を求める
    uint8_t shift = (param->current_msec >> 4) - (param->prev_msec >> 4);
    if (shift) {
      for (int i = 0; i < 3; ++i) {
        auto tx = _tx_dots[i];
        if (tx) {
          _tx_dots[i] = tx << shift;
          updated = true;
        }
        auto rx = _rx_dots[i];
        if (rx) {
          _rx_dots[i] = rx << shift;
          updated = true;
        }
      }
    }
    for (int i = 0; i < 3; ++i) {
      if (_tx_couunt[i] != tx_couunt[i]) {
        _tx_couunt[i] = tx_couunt[i];
        _tx_dots[i] |= 1;
        updated = true;
      }
      if (_rx_couunt[i] != rx_couunt[i]) {
        _rx_couunt[i] = rx_couunt[i];
        _rx_dots[i] |= 1;
        updated = true;
      }
    }
    if (updated) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    int x = offset_x;
    int w = _client_rect.w;
    for (int i = 0; i < 3; ++i) {
      int y = offset_y + (i * 8);
      canvas->drawBitmap(x, y+2, (const uint8_t*)images[i], 32, 5, _color[i]);
      canvas->setColor(0xFFFF00u);
      for (int j = 0; j < 13; ++j) {
        if (_tx_dots[i] & (1 << j)) {
          canvas->writePixel(x +     (12 - j), y);
          canvas->writePixel(x +w-1- (12 - j), y);
        }
        if (_rx_dots[i] & (1 << j)) {
          canvas->writePixel(x +     j, y + 1);
          canvas->writePixel(x +w-1- j, y + 1);
        }
      }
    }
  }
};
static ui_midiport_info_t ui_midiport_info;

struct ui_icon_auto_play_t : public ui_base_t
{
protected:
  def::play::auto_play_state_t _autoplay_style;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto mode = system_registry->runtime_info.getGuiAutoplayState();
    if (_autoplay_style != mode) {
      _autoplay_style = mode;
      if (mode == def::play::auto_play_state_t::auto_play_none) {
        _target_rect.w = 0;
      } else {
        _target_rect.w = 11;
      }
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  { // オートプレイアイコン
    int x = offset_x;
    int w = _client_rect.w;

    int y = offset_y;
    int h = _client_rect.h;

    x = x + (w >> 1);
    y = y + (h >> 1);
    switch (_autoplay_style) {
    default:
      canvas->fillTriangle(x - 5, y - 5, x - 5, y + 5, x + 5, y, TFT_GREEN);
      break;
    case def::play::auto_play_state_t::auto_play_none:
    case def::play::auto_play_state_t::auto_play_waiting:
      canvas->fillRect(x - 5, y - 5, 11, 11, TFT_DARKGRAY);
      break;
    case def::play::auto_play_state_t::auto_play_paused:
      canvas->fillRect(x - 5, y - 5,  3, 11, TFT_DARKGRAY);
      canvas->fillRect(x + 5, y - 5, -3, 11, TFT_DARKGRAY);
      break;
    case def::play::auto_play_state_t::auto_play_beatmode:
      canvas->drawFastHLine(x - 4, y,  2, TFT_YELLOW);
      canvas->drawFastHLine(x + 3, y,  2, TFT_YELLOW);
      canvas->drawLine(x - 3, y    , x - 1, y - 6, TFT_YELLOW);
      canvas->drawLine(x - 1, y - 5, x + 1, y + 6, TFT_YELLOW);
      canvas->drawLine(x + 3, y    , x + 1, y + 6, TFT_YELLOW);
      break;
    }
  }
};
static ui_icon_auto_play_t ui_icon_auto_play;

struct ui_song_modified_t : public ui_base_t
{
protected:
  bool _modified = false;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto modified = system_registry->runtime_info.getSongModified();
    if (_modified != modified) {
      _modified = modified;
      if (!modified) {
        _target_rect.x += _target_rect.w;
        _target_rect.w = 0;
      } else {
        _target_rect.w = 9;
        _target_rect.x -= _target_rect.w;
      }
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  { // 未保存の変更ありアイコン
    int x = offset_x;
    int w = _client_rect.w;

    int y = offset_y;
    int h = _client_rect.h;

    int xc = x + (w >> 1);
    int yc = y + (h >> 1) + 4;

    canvas->drawLine(xc - 3, yc - 3, xc + 3, yc + 3, TFT_YELLOW);
    canvas->drawLine(xc - 3, yc + 3, xc + 3, yc - 3, TFT_YELLOW);
    canvas->drawFastHLine(xc - 4, yc,  9, TFT_YELLOW);
    canvas->drawFastVLine(xc, yc - 5, 11, TFT_YELLOW);
  }
};
static ui_song_modified_t ui_song_modified;

struct ui_playkey_info_t : public ui_base_t
{
protected:
    uint8_t _master_key = 0;
    int8_t _slot_key = 0;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto master_key = system_registry->runtime_info.getMasterKey();
    int slot_key = system_registry->current_slot->slot_info.getKeyOffset();
    if (_master_key != master_key || _slot_key != slot_key) {
      _master_key = master_key;
      _slot_key = slot_key;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    int x = offset_x + (_client_rect.w >> 1);
    int y = offset_y - (_slot_key ? 3 : 6);
    canvas->setTextSize(1, _slot_key ? 1 : 2);
    canvas->setTextDatum(m5gfx::datum_t::top_center);
    canvas->setTextColor(0xFFFFFFu);
    auto text = def::app::key_name_table[_master_key];
    canvas->drawString(text, x, y);
    if (_slot_key) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%+d", _slot_key);
      canvas->drawString(buf, x, y + (_client_rect.h >> 1));
    }
  }
};
static ui_playkey_info_t ui_playkey_info;

struct ui_playkey_select_t : public ui_timer_popup_t
{
protected:
    int _current_x256 = 0;
    int _target_x256 = 0;
    uint8_t _master_key = 0;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool updated = false;

    if (system_registry->runtime_info.getCurrentMode() == def::playmode::menu_mode) {
      close();
    } else {
      auto master_key = system_registry->runtime_info.getMasterKey();
      if (_master_key != master_key) {
        updated = true;
        _master_key = master_key;
        _target_x256 = master_key * 256;
      }
      if (_current_x256 != _target_x256) {
        updated = true;
        int diff = abs(_current_x256 - _target_x256) >> 8;
        if (diff > (def::app::max_play_key >> 1)) {
          if (_current_x256 < _target_x256) {
            _current_x256 += def::app::max_play_key << 8;
          } else {
            _current_x256 -= def::app::max_play_key << 8;
          }
        }
        _current_x256 = smooth_move(_target_x256, _current_x256, param->smooth_step);
      }
      procTime(updated ? 1000 : -param->smooth_step);
    }

    ui_timer_popup_t::update_impl(param, offset_x, offset_y);
    if (updated) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (_client_rect.h > (header_height * 7 >> 2))
    {
      int r = 79;
// M5_LOGV("x:%d y:%d cx:%d cy:%d", offset_x, offset_y, _client_rect.x, _client_rect.y);
      int x = offset_x + _client_rect.w - r - 1;
      int y = offset_y + (_client_rect.h >> 1);
      canvas->fillArc(x, y, r, r-54, 280, 80, 0x101020u);
      canvas->setTextDatum(m5gfx::datum_t::middle_center);
      canvas->setTextSize(1, 2);
      int i1 = (_current_x256 - 384) >> 8;
      int i2 = (_current_x256 + 640) >> 8;
      for (int i = i1; i <= i2; ++i) {
        int key = i;
        if (key < 0) {
          key += def::app::max_play_key;
        } else if (key >= def::app::max_play_key) {
          key -= def::app::max_play_key;
        }
        auto text = def::app::key_name_table[key];
        float angle = (((key + 3) << 8) - _current_x256) * M_PI / (256 * 6);
        int x1 = x + (int)((r * 3 / 4.0f) * sinf(angle)) - 6;
        int y1 = y + (int)((r * 3 / 4.0f) * cosf(angle)) - 2;
        canvas->setTextColor(i == _master_key ? 0xFFFF00u : 0xBBBBBBu);
        canvas->drawString(text, x1, y1);
      }
    } else {
      canvas->setTextDatum(m5gfx::datum_t::top_left);
      canvas->setTextSize(1, 2);
      canvas->setTextColor(0xFFFF00u);
      auto text = def::app::key_name_table[_master_key];
      int x = offset_x;
      int y = offset_y - 6;
      canvas->drawString(text, x, y);
    }
  }
};
ui_playkey_select_t ui_playkey_select;

struct ui_main_buttons_t : public ui_base_t
{
  protected:
    const char* _text_upper[def::hw::max_main_button] = { 0, };
    const char* _text_lower[def::hw::max_main_button] = { 0, };
    char _text[def::hw::max_main_button][8] = { { 0 }, };
    uint32_t _btns_color[def::hw::max_main_button] = { 0, };
    uint32_t _text_color[def::hw::max_main_button] = { 0, };
    uint8_t _text_width[def::hw::max_main_button] = { 0, };
    uint32_t _btn_bitmask = 0x00;
    uint8_t _play_mode = 0x80;
    uint8_t _master_key = 0x80;
    uint8_t _minor_swap = 0x80;
    int8_t _semitone = 0x80;
    uint8_t _modifier = 0x80;
    int8_t _offset_key = 0x80;
    uint8_t _note_scale = 0x80;

    registry_t::history_code_t _mapping_history_code;
    uint32_t _working_command_change_count;
  public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    bool flg_update = false;
    uint8_t play_mode = system_registry->runtime_info.getCurrentMode();
    uint8_t master_key = system_registry->runtime_info.getMasterKey();
    uint8_t minor_swap = system_registry->chord_play.getChordMinorSwap();
    int8_t semitone = system_registry->chord_play.getChordSemitoneShift();
    uint8_t modifier = system_registry->chord_play.getChordModifier();
    int8_t offset_key = system_registry->current_slot->slot_info.getKeyOffset();

    uint8_t note_scale = system_registry->current_slot->slot_info.getNoteScale();
    auto mapping_history_code = system_registry->command_mapping_current.getHistoryCode();
    auto working_command_change_count = system_registry->working_command.getChangeCounter();
    if (_play_mode != play_mode
     || _master_key != master_key
     || _minor_swap != minor_swap
     || _semitone != semitone
     || _modifier != modifier
     || _offset_key != offset_key
     || _note_scale != note_scale
     || _mapping_history_code != mapping_history_code
     ) {
      _play_mode = play_mode;
      _master_key = master_key;
      _minor_swap = minor_swap;
      _semitone = semitone;
      _modifier = modifier;
      _offset_key = offset_key;
      _note_scale = note_scale;
      _mapping_history_code = mapping_history_code;
      flg_update = true;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    uint32_t prev_bitmask = _btn_bitmask;
    _btn_bitmask = 0x7FFF & system_registry->internal_input.getButtonBitmask();
    uint32_t xor_bitmask = prev_bitmask ^ _btn_bitmask;
    if (xor_bitmask) {
      flg_update = true;
    }
    _gfx->setTextSize(1, 2);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      uint32_t color = system_registry->rgbled_control.getColor(i);
      if (_btns_color[i] != color || xor_bitmask & (1 << i)) {
        _btns_color[i] = color;
        param->addInvalidatedRect(getButtonRect(i, offset_x, offset_y));
      }
    }
    if (flg_update || _working_command_change_count != working_command_change_count) {
      int slot_key = master_key + offset_key;
      while (slot_key < 0) { slot_key += 12; }
      while (slot_key >= 12) { slot_key -= 12; }

      _working_command_change_count = working_command_change_count;
      for (int i = 0; i < def::hw::max_main_button; ++i) {
        _text[i][0] = 0;
        _text_upper[i] = "";
        _text_lower[i] = "";

        auto cp_pair = system_registry->command_mapping_current.getCommandParamArray(i);
        def::command::command_param_t command_param;
        int pindex = 0;
        bool hit = true;
        for (int j = 0; cp_pair.array[j].command != def::command::none; ++j) {
          pindex = j;
          command_param = cp_pair.array[j];
          hit &= system_registry->working_command.check(command_param);
        }
        auto command = command_param.command;
        uint32_t text_color = system_registry->color_setting.getButtonPressedTextColor();
        if ((_btn_bitmask & (1 << i)) == 0) {
          if (hit) {
            text_color = system_registry->color_setting.getButtonWorkingTextColor();
          } else {
            text_color = system_registry->color_setting.getButtonDefaultTextColor();
          }
        }
        _text_color[i] = text_color;

        const char* name = nullptr;
        if (command < sizeof(def::command::command_name_table) / sizeof(def::command::command_name_table[0]))
        {
          auto table = def::command::command_name_table[command];
          if (table != nullptr) {
            name = table[command_param.param];
          }
        }

        for (auto data: def::command::button_text_table) {
          if (data.command == cp_pair) {
            name = data.text;
            _text_lower[i] = data.lower;
            _text_upper[i] = data.upper;
            break;
          }
        }
        // 単発コマンドの場合の表示
        if (pindex == 0) {
          switch (command) {
          default:
            break;

          case def::command::drum_button:
            { // ドラムモードボタンの場合の表示名処理
              uint8_t drum_index = command_param.param - 1;
              uint8_t note = system_registry->drum_mapping.get8(drum_index);
              name = def::midi::drum_name_tbl[note];
            }
            break;

          case def::command::note_button:
            { // ノートモードボタンの場合の表示名処理
              uint8_t note_button_index = command_param.param - 1;
              int32_t note = def::play::note::note_scale_note_table[note_scale][note_button_index];
              note += slot_key;
              if (note >= 0 && note < 128) {
                int name_index = note % def::app::max_play_key;
                auto note_name = def::app::note_name_table[name_index];
                if (note_name != nullptr) {
                  name = def::app::note_name_table[name_index];
                }
              }
            }
            break;

          case def::command::chord_modifier:
            {
  #if defined ( KANTAN_USE_MODIFIER_6_AS_7M7 )
              // 7とM7の同時押し時に6に表示を変換する
              auto table = def::command::command_name_table[command];
              if (command_param.param == KANTANMusic_Modifier_7) {
                if (system_registry->working_command.check( { def::command::chord_modifier, KANTANMusic_Modifier_M7 } )) {
                  name = table[KANTANMusic_Modifier_6];
                }
              }
              if (command_param.param == KANTANMusic_Modifier_M7) {
                if (system_registry->working_command.check( { def::command::chord_modifier, KANTANMusic_Modifier_7 } )) {
                  name = table[KANTANMusic_Modifier_6];
                }
              }
  #endif
            }
            break;

          case def::command::chord_degree:
            {
              auto degree_param = (degree_param_t)command_param.param;
              int degree = degree_param.getDegree();
              if (degree_param.raw == degree) {
                // 各キーに対して画面の表示をフラットに統一するかシャープに統一するかの分岐テーブル (0=flat / 1=sharp)
                static constexpr const uint8_t note_flat_sharp_tbl[12] =
                { 0, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, };
  
                // 各演奏ボタンに対してマイナー符号を付与するか分岐 (0=メジャー / 1=マイナー)
                static constexpr const uint8_t button_minor_tbl[7] =
                { 0, 1, 1, 0, 0, 1, 1, };

                KANTANMusic_GetMidiNoteNumberOptions options;
                KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
                options.minor_swap = _minor_swap | degree_param.getMinorSwap();
                int ss = _semitone + degree_param.getSemitoneShift();
                options.semitone_shift = ss < 0 ? -1 : (ss == 0 ? 0 : 1);
                uint8_t note = KANTANMusic_GetMidiNoteNumber
                  ( 1
                  , degree
                  , slot_key
                  , &options
                  );
                note %= 12;
                bool is_minor = false;
                // 以下の3つの修飾子の場合はメジャー・マイナーの概念がないのでマイナー表示をしない
                if (_modifier == KANTANMusic_Modifier_dim
                || _modifier == KANTANMusic_Modifier_sus4
                || _modifier == KANTANMusic_Modifier_aug) {
                  is_minor = false;
                }
                else
                if (_semitone == 0) {
                  is_minor = button_minor_tbl[degree - 1];
                  if (_minor_swap) { is_minor = !is_minor; }
                }
  
                auto notename = def::app::note_name_table[note];
                /// 12個の音階に対して半音部分を♭に置き換えるか判定処理
                if (notename[1] != 0x00) {
                  bool sharp = note_flat_sharp_tbl[_master_key];
                  notename = def::app::note_name_table[note + (sharp ? -1 : 1)];
                  _text_upper[i] = sharp ? "♯" : "♭";
                }
                _text_lower[i] = (is_minor) ? "m" : " ";
                snprintf(_text[i], sizeof(_text[i]), "%d %s", degree, notename);
                name = nullptr;
              }
            }
            break;
          }

        }
        if (name != nullptr) {
          snprintf(_text[i], sizeof(_text[i]), "%s", name);
        }

        int tw = _gfx->textWidth(_text[i]);
        int lower_width = 0;
        int upper_width = 0;
        if (_text_lower[i] != nullptr) {
          lower_width = _gfx->textWidth(_text_lower[i]);
        }
        if (_text_upper[i] != nullptr) {
          upper_width = _gfx->textWidth(_text_upper[i]);
        }
        if (lower_width < upper_width) {
          lower_width = upper_width;
        }
        _text_width[i] = tw + lower_width;
      }
    }
  }
  rect_t getButtonRect(uint8_t index, int offset_x, int offset_y) {
    int x = index % 5;
    int xs = (x) * _client_rect.w / 5 + 1;
    int xe = (x + 1) * _client_rect.w / 5 - 1;
    int y = 2 - (index / 5);
    int ys = (y) * _client_rect.h / 3 + 1;
    int ye = (y + 1) * _client_rect.h / 3 - 1;
    return { xs + offset_x, ys + offset_y, xe - xs, ye - ys };
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    canvas->setTextDatum(m5gfx::datum_t::middle_left);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto rect = getButtonRect(i, offset_x, offset_y);
      int xs = rect.x;
      int ys = rect.y;
      if (!clip_rect->isIntersect(rect)) { continue; }

      draw_button(canvas, xs, ys, rect.w, rect.h, _btns_color[i]);
      int y = ys + (rect.h >> 1) - 1;

      canvas->setTextColor(_text_color[i]);

      canvas->setTextSize(1, 2);
      int x = xs + ((rect.w - (int)_text_width[i]) >> 1);
      x += canvas->drawString(_text[i], x, y);

      canvas->setTextSize(1, 1);
      if (_text_upper[i] != nullptr) {
        canvas->drawString(_text_upper[i], x, y - 4);
      }
      if (_text_lower[i] != nullptr) {
        canvas->drawString(_text_lower[i], x, y + 6);
      }
    }
#if 0
    canvas->setTextDatum(m5gfx::datum_t::middle_center);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto rect = getButtonRect(i, offset_x, offset_y);
      int xs = rect.x;
      int ys = rect.y;
      if (!clip_rect->isIntersect(rect)) { continue; }

      draw_button(canvas, xs, ys, rect.w, rect.h, _btns_color[i]);
      int y = ys + (rect.h >> 1) - 1;
// canvas->setTextColor(app_core.getBtnPressed(i) ? 0xFFFFFFu : 0xAAAAAAu);
      // bool isPressed = _btn_bitmask & (1 << i);
      // canvas->setTextColor(isPressed ? 0xFFFFFFu : 0xBBBBBBu);
      canvas->setTextColor(_text_color[i]);

      canvas->setTextSize(1, 1);
      if (_text_upper[i] != nullptr) {
        canvas->drawString(_text_upper[i], xs + 36, y - 4);
      }
      if (_text_lower[i] != nullptr) {
        canvas->drawString(_text_lower[i], xs + 36, y + 6);
      }
      canvas->setTextSize(1, 2);
      // canvas->drawString(_text[i], xs + 23, ys);
      canvas->drawString(_text[i], xs + 23, y);
    }
#endif
  }

  void draw_button(M5Canvas *canvas, int x, int y, int w, int h, uint32_t color)
  {  // ボタンのグラデーション表現用カラーテーブル
    static constexpr const uint32_t button_color_tbl[] = {
      0x383644, 0x3B3A3C, 0x212121, 0x080808,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      // 0x000000,
      0x000000, 0x000000, 0x000000,
      0x010101, 0x020202, 0x030303, 0x060606, 0x090909, 0x0D0D0D, 0x131313,
      0x1A1A1A, 0x212121, 0x272727, 0x292929, 0x141414, 0xEAEAEA, 0xCECECE,
    };
    static constexpr const uint32_t button_color_tbl_size = sizeof(button_color_tbl) / sizeof(button_color_tbl[0]);

    canvas->drawFastVLine(x+w-1, y, h, color);

    canvas->drawFastVLine(x, y, h, add_color(color, button_color_tbl[0]));
    if (h > button_color_tbl_size) { h = button_color_tbl_size; }
    for (int i = 0; i < h; ++i) {
      uint32_t c = button_color_tbl[i];
      c = (c == 0) ? color : add_color(color, c);
      canvas->writeFastHLine(x+1, y+i, w-2, c);
    }
  };
  void touchControl(draw_param_t *param, const m5::touch_detail_t& td) override
  {
/* 画面タッチでもボタン操作できるようにしようと思ったけど物理ボタンとの OR を取る必要があるのでいったん保留
    if (td->wasPressed() || td->wasReleased())
    {
      int x =   ((td->x - _client_rect.x) * 5 / _client_rect.w);
      int y = 2-((td->y - _client_rect.y) * 3 / _client_rect.h);
      app_core.setBtnRaw(x+y*5, param->frame_msec, td->isPressed());
    }
//*/
  }
};
static ui_main_buttons_t ui_main_buttons;

struct ui_sub_buttons_t : public ui_base_t
{
    static constexpr const size_t max_button = def::hw::max_sub_button * 2;
    static constexpr const size_t columns = 4;
    static constexpr const size_t rows = max_button / columns;
  protected:
    char _text[max_button][8] = { { 0 }, };
    uint32_t _btns_color[max_button] = { 0, };
    uint32_t _text_color[max_button] = { 0, };
    uint32_t _btn_bitmask = 0x00;
    registry_t::history_code_t _sub_button_history_code;
    // uint32_t _working_command_change_count;
  public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    bool flg_update = false;
    auto history_code = system_registry->sub_button.getHistoryCode();
    if (_sub_button_history_code != history_code
     ) {
      _sub_button_history_code = history_code;
      flg_update = true;
    }
    // uint32_t working_command_counter = system_registry->working_command.getChangeCounter();
    // if (_working_command_change_count != working_command_counter) {
    //   _working_command_change_count = working_command_counter;
    //   flg_update = true;
    // }

    uint32_t prev_bitmask = _btn_bitmask;
    _btn_bitmask = system_registry->internal_input.getButtonBitmask() >> def::hw::max_main_button;
    uint32_t xor_bitmask = prev_bitmask ^ _btn_bitmask;
    if (xor_bitmask) {
      flg_update = true;
    }
    for (int i = 0; i < max_button; ++i) {
      // uint32_t color = system_registry->rgbled_control.getColor(i + def::hw::max_main_button);
      uint32_t color = system_registry->sub_button.getSubButtonColor(i);
      if (_btns_color[i] != color || xor_bitmask & (1 << i)) {
        _btns_color[i] = color;
        param->addInvalidatedRect(getButtonRect(i, offset_x, offset_y));
      }
    }
    if (flg_update) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      for (int i = 0; i < max_button; ++i) {
        _text_color[i] = system_registry->color_setting.getButtonPressedTextColor();
        _text[i][0] = 0;

        auto pair = system_registry->sub_button.getCommandParamArray(i);
        auto command_param = pair.array[0];

        bool sub_button_swap = system_registry->runtime_info.getSubButtonSwap();
        bool is_pressed = _btn_bitmask & (1 << (i % def::hw::max_sub_button));
        if (!is_pressed) {
          uint32_t color = system_registry->color_setting.getButtonDefaultTextColor();

          if (sub_button_swap == (bool)(i < def::hw::max_sub_button)) {
            color = (color >> 1) & 0x7F7F7F;
          }

          if (system_registry->working_command.check(command_param)) {
            color = system_registry->color_setting.getButtonWorkingTextColor();
          }
          _text_color[i] = color;
        }

        const auto command = command_param.getCommand();
        const auto param = command_param.getParam();
/*
        switch (command) {
        case def::command::command_t::sub_button:
          {
            uint8_t sub_button_index = param - 1;
            command_raw = (def::command::command_t)system_registry->sub_button.getCommandParamArray(sub_button_index);
            command = def::command::trimFlag(command_raw);
            param = def::command::getParam(command_raw);
            break;
          }
        }
//*/
        const char* name = nullptr;
        if (command < sizeof(def::command::command_name_table) / sizeof(def::command::command_name_table[0]))
        {
          auto table = def::command::command_name_table[command];
          if (table != nullptr) {
            name = table[param];
          }
        }

        if (name != nullptr)
        {
          snprintf(_text[i], sizeof(_text[i]), "%s", name);
        }
      }
    }
  }
  rect_t getButtonRect(uint8_t index, int offset_x, int offset_y) {
    int x = index % columns;
    int xs = (x) * _client_rect.w / columns + 1;
    int xe = (x + 1) * _client_rect.w / columns - 1;
    int y = index / columns;
    int ys = (y) * _client_rect.h / rows + 1;
    int ye = (y + 1) * _client_rect.h / rows - 1;
    return { xs + offset_x, ys + offset_y, xe - xs, ye - ys };
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
    canvas->setTextSize(1, 1);
    for (int i = 0; i < max_button; ++i) {
      auto rect = getButtonRect(i, offset_x, offset_y);
      int xs = rect.x;
      int ys = rect.y;
      if (!clip_rect->isIntersect(rect)) { continue; }
      draw_button(canvas, xs, ys, rect.w, rect.h, _btns_color[i]);
      canvas->setTextColor(_text_color[i]);
      canvas->drawString(_text[i], xs + (rect.w >> 1), ys + (rect.h >> 1));
    }
  }

  void draw_button(M5Canvas *canvas, int x, int y, int w, int h, uint32_t color)
  {  // ボタンのグラデーション表現用カラーテーブル
    static constexpr const uint32_t button_color_tbl[] = {
      0x383644,
      // 0x3B3A3C,
      0x212121,
      // 0x080808,
      // 0x000000, 0x000000,
      0x010101, 0x020202, 0x030303, 0x060606, 0x090909,
      0x0D0D0D, 0x131313, 0x1A1A1A, 0x212121, 0x272727, 0x292929, 0x141414,0xEAEAEA,
      // 0xCECECE,
    };
    static constexpr const uint32_t button_color_tbl_size = sizeof(button_color_tbl) / sizeof(button_color_tbl[0]);

    canvas->drawFastVLine(x+w-1, y, h, color);

    canvas->drawFastVLine(x, y, h, add_color(color, button_color_tbl[0]));
    if (h > button_color_tbl_size) { h = button_color_tbl_size; }
    for (int i = 0; i < h; ++i) {
      uint32_t c = button_color_tbl[i];
      c = (c == 0) ? color : add_color(color, c);
      canvas->writeFastHLine(x+1, y+i, w-2, c);
    }
  };
  void touchControl(draw_param_t *param, const m5::touch_detail_t& td) override
  {
  }
};
static ui_sub_buttons_t ui_sub_buttons;

struct ui_partinfo_t : public ui_base_t
{
protected:
  uint32_t _backcolor;
  uint32_t _color_hit;
  int16_t _effect_remain = 0;
  registry_t::history_code_t _partinfo_history_code;
  registry_t::history_code_t _arpeggio_history_code;
  uint8_t _slot_index;
  uint8_t _hit_effect_index;
  bool _isEnabled;
  bool _isEmpty;   // ベロシティパターンが空欄かどうか
  bool _isDetailMode;
  bool _need_draw = true;

  // ハイライトに追従するフラグ
  bool _is_follow_highlight = true;

public:
  static constexpr const uint8_t icon_width = 64;
  static constexpr const uint8_t icon_height = 64;

  void update_inner(draw_param_t *param, int offset_x, int offset_y) {
    auto part = &system_registry->current_slot->chord_part[_part_index];
    auto partinfo = &part->part_info;
    // auto partinfo = &param->multipart->channel_info_list[part_index];

    // partinfoに変更があれば再描画対象とする
    auto partinfo_history_code = partinfo->getHistoryCode();
    auto arpeggio_history_code = part->arpeggio.getHistoryCode();
    auto slot_index = system_registry->runtime_info.getPlaySlot();
    bool flg_update = (_partinfo_history_code != partinfo_history_code
                    || _arpeggio_history_code != arpeggio_history_code
                   || _slot_index != slot_index);
    if (flg_update) {
      _partinfo_history_code = partinfo_history_code;
      _arpeggio_history_code = arpeggio_history_code;
      _slot_index = slot_index;
      _isEmpty = system_registry->current_slot->chord_part[_part_index].arpeggio.isEmpty();
    }

    int highlight_target = system_registry->chord_play.getPartStep(_part_index);
    if (highlight_target < 0) { highlight_target = 0; }
    highlight_target <<= 8;
    if (x_highlight_256 != highlight_target) {
      if (_isDetailMode) {
        auto before_rect = getHighlightRect(offset_x, offset_y);
        x_highlight_256 = smooth_move(highlight_target, x_highlight_256, param->smooth_step);
        auto after_rect = getHighlightRect(offset_x, offset_y);
        param->addInvalidatedRect(rect_or(before_rect, after_rect));
      } else {
        x_highlight_256 = highlight_target;
      }
      if (_is_follow_highlight) {
        int x256 = highlight_target < 0 ? 0 : highlight_target;
        if (x_scroll_target > x256) {
          changePage(getCurrentPage() - 1);
        } else
        if (x_scroll_target < x256 - (256 * 7))
        {
          changePage(getCurrentPage() + 1);
        }
      }
    }
    if (x_scroll_current != x_scroll_target) {
      if (_isDetailMode) {
        x_scroll_current = smooth_move(x_scroll_target, x_scroll_current, param->smooth_step);
        flg_update = true;
      } else {
        x_scroll_current = x_scroll_target;
      }
    }
    bool isEnabled = partinfo->getEnabled();
    if (_isEnabled != isEnabled) {
      _isEnabled = isEnabled;
      flg_update = true;
    }
    _backcolor = (isEnabled)
                ? system_registry->color_setting.getEnablePartColor()
                : system_registry->color_setting.getDisablePartColor();
    auto color = system_registry->color_setting.getArpeggioNoteForeColor();
    // auto color = param->color_set->arpeggio_note_fore;
    if (!isEnabled) { color = (color >> 1) & 0x7F7F7Fu; }

    auto effect_remain = _effect_remain;
    if (_isDetailMode) {
      effect_remain = 0;
    } else {
      auto hit_effect_index = system_registry->runtime_info.getPartEffect(_part_index);
      if (_hit_effect_index != hit_effect_index) {
        _hit_effect_index = hit_effect_index;
        if (effect_remain == 0) {
          flg_update = true;
        }
        effect_remain = 127;
      }
      if (effect_remain) {
        effect_remain -= param->smooth_step;
        if (effect_remain <= 0) {
          effect_remain = 0;
          flg_update = true;
        }
      }
      if (flg_update) {
        color = add_color(color, (effect_remain * 0x010100u) | (uint8_t)(-effect_remain));
      }
    }
    _effect_remain = effect_remain;
    _color_hit = color;

    if (flg_update) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    _isDetailMode = system_registry->user_setting.getGuiDetailMode();
    if (system_registry->runtime_info.getPlayMode() != def::playmode::chord_edit_mode) {
      ui_base_t::update_impl(param, offset_x, offset_y);
      update_inner(param, offset_x, offset_y);
    }
  }

  void fill_background(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect)
  {
    auto partinfo = &system_registry->current_slot->chord_part[_part_index].part_info;
    // auto partinfo = &param->multipart->channel_info_list[part_index];
    bool isEnabled = _isEnabled;
    canvas->setColor(_backcolor);
    canvas->fillRect(offset_x+1, offset_y+1, _client_rect.w-2, _client_rect.h-2);
    canvas->setColor(isEnabled ? 0x88AACCu : 0x224466u);
    canvas->writeFastHLine(offset_x, offset_y  , _client_rect.w-1);
    canvas->writeFastVLine(offset_x, offset_y+1, _client_rect.h-2);

    canvas->setColor(0x000033u);
    canvas->writeFastHLine(offset_x, offset_y+_client_rect.h-1, _client_rect.w-1);
    canvas->writeFastVLine(offset_x+_client_rect.w-1, offset_y+1, _client_rect.h-2);
    {/// 楽器名の表示
      auto name = def::midi::program_name_table.at(partinfo->getTone())->get();
      // auto name = draw_param_t::program_name_tbl[partinfo->program_number];
      // canvas->setFont(&fonts::efontJA_16_b);
      float fx = _client_rect.w / 232.0f;
      if (fx < 0.5f) { fx = 0.5f; }
      float fy = _client_rect.h / 152.0f;
      if (fy < 1.0f) { fy = 1.0f; }
      canvas->setTextSize(fx, fy);
      canvas->setTextDatum(m5gfx::textdatum_t::top_center);
      canvas->setTextColor(isEnabled ? 0xFFFFFFu : 0x7F7F7Fu);
      {
        int y = offset_y + getY((7 << 8) + 128)-2;
        canvas->drawString(name, offset_x + 1 + (_client_rect.w >> 1), y);
      }
    }
  }

  void draw_easymode(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect)
  {
    auto partinfo = &system_registry->current_slot->chord_part[_part_index].part_info;
    // auto partinfo = &param->multipart->channel_info_list[part_index];
    int x = offset_x + ((_client_rect.w - 64) >> 1);
    int y = offset_y + ((_client_rect.h - 64) >> 1) - getY(128);
    if (_isEmpty) {
      // パートの中身が空の場合はグレー塗りにしてパートが存在しないように見せる
      canvas->fillRect(offset_x + 1, offset_y + 1, _client_rect.w - 2, _client_rect.h - 2, 0x808080u);
      return;
    }
    if ((clip_rect->right() < x) || (clip_rect->left()  > (x + icon_width)))
    { return; }
    canvas->pushGrayscaleImage( x
                      , y
                      , icon_width
                      , icon_height
                      , &resource_icon_instrument_64x64x36[resource_program_icon_table[partinfo->getTone()] * (icon_width * icon_height)]
                      , m5gfx::color_depth_t::grayscale_8bit
                      , _color_hit
                      , _backcolor
                      );
  }

  rect_t getHighlightRect(int offset_x, int offset_y)
  {
    int x = getX(x_highlight_256);
    int w = getX(x_highlight_256 + 256) - x;
    int y = getY(-128);
    int h = getY((7 << 8) + 128) - y;
    x -= w >> 1;
    return { x + offset_x, y + offset_y, w, h };
  }

  void draw_normalmode(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect)
  {
    auto partinfo = &system_registry->current_slot->chord_part[_part_index].part_info;
    // auto partinfo = &param->multipart->channel_info_list[part_index];

    bool clear_confirm = system_registry->chord_play.getConfirm_AllClear();

    bool isEnabled = partinfo->getEnabled();

    int r = (std::min(_client_rect.w , _client_rect.h) + 12) / 24;
    { // 背景ドット描画
      canvas->setColor(system_registry->color_setting.getArpeggioNoteBackColor());
      // canvas->setColor(param->color_set->arpeggio_note_back);
      for (int j = 0; j < def::app::max_arpeggio_step; j += 2)
      // for (int j = 0; j < arpeggio_step_number_max; j += 2)
      {
        int x = getX(j << 8);
        if (x < 0) { continue; }
        if (x > _client_rect.w) { break; }
        x += offset_x;
        for (int i = 7; i > 0; --i) {
          int y = offset_y + getY(i << 8);
          canvas->fillCircle(x, y, r >> 1);
        }
      }
    }

    {
      auto color = system_registry->color_setting.getArpeggioStepColor();
      if (!isEnabled) { color = (color >> 1) & 0x7F7F7Fu; }
      auto rect = getHighlightRect(offset_x, offset_y);
      canvas->fillRect(rect.x, rect.y, rect.w, rect.h, color);
    }

    // canvas->setFont(&fonts::TomThumb);
    // canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
    // canvas->setTextSize(1, 1);
    int j = getIndexByX(clip_rect->x - r - offset_x + _client_rect.x);
    int je = getIndexByX(clip_rect->right() + r - offset_x + _client_rect.x);
    if (j < 0) { j = 0; }
    if (je >= def::app::max_arpeggio_step) { je = def::app::max_arpeggio_step - 1; }
    for (; j <= je; ++j) {
      int x = getX(j << 8) + offset_x;

      {
        int y = offset_y + getY(0);
        int xt = x + 1;
        if (j == partinfo->getAnchorStep()) {
          bool last = false;
          for (int yp = 0; !last; ++yp) {
            last = (yp == (r << 1) + 1);
            canvas->setColor(0x000080u);
            int w = r + (yp >> 2);
            if (r > 5) {
              canvas->drawPixel(xt - w-1, y-r+yp, TFT_LIGHTGRAY);
              canvas->drawPixel(xt + w-1, y-r+yp, TFT_LIGHTGRAY);
              if (yp != 0 && !last) {
                canvas->setColor(0x000080u);
              }
            }
            canvas->fillRect(xt -w, y-r+yp, (w << 1) - 1, 1);
          }
        }
        if (j == partinfo->getLoopStep()) {
          canvas->fillTriangle(xt-(r*3>>1), y, xt+r, y-r, xt+r,y+r, 0x800000u);
          if (r > 5)
          canvas->drawTriangle(xt-(r*3>>1), y, xt+r, y-r, xt+r,y+r, TFT_LIGHTGRAY);
        }
        // 列番号の描画
        // canvas->drawNumber(j+1, x+4, y+1);
      }

      bool highlight = (j == system_registry->chord_play.getPartStep(_part_index));
      // bool highlight = (j == partinfo->current_step);
      int lighting = (isEnabled ? 1 : 0) + (highlight ? 0 : -1);

      if (!partinfo->isDrumPart())
      {
        int y = offset_y + getY(7 << 8);
        auto style = system_registry->current_slot->chord_part[_part_index].arpeggio.getStyle(j);
        // auto style = partinfo->arpeggio_pattern.step[j].arpeggio_style;
        static constexpr const uint32_t style_color_tbl[] = {
          0x339933u,  // arpeggio_pattern_t::arpeggio_style_t::same_time
          0xFF6666u,  // arpeggio_pattern_t::arpeggio_style_t::low_to_high
          0x6666FFu,  // arpeggio_pattern_t::arpeggio_style_t::high_to_low
          0x999999u,  // arpeggio_pattern_t::arpeggio_style_t::mute
          0x111111u,  // arpeggio_pattern_t::arpeggio_style_t::return_to_start
        };
        uint32_t color = 0;
        if (style < (sizeof(style_color_tbl) / sizeof(uint32_t))) {
          color = style_color_tbl[style];
        }
        if (j > partinfo->getLoopStep()) { --lighting; }
        if (lighting) {
          uint32_t add = (lighting < 0) ? 0xE0E0E0u : 0x202020u;
          color = add_color(color, add);
        }

        switch (style) {
        case def::play::arpeggio_style_t::high_to_low:
          canvas->fillTriangle(x, y+r, x-r, y-r, x+r,y-r, color);
          canvas->drawTriangle(x, y+r, x-r, y-r, x+r,y-r, TFT_LIGHTGRAY);
          break;
        case def::play::arpeggio_style_t::low_to_high:
          canvas->fillTriangle(x, y-r, x-r, y+r, x+r,y+r, color);
          canvas->drawTriangle(x, y-r, x-r, y+r, x+r,y+r, TFT_LIGHTGRAY);
          break;
        case def::play::arpeggio_style_t::mute:
          canvas->fillRect(x-(r), y-(r), r<<1, r<<1, color);
          canvas->drawRect(x-(r), y-(r), r<<1, r<<1, TFT_LIGHTGRAY);
          break;
        case def::play::arpeggio_style_t::same_time:
          canvas->fillCircle(x, y, r>>1, color);
          canvas->drawCircle(x, y, r>>1, TFT_LIGHTGRAY);
          break;
        default: break;
        }
      }
      uint32_t fore_color = system_registry->color_setting.getArpeggioNoteForeColor();
      uint32_t back_color = system_registry->color_setting.getArpeggioNoteBackColor();
      // uint32_t fore_color = param->color_set->arpeggio_note_fore;
      // uint32_t back_color = param->color_set->arpeggio_note_back;
      if (lighting) {
        uint32_t add = (lighting < 0) ? 0xE0E0E0u : 0x202020u;
        fore_color = add_color(fore_color, add);
        back_color = add_color(back_color, add);
      }
      // auto step = &(partinfo->arpeggio_pattern.step[j]);
      canvas->setColor(fore_color);
      for (int i = 5 + partinfo->isDrumPart(); i >= 0; --i) {
        auto v = system_registry->current_slot->chord_part[_part_index].arpeggio.getVelocity(j, i);
        // auto v = step->velocity[i];
        if (v) {
          int y = offset_y + getY((i + 1) << 8);
          if (v < 0) {
            if (!clear_confirm) {
              canvas->fillRect(x - (r >> 1), y - (r >> 1), r, r, 0x999999u);
            }
            canvas->drawRect(x - (r >> 1), y - (r >> 1), r, r, fore_color);
          } else {
            if (clear_confirm) {
              canvas->drawArc(x, y, r, 0, 90, 90 + v * 36 / 10, fore_color);
            } else {
              canvas->fillArc(x, y, r, 0, 90, 90 + v * 36 / 10, fore_color);
            }
          }
        }
      }
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (!_need_draw) { return; }
    fill_background(param, canvas, offset_x, offset_y, clip_rect);

    if (_isDetailMode) {
      draw_normalmode(param, canvas, offset_x, offset_y, clip_rect);
    } else {
      draw_easymode(param, canvas, offset_x, offset_y, clip_rect);
    }
  }
  void changePage(int pageindex) {
    pageindex = (pageindex < 0) ? 0 : (pageindex > ((def::app::max_arpeggio_step >> 3)-1) ) ? ((def::app::max_arpeggio_step >> 3)-1) : pageindex;
    // pageindex = (pageindex < 0) ? 0 : (pageindex > ((arpeggio_step_number_max>>3)-1) ) ? ((arpeggio_step_number_max>>3)-1) : pageindex;
    x_scroll_target = pageindex * 256 * 8;
  }
  int getCurrentPage(void) const {
    return x_scroll_target >> 11;
  }
  int getX(int x256) {
    return ((x256 - x_scroll_current + 128) * _client_rect.w) / (256 * 8);
  }
  int getY(int y256) {
    return ((y256 + 128) * _client_rect.h) / (256 * 10);
  }
  int getIndexByX(int x) {
    x -= _client_rect.x;
    return (((x * 256 * 8) / _client_rect.w) + x_scroll_current) >> 8;
  }
  int getIndexByY(int y) {
    y -= _client_rect.y;
    return ((y * 10) / (_client_rect.h));
  }

  int16_t x_highlight_256 = -16;
  int16_t x_scroll_current = 0;
  int16_t x_scroll_target = 0;
  void setPartIndex(uint8_t part_index) { _part_index = part_index; }
  void setNeedDraw(bool need_draw) { _need_draw = need_draw; }
protected:
  uint8_t _part_index;
};
static ui_partinfo_t ui_partinfo_list[def::app::max_chord_part];

static void setPartInfoNeedDraw(bool need_draw) {
  for (int i = 0; i < def::app::max_chord_part; ++i) {
    ui_partinfo_list[i].setNeedDraw(need_draw);
  }
}

struct ui_arpeggio_edit_t : public ui_partinfo_t
{
protected:
  int16_t _cursor_target_x_256 = 0;
  int16_t _cursor_target_y_256 = 0;
  int16_t _cursor_current_x_256 = 0;
  int16_t _cursor_current_y_256 = 0;
  uint8_t _cursor_x = 0x80;
  uint8_t _cursor_y = 0x80;
  int16_t _cursor_display_x = 0;
  int16_t _cursor_display_y = 0;

  def::command::command_param_t _command_param[4];
  def::command::edit_enc2_target_t _enc2_target;

  uint8_t _part_volume = 0;
  uint8_t _program = 0;
  uint8_t _end_point = -1;
  uint8_t _anchor_step = -1;
  uint8_t _displacement = -1;
  int8_t _position = 0;
  KANTANMusic_Voicing _voicing = KANTANMusic_Voicing::KANTANMusic_MAX_VOICING;
  const char* _voicing_name = "";
  int _edit_velocity = 0;
  bool _is_enabled = false;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool visible = !_client_rect.empty();
    bool moving = _client_rect != _target_rect;

    ui_base_t::update_impl(param, offset_x, offset_y);
    update_inner(param, offset_x, offset_y);
    bool flg_update = false;
    auto mode = system_registry->runtime_info.getPlayMode();
    auto target_index = system_registry->chord_play.getEditTargetPart();

    if (!moving) {
      if (!visible && mode == def::playmode::chord_edit_mode) {
        _isDetailMode = true;
        _is_follow_highlight = true;
        _part_index = target_index;
        flg_update = true;
        _is_enabled = true;
        setClientRect(ui_partinfo_list[target_index].getClientRect());
        setTargetRect({0, 0, main_area_width, main_area_height});
        _cursor_x = 0x80;
        _cursor_y = 0x80;
      } else
      if (visible && (mode != def::playmode::chord_edit_mode || _part_index != target_index)) {
        flg_update = true;
        _is_enabled = false;
        setTargetRect(ui_partinfo_list[_part_index].getTargetRect());
        setPartInfoNeedDraw(true);
      }
    }

    if (visible && moving && getClientRect() == getTargetRect()) {
      setPartInfoNeedDraw(!_is_enabled);
      if (!_is_enabled) {
        setTargetRect({0, 0, 0, 0});
        setClientRect({0, 0, 0, 0});
      }
  }

  
    auto part = &system_registry->current_slot->chord_part[_part_index];
    auto part_info = &part->part_info;

    if (!_target_rect.empty()) {
      // サブボタン群の表示更新があるか確認
      bool subbutton_update = false;
      auto enc2_target = (def::command::edit_enc2_target_t)(system_registry->chord_play.getEditEnc2Target());
      if (_enc2_target != enc2_target) {
        _enc2_target = enc2_target;
        subbutton_update = true;
      }
      for (int i = 0; i < def::hw::max_sub_button; ++i) {
        auto sub_button_index = i;
        auto pair0 = system_registry->sub_button.getCommandParamArray(sub_button_index);
        auto pair1 = system_registry->sub_button.getCommandParamArray(sub_button_index + def::hw::max_sub_button);
        auto command_param_0 = pair0.array[0].getParam();
        auto command_param_1 = pair1.array[0].getParam();

        if (enc2_target == command_param_0) {
          sub_button_index = i;
        } else
        if (enc2_target == command_param_1) {
          sub_button_index = i + def::hw::max_sub_button;
        } else {
          // サブボタンのスワップを考慮
          bool sub_button_swap = system_registry->runtime_info.getSubButtonSwap();
          if (sub_button_swap) {
            sub_button_index = i + def::hw::max_sub_button;
          }
        }
        auto pair = system_registry->sub_button.getCommandParamArray(sub_button_index);
        auto command_param = pair.array[0];
        if (_command_param[i] != command_param) {
          _command_param[i] = command_param;
          subbutton_update = true;
        }
      }
      uint8_t part_volume = part_info->getVolume();
      if (_part_volume != part_volume) {
        _part_volume = part_volume;
        subbutton_update = true;
      }
      uint8_t program = part_info->getTone();
      if (_program != program) {
        _program = program;
        subbutton_update = true;
      }
      int8_t position = part_info->getPosition();
      if (_position != position) {
        _position = position;
        subbutton_update = true;
      }
      auto voicing = part_info->getVoicing();
      if (_voicing != voicing) {
        _voicing = voicing;
        _voicing_name = def::play::GetVoicingName(voicing);
        subbutton_update = true;
      }
      int velo = system_registry->runtime_info.getEditVelocity();
      if (_edit_velocity != velo) {
        _edit_velocity = velo;
        subbutton_update = true;
      }
      uint8_t end_point = part_info->getLoopStep();
      if (_end_point != end_point) {
        _end_point = end_point;
        subbutton_update = true;
      }
      uint8_t anchor_step = part_info->getAnchorStep();
      if (_anchor_step != anchor_step) {
        _anchor_step = anchor_step;
        subbutton_update = true;
      }
      uint8_t displacement = part_info->getStrokeSpeed();
      if (_displacement != displacement) {
        _displacement = displacement;
        subbutton_update = true;
      }
      if (subbutton_update) {
        int top = getY(128 + (7 << 8));
        int bottom = getY(128 + (9 << 8));
        int h = bottom - top;
        int y = offset_y + top;
        int x = offset_x;
        int w = _client_rect.w;
        param->addInvalidatedRect({x, y, w, h});
      }

      bool cursor_update = (param->prev_msec >> 8) != (param->current_msec >> 8);
      if (cursor_update) { // TODO:暫定処置。これを無くしても良い状態に差分更新を実装したい
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      }

      int cx = system_registry->chord_play.getPartStep(_part_index);
      if (cx < 0) { cx = 0; }
      auto cy = system_registry->chord_play.getCursorY() + 1;
      changePage(cx >> 3);
      if (_cursor_x != cx || _cursor_y != cy || (getClientRect() != getTargetRect())) {
        _cursor_x = cx;
        _cursor_y = cy;
        _cursor_target_x_256 = cx << 8;
        _cursor_target_y_256 = cy << 8;
        flg_update = true;
      }
      if (_cursor_current_x_256 != _cursor_target_x_256
       || _cursor_current_y_256 != _cursor_target_y_256) {
        _cursor_current_x_256 = smooth_move(_cursor_target_x_256, _cursor_current_x_256, param->smooth_step);
        _cursor_current_y_256 = smooth_move(_cursor_target_y_256, _cursor_current_y_256, param->smooth_step);
      }
      int new_display_x = getX(_cursor_current_x_256);
      int new_display_y = getY(_cursor_current_y_256);
      if (_cursor_display_x != new_display_x || _cursor_display_y != new_display_y) {
        param->addInvalidatedRect({_cursor_display_x + offset_x - 24, _cursor_display_y + offset_y - 24, 48, 48});
        _cursor_display_x = new_display_x;
        _cursor_display_y = new_display_y;
        cursor_update = true;
      }
      if (cursor_update) {
        param->addInvalidatedRect({_cursor_display_x + offset_x - 24, _cursor_display_y + offset_y - 24, 48, 48});
        // flg_update = true;
      }
    }
    if (flg_update) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    fill_background(param, canvas, offset_x, offset_y, clip_rect);
    draw_normalmode(param, canvas, offset_x, offset_y, clip_rect);

    canvas->setTextSize(1);
    canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
    for (int j = 0; j < def::app::max_arpeggio_step; j += 2) {
      int x = getX(j << 8);
      if (x < 0) { continue; }
      if (x > _client_rect.w) { break; }
      x += offset_x + 4;
      int y = offset_y + getY(0);
      canvas->drawNumber((j>>1)+1, x, y);
    }

    const int r = (std::min(_client_rect.w , _client_rect.h) + 12) / 24;

    bool invert = 1 & (param->current_msec >> 8);
    {
      // int r = getX(128) - getX(0);
      int x = _cursor_display_x + offset_x;
      int y = _cursor_display_y + offset_y;
      // カーソルを描画
      canvas->drawCircle(x, y, r + 5, invert ? TFT_YELLOW : TFT_RED);
      canvas->drawCircle(x, y, r + 7, invert ? TFT_RED : TFT_YELLOW);
    }

    if (system_registry->chord_play.getConfirm_Paste())
    { // コピー/ペースト(クリップボード)のパターンを描画
      auto part = &system_registry->current_slot->chord_part[_part_index];
      auto part_info = &part->part_info;

      int range_w = system_registry->chord_play.getRangeWidth();
      for (int j = 0; j < range_w; ++j) {
        int step = _cursor_x + j;
        int x = getX(step << 8) + offset_x;
        if (x + r < 0) { continue; }
        if (x - r > _client_rect.w) { break; }

        canvas->setColor(TFT_YELLOW);
        for (int i = 5 + part_info->isDrumPart(); i >= 0; --i) {
          auto v = system_registry->clipboard_arpeggio.getVelocity(j, i);
          if (v) {
            int y = offset_y + getY((i + 1) << 8);
            if (v < 0) {
              canvas->drawRect(x - (r >> 1), y - (r >> 1), r, r);
            } else {
              canvas->drawArc(x, y, r, 0, 90, 90 + v * 36 / 10);
            }
          }
        }
      }
    }


    const uint32_t fore_color = 0xFFFF44u;
    const uint32_t unfocus_color = 0xCCDDEEu;

    canvas->setTextSize(1);
    const int y = offset_y + getY(9 << 8);
    for (int i = 0; i < def::hw::max_sub_button; ++i)
    { // サブボタン機能の描画
      auto command_param = _command_param[i];
      auto param = command_param.getParam();

      uint32_t color = (param == _enc2_target) ? fore_color : unfocus_color;
      canvas->setColor(color);
      canvas->setTextColor(color);
      canvas->setTextDatum(textdatum_t::middle_center);

      // int x = offset_x + getX(x_scroll_current + ((i*2) << 8));
      int left = (i * _client_rect.w) >> 2;
      int right = ((i+1) * _client_rect.w) >> 2;
      int x = offset_x + left;
      int w = right - left;
      int x1 = x + (w>>2);
      int x2 = x + (w>>1);
      int x3 = x + (w*3>>2);

      switch (param) {
      case def::command::edit_enc2_target_t::part_vol:
        {
          int v = _part_volume;
          canvas->fillArc(x1, y, r, 0, 90, 90 + v * 36 / 10);
          canvas->drawNumber(v, x3, y);
        }
        break;

      case def::command::edit_enc2_target_t::velocity:
        {
          int v = _edit_velocity;
          if (v < 0) {
            canvas->fillRect(x1 - (r >> 1), y - (r >> 1), r, r, color);
            canvas->drawString("mute", x3, y);
          } else {
            canvas->fillArc(x1, y, r, 0, 90, 90 + v * 36 / 10, color);
            canvas->drawNumber(v, x3, y);
          }
        }
        break;

      case def::command::edit_enc2_target_t::program:
        {
          // プログラムナンバーは 1~128 として表示する
          int p = _program + 1;
          canvas->drawNumber(p, x2, y);
        }
        break;

      case def::command::edit_enc2_target_t::endpoint:
        {
          int p = (_end_point>>1) + 1;
          canvas->drawNumber(p, x2, y);
        }
        break;

      case def::command::edit_enc2_target_t::banlift:
        {
          int p = (_anchor_step>>1) + 1;
          canvas->drawNumber(p, x2, y);
        }
        break;

      case def::command::edit_enc2_target_t::displacement:
        {
          int p = _displacement;
          canvas->drawNumber(p, x2, y);
        }
        break;

      case def::command::edit_enc2_target_t::position:
        {
          int pos = _position + def::app::position_table_offset;
          canvas->drawString(def::app::position_name_table.at(pos)->get(), x2, y);
        }
        break;

      case def::command::edit_enc2_target_t::voicing:
        {
          canvas->drawString(_voicing_name, x2, y);
        }
        break;

      default: break;

      }
    }
  }
};
static ui_arpeggio_edit_t ui_arpeggio_edit;

struct ui_chord_part_container_t : public ui_container_t
{
protected:
  def::playmode::playmode_t _prev_mode;

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override
  {
    auto mode = system_registry->runtime_info.getCurrentMode();
    if (_prev_mode != mode) {
      _prev_mode = mode;
      switch (mode) {
      case def::playmode::chord_mode:
      case def::playmode::chord_edit_mode:
      case def::playmode::note_mode:
      case def::playmode::drum_mode:
      case def::playmode::seq_edit_mode:
      case def::playmode::seq_play_mode:
        setTargetRect({0, header_height, main_area_width, main_area_height}); // TODO: 仮の値
        break;

      default:
        setTargetRect({0, header_height, 0, main_area_height}); // TODO: 仮の値
        break;
      }
    }
    ui_container_t::update_impl(param, offset_x, offset_y);
  }
};
static ui_chord_part_container_t ui_chord_part_container;

struct ui_sequence_timeline_t : public ui_base_t
{
protected:
  static constexpr const int32_t max_visible_step = 5;
  static constexpr const int32_t step_icon_width = main_area_width / max_visible_step;
  int16_t _current_step_index = 0;
  int32_t _x_scroll_current = 0;
  int32_t _x_scroll_target = 0;
  int32_t _x_scroll_offset = 0;
  uint8_t _offset_step = 0;
  uint8_t _highlight_index = 0;
  def::seqmode::seqmode_t _prev_mode = def::seqmode::seq_free_play;

  sequence_chord_desc_t _desc[max_visible_step + 2];
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto mode = system_registry->runtime_info.getGuiSequenceMode();

    bool visible = mode != def::seqmode::seq_free_play;
    bool need_init = _prev_mode != mode;
    if (need_init) {
      _prev_mode = mode;
      if (mode == def::seqmode::seq_song_edit) {
        _offset_step = 2;
      } else {
        _offset_step = 0;
      }

      auto r = getTargetRect();
      if (visible) {
        // r.x = 0;
        // r.w = disp_width;
        r.y = disp_height - main_btns_height;
        r.h = main_btns_height;
      } else {
        // r.x = disp_width;
        // r.w = 0;
        r.y = disp_height;
        r.h = 0;
      }
      setTargetRect(r);
    }

    ui_base_t::update_impl(param, offset_x, offset_y);

    if (_client_rect.empty()) { return; }

    int32_t current_stepindex = (int32_t)system_registry->runtime_info.getSequenceStepIndex();
    _x_scroll_target = current_stepindex * step_icon_width * 256;
    if (need_init) {
      _x_scroll_current = _x_scroll_target;
    } else {
      _x_scroll_current = smooth_move(_x_scroll_target, _x_scroll_current, (param->smooth_step + 2) >> 2);
    }
    int32_t visible_stepindex = _x_scroll_current / (step_icon_width * 256);
    int32_t visible_scroll = visible_stepindex * step_icon_width * 256;
    int32_t offset = (visible_scroll - _x_scroll_current + 128) >> 8;
    visible_stepindex -= _offset_step;

    _highlight_index = current_stepindex - visible_stepindex;
    visible_stepindex -= 1;

    if (_current_step_index != visible_stepindex || _x_scroll_offset != offset || need_init) {
      _current_step_index = visible_stepindex;
      _x_scroll_offset = offset;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      for (int32_t i = 0; i < max_visible_step+2; ++i) {
        _desc[i] = system_registry->current_sequence->getStepDescriptor(visible_stepindex + i);
      }
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    image_dark_shift(canvas, offset_x, offset_y, _client_rect.w, _client_rect.h, 2);

    canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
    canvas->setTextColor(TFT_WHITE);
    int32_t y_degree = offset_y + (main_btns_height >> 1);
    int32_t xe = offset_x + _x_scroll_offset;

    // image_dark_shift(canvas, offset_x + _offset_step * step_icon_width, offset_y, step_icon_width, main_btns_height);
    // image_color_or(canvas, offset_x + _offset_step * step_icon_width, offset_y, step_icon_width - 1, main_btns_height, 0x4200u);
    // image_color_or(canvas, offset_x + _offset_step * step_icon_width, offset_y, 1, main_btns_height, 0x8410u);
    // image_color_or(canvas, offset_x + (_offset_step+1) * step_icon_width-1, offset_y, 1, main_btns_height, 0x8410u);
    for (int32_t i = 0; i <= max_visible_step; ++i) {
      int32_t x = xe;
      xe += step_icon_width;
      if (xe < clip_rect->left() || clip_rect->right() < x) {
        continue;
      }
      if (i == _highlight_index) {
        int xtmp = x < clip_rect->left() ? clip_rect->left() : x;
        int xetmp = xe > clip_rect->right() ? clip_rect->right() : xe;
        image_dark_shift(canvas, xtmp, offset_y, xetmp - xtmp, main_btns_height);
        image_color_or(canvas, xtmp, offset_y, xetmp - xtmp, main_btns_height, 0x4200);
      }
      if (xe > clip_rect->left()) {
        image_color_or(canvas, xe - 1, offset_y, 1, main_btns_height, 0x8410);
      }
      // canvas->drawFastVLine(xe - 1, offset_y, main_btns_height, TFT_LIGHTGRAY);

      const auto &prev_desc = _desc[i];
      const auto &desc = _desc[i+1];
      const auto degree = desc.getDegree();
   // if (degree && prev_desc != desc)
      if (degree)
      {
        int32_t x_center = x + (step_icon_width >> 1);
        {
          auto modifier = desc.getModifier();
          auto name_tbl = def::command::command_name_table[def::command::command_t::chord_modifier];
          int32_t y_mod = offset_y + main_btns_height - (main_btns_height / 6);
          int32_t x_mod = x + (step_icon_width >> 1);
          const char* mod_str = name_tbl[modifier];
          canvas->setTextSize(1, 2);
          canvas->drawString(mod_str, x_mod, y_mod);
        }

        auto x_degree = x_center;
        auto semitone = desc.getSemitoneShift();
        auto minor_swap = desc.getMinorSwap();
        if (semitone || minor_swap) {
          canvas->setTextSize(1, 1);
          x_degree -= 8;
          if (semitone) {
            auto text = (semitone < 0) ? "♭" : "♯";
            canvas->drawString(text, x_degree + 16, y_degree - 6);
          }
          if (minor_swap) {
            canvas->drawString("〜", x_degree + 16, y_degree + 6);
          }
        }
        canvas->setTextSize(2, 2);
        canvas->drawNumber(degree, x_degree, y_degree);

        if (prev_desc.getPartBits() != desc.getPartBits() || prev_desc.getSlotIndex() != desc.getSlotIndex())
        {
          uint32_t colors[] = {
            system_registry->color_setting.getDisablePartColor(),
            add_color(system_registry->color_setting.getEnablePartColor(), 0x404040u)
          };
          for (int part = 0; part < def::app::max_chord_part; ++part)
          {
            auto color = colors[desc.getPartEnable(part)];
            int32_t y_part = 4 + offset_y + (part < 3 ? 0 : (main_btns_height / 10));
            int32_t x_part = x + (part % 3) * (step_icon_width / 4) + (step_icon_width / 8);
            canvas->fillRect( x_part, y_part, (step_icon_width / 4)-1, (main_btns_height / 10) - 1,
                              color);
          }

          auto slotindex = desc.getSlotIndex();
          for (int slot = 0; slot < def::app::max_slot; ++slot)
          {
            auto color = colors[(slot == slotindex)];
            int32_t y_slot = 4 + (main_btns_height / 10)*2 + offset_y + (slot < 4 ? 0 : 5);
            int32_t x_slot = x + (slot % 4) * (step_icon_width / 5) + (step_icon_width / 8);
            canvas->fillRect( x_slot, y_slot, (step_icon_width / 5)-1, 5 - 1,
                              color);
          }
        }
      }
    }
    for (int i = 1; i < 3; ++i) {
      image_color_or(canvas, offset_x, offset_y + (main_btns_height/3) * i, _client_rect.w, 1, 0x8410u);
//      canvas->drawFastHLine(offset_x, offset_y + (main_btns_height/3) * i, _client_rect.w, TFT_DARKGRAY);
    }
  }
};
static ui_sequence_timeline_t ui_sequence_timeline;

struct ui_menu_header_t : public ui_base_t
{
protected:
  static constexpr const size_t max_ancestors_size = 6;
  struct ancestors_t {
    const char* title;
    int16_t current_x = main_area_width;
    int16_t target_x = main_area_width;
  } _ancestors[max_ancestors_size];

  def::menu_category_t _category;
  registry_t::history_code_t _history_code;
  bool _is_visible = true;
  bool _is_closing = false;
  int8_t _level = -1;

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override
  {
    auto show = system_registry->runtime_info.getCurrentMode() == def::playmode::menu_mode;
    auto category = static_cast<def::menu_category_t>(system_registry->menu_status.getMenuCategory());

    if (_is_visible && !_is_closing && (!show || _category != category)) {
      _is_closing = true;
      setTargetRect({0, header_height, main_area_width, 0 });
      _level = -1;
      for (int lv = 0; lv < max_ancestors_size; ++lv) {
        _ancestors[lv].target_x = main_area_width;
      }
    } else
    // 非表示状態からメニュー表示状態への遷移
    if (show && (!_is_visible || _is_closing) && category != def::menu_category_t::menu_none) {
      _is_visible = true;
      _is_closing = false;
      _category = category;
      setTargetRect({0, header_height, main_area_width, menu_header_height });
    }
    // メニュー表示状態から非表示状態への遷移
    bool invalidated = false;
    if (show) {
      auto code = system_registry->menu_status.getHistoryCode();
      if (_history_code != code) {
        _history_code = code;
        size_t level = system_registry->menu_status.getCurrentLevel();
        if (_level != level) {
          _level = level;
          invalidated = true;

          size_t lv = 0;
          int target_x = 0;
          _gfx->setTextSize(1, 2);
          for (; lv <= level; lv++) {
            auto item = menu_control.getItemByLevel(lv);
            _ancestors[lv].target_x = target_x;
            auto title = item->getTitleText();
            _ancestors[lv].title = title;
            target_x += 8 + _gfx->textWidth(title);
          }
          target_x -= 8;
          if (target_x > main_area_width) {
            int diff = main_area_width - target_x;
            for (int i = 0; i < lv; ++i) {
              _ancestors[i].target_x += diff;
            }
          }
          for (; lv < max_ancestors_size; ++lv) {
            _ancestors[lv].target_x = main_area_width;
          }
        }
      }
    }
    if (_is_visible) {
      for (int i = 0; i < max_ancestors_size; ++i) {
        if (_ancestors[i].current_x != _ancestors[i].target_x) {
          _ancestors[i].current_x = smooth_move(_ancestors[i].target_x, _ancestors[i].current_x, param->smooth_step);
          invalidated = true;
        } else {
          if (_ancestors[i].target_x >= main_area_width) {
            _ancestors[i].title = nullptr;
          }
        }
      }
      if (invalidated) {
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      }
      if (_is_closing && getClientRect() == getTargetRect()) {
        _is_visible = false;
      }
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (!_is_visible) { return; }
    uint32_t color = 0x1F1F1Fu;
    canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, color);

    int y = offset_y + menu_header_height - 1;
    int x = 0;

    auto current_level = system_registry->menu_status.getCurrentLevel();
    canvas->setTextSize(1, 2);
    canvas->setTextDatum(m5gfx::textdatum_t::bottom_left);
    int lv = 0;
    for (auto& ancestor : _ancestors)
    {
      if (ancestor.title == nullptr) { break; }
      if (lv++ == current_level) {
        canvas->setTextColor(TFT_WHITE);
      } else {
        canvas->setTextColor(TFT_DARKGRAY);
      }
      x = offset_x + ancestor.current_x;
      auto str = ancestor.title;
      if (str[0]) {
        canvas->drawString(str, x, y);
      }
    }
    canvas->drawFastHLine(offset_x, y, _client_rect.w, TFT_YELLOW);
  }
};
static ui_menu_header_t ui_menu_header;

// メニューUI用の描画クラス
struct menu_drawer_t
{
  static constexpr const int scroll_gap = 32;
  menu_drawer_t(uint16_t menu_index, menu_item_ptr menu_item)
  : _menu_item { menu_item }
  , _menu_index { menu_index }
  {}

  virtual void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y)
  {
    if (_current_focus_rect != _target_focus_rect) {
      int move_diff = 0;
      if (_current_focus_rect.empty()) {
        _current_focus_rect = _target_focus_rect;
      } else {
        int prev_y = _current_focus_rect.y;
        _current_focus_rect.smooth_move(_target_focus_rect, param->smooth_step);
        move_diff = _current_focus_rect.y - prev_y;
      }
      int y = _current_focus_rect.y - _y_scroll;
      int invalidated_y0 = y + (move_diff < 0 ? 0 : -move_diff);
      int invalidated_y1 = y + (move_diff < 0 ? -move_diff : 0) + _current_focus_rect.h;
      if (invalidated_y0 < 0) { invalidated_y0 = 0; }
      if (invalidated_y1 > ui->getClientRect().h) { invalidated_y1 = ui->getClientRect().h; }
      if (invalidated_y1 > invalidated_y0) {
        param->addInvalidatedRect({_current_focus_rect.x + offset_x, invalidated_y0 + offset_y, _current_focus_rect.w, invalidated_y1 - invalidated_y0});
      }

      int new_y_scroll = _y_scroll;
      if (new_y_scroll < _current_focus_rect.bottom() - ui->getClientRect().h + scroll_gap) {
        new_y_scroll = _current_focus_rect.bottom() - ui->getClientRect().h + scroll_gap;
        if (new_y_scroll > _scroll_limit) { new_y_scroll = _scroll_limit; }
      }
      if (new_y_scroll > _current_focus_rect.y - scroll_gap ) {
        new_y_scroll = _current_focus_rect.y - scroll_gap;
      }
      if (new_y_scroll < 0) { new_y_scroll = 0; }
      if (_y_scroll != new_y_scroll) {
        _y_scroll = new_y_scroll;
        param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
      }
    }
  }
  virtual void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect)
  {
    uint32_t color = 0x1F1F1Fu;
    canvas->fillScreen(color);

    canvas->fillRoundRect
      ( offset_x + _current_focus_rect.x
      , offset_y + _current_focus_rect.y - _y_scroll
      , _current_focus_rect.w
      , _current_focus_rect.h
      , 4
      , 0x303080u);

    auto cr = ui->getClientRect();

    canvas->drawFastVLine
      ( offset_x
      , offset_y
      , cr.h
      , 0x808080u);

    canvas->drawFastVLine
      ( offset_x + cr.w - 1
      , offset_y
      , cr.h
      , 0x808080u);
  }
protected:
  menu_item_ptr _menu_item = nullptr;
  rect_t _current_focus_rect;
  rect_t _target_focus_rect;
  uint16_t _menu_index = 0;
  int16_t _y_scroll = 0;
  int16_t _scroll_limit = 0;
};

struct menu_drawer_list_t : public menu_drawer_t
{
protected:
  static constexpr const int default_item_height = 32;
  int16_t _min_value; // 選択肢の最小値
  int16_t _focus_pos = -1;
  int16_t _stored_pos = -1;
  bool _draw_index = true;
public:
  menu_drawer_list_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {
    _min_value = menu_item->getMinValue();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto item_height = getItemHeight();
    int item_index = getFocusPos();
    _target_focus_rect = { 0, item_index * item_height, ui->getTargetRect().w, item_height };

    _scroll_limit = getItemCount() * item_height - ui->getClientRect().h;
  }

  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);

    auto tr = ui->getClientRect();

    int y = offset_y - _y_scroll;
    int number_width = 18;
    int x = offset_x + 3;
    int w = tr.w - 6;
    if (_draw_index) {
      x += number_width;
      w -= number_width;
    }

    int count = getItemCount();
    int item_height = getItemHeight();
    canvas->setTextSize(1, 2);
    const char* str;
    for (int i = 0; i < count; ++i, y += item_height) {
      if (y + item_height < clip_rect->y) { continue; }
      if (y > clip_rect->y + clip_rect->h) { break; }

      canvas->setTextColor(_stored_pos == i ? TFT_YELLOW : (_focus_pos == i ? TFT_WHITE : TFT_LIGHTGRAY));

      canvas->setTextDatum(m5gfx::textdatum_t::top_left);
      if (_draw_index) {
        canvas->drawNumber(i + _min_value, x - number_width, y);
      }

      if (nullptr != (str = getLeftText(i))) {
        canvas->drawString(str, x, y);
      }

      if (nullptr != (str = getCenterText(i))) {
        canvas->setTextDatum(m5gfx::textdatum_t::top_center);
        canvas->drawString(str, x + (w >> 1), y);
      }

      if (nullptr != (str = getRightText(i))) {
        canvas->setTextDatum(m5gfx::textdatum_t::top_right);
        canvas->drawString(str, x + w, y);
      }
    }
  }
  virtual int getFocusPos(void) { return 0; }
  virtual int getItemCount(void) { return 0; }
  virtual int getItemHeight(void) { return default_item_height; }
  virtual const char* getLeftText(int index) { return nullptr; }
  virtual const char* getCenterText(int index) { return nullptr; }
  virtual const char* getRightText(int index) { return nullptr; }
};

struct menu_drawer_tree_t : public menu_drawer_list_t
{
protected:
  // static constexpr const int max_children_size = 10;
  // menu_item_ptr _item_array[max_children_size + 1];
  registry_t::history_code_t _history_code;
  std::vector<menu_item_ptr> _item_array;
  uint8_t _childrens_count = 0;
  uint8_t _level;

public:
  menu_drawer_tree_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_list_t(menu_index, menu_item)
  {
    _level = menu_item->getLevel();
    std::vector<uint16_t> menu_id_list;
    _childrens_count = menu_control.getChildrenMenuIDList(&menu_id_list, menu_index);
    for (int i = 0; i < _childrens_count; ++i) {
      _item_array.push_back(menu_control.getItemByMenuID(menu_id_list[i]));
    }
    _item_array.shrink_to_fit();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    auto code = system_registry->menu_status.getHistoryCode();
    if (_history_code != code) {
      _history_code = code;

      auto focus_index = system_registry->menu_status.getSelectIndex(_level);
      auto focus_item = menu_control.getItemByMenuID(focus_index);

      for (int i = 0; i < _childrens_count; ++i) {
        auto item = _item_array[i];
        if (focus_item == item) {
          _focus_pos = i;
        }
      }
    }
    menu_drawer_list_t::update(ui, param, offset_x, offset_y);
  }
  int getItemCount(void) override { return _childrens_count; }
  int getFocusPos(void) override { return _focus_pos; }
  const char* getLeftText(int index) override { return _item_array[index]->getTitleText(); }
  const char* getRightText(int index) override { return _item_array[index]->getValueText(); }
};

struct menu_drawer_normal_t : public menu_drawer_list_t
{
protected:
  size_t _count;
public:
  menu_drawer_normal_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_list_t(menu_index, menu_item)
  {
    _count = menu_item->getSelectorCount();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    int min_value = _menu_item->getMinValue();
    _focus_pos = _menu_item->getSelectingValue() - min_value;
    int stored_pos = _menu_item->getValue() - min_value;
    if (_stored_pos != stored_pos) {
      _stored_pos = stored_pos;
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
    menu_drawer_list_t::update(ui, param, offset_x, offset_y);
  }

  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_list_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
  }
  int getItemCount(void) override { return _count; }
  int getFocusPos(void) override { return _focus_pos; }
  const char* getCenterText(int index) override { return _menu_item->getSelectorText(index); }
};

struct menu_drawer_input_number_t : public menu_drawer_normal_t
{
  menu_drawer_input_number_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_normal_t(menu_index, menu_item)
  {}
};
/*
struct menu_drawer_qrcode_t : public menu_drawer_t
{
  M5Canvas _qr_canvas;
  uint8_t _value = 0xFF;
  std::string _qr_text;
  int8_t _current_scale = 0;
  int8_t _target_scale = 0;
public:
  menu_drawer_qrcode_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {
    _qr_canvas.setColorDepth(1);
    _qr_canvas.setPsram(true);
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto value = _menu_item->getSelectingValue();
    if (_value != value) {
      _target_scale = 0;
      if (_current_scale == 0) {
        _value = value;
        _target_scale = 64;
        _qr_canvas.deleteSprite();
        auto qr_text = _menu_item->getString();
        _qr_canvas.createSprite(33, 33);
        _qr_canvas.fillScreen(TFT_WHITE);
        _qr_canvas.qrcode(qr_text.c_str());
      }
    }
    if (_current_scale != _target_scale) {
      _current_scale = smooth_move(_target_scale, _current_scale, param->smooth_step);
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
  }
  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
    auto cr = ui->getClientRect();
    float scale = _current_scale / 16.0f;
    _qr_canvas.pushRotateZoom(canvas, offset_x + (cr.w >> 1), offset_y + (cr.h >> 1), 0.0f, scale, scale);
  }
};
*/
struct menu_drawer_progress_t : public menu_drawer_t
{
  uint16_t _value = UINT16_MAX;
  std::string _text;
public:
  menu_drawer_progress_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {}

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto value = _menu_item->getSelectingValue();
    if (_value != value) {
      _value = value;

      _text = _menu_item->getString();
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
  }
  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
    auto cr = ui->getClientRect();
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1, 2);
    canvas->setTextDatum(m5gfx::textdatum_t::bottom_center);
    canvas->drawString(_text.c_str(), offset_x + (cr.w >> 1), offset_y + (cr.h >> 1));
    if (_value <= 100) {
      canvas->drawRect(offset_x + ((cr.w - 100) >> 1) - 2, offset_y + (cr.h >> 1)    , 100 + 4, (cr.h >> 3) + 4, TFT_WHITE);
      canvas->fillRect(offset_x + ((cr.w - 100) >> 1)    , offset_y + (cr.h >> 1) + 2,  _value, (cr.h >> 3)    , TFT_GREEN);
    }
  }
};

struct ui_menu_body_t : public ui_base_t
{
  ui_menu_body_t(bool odd_even) : _odd_even { odd_even } {
    setTargetRect({ main_area_width, header_height + menu_header_height, main_area_width, main_area_height - menu_header_height });
  }

protected:
  std::unique_ptr<menu_drawer_t> _drawer = nullptr;

  def::menu_category_t _category;
  bool _is_visible = true;
  bool _is_closing = false;
  const bool _odd_even;
  int8_t _level = -1;
  int8_t _prev_level = -1;

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override
  {
    ui_base_t::update_impl(param, offset_x, offset_y);

    auto show = system_registry->runtime_info.getCurrentMode() == def::playmode::menu_mode;
    auto category = static_cast<def::menu_category_t>(system_registry->menu_status.getMenuCategory());
    int current_level = system_registry->menu_status.getCurrentLevel();

    if ((current_level & 1) == _odd_even) {
      _level = current_level;
    }

    // メニュー表示状態から非表示状態への遷移
    if (_is_visible && !_is_closing && (!show || _category != category || current_level != _level)) {
      _is_closing = true;
      int x = (_level < current_level) ? -main_area_width : main_area_width;
      _target_rect.x = x;
    } else
    if (show && (!_is_visible || _is_closing) && category != def::menu_category_t::menu_none && current_level == _level)
    { // 非表示状態からメニュー表示状態への遷移
      _is_visible = true;
      _is_closing = false;
      _category = category;
      setTargetRect({0, header_height + menu_header_height, main_area_width, main_area_height - menu_header_height });

      _client_rect.x = (_prev_level <= current_level)
                      ?  main_area_width
                      : -main_area_width;

      auto current_index = _level ? system_registry->menu_status.getSelectIndex(_level - 1) : 0;
      auto menu_item = menu_control.getItemByMenuID(current_index);
      if (menu_item) {
        switch (menu_item->getType()) {
        default:
        case menu_item_type_t::mt_tree:
          _drawer.reset(new menu_drawer_tree_t(current_index, menu_item));
          break;

        case menu_item_type_t::mt_normal:
          _drawer.reset(new menu_drawer_normal_t(current_index, menu_item));
          break;

        case menu_item_type_t::input_number:
          _drawer.reset(new menu_drawer_input_number_t(current_index, menu_item));
          break;

        case menu_item_type_t::show_progress:
          _drawer.reset(new menu_drawer_progress_t(current_index, menu_item));
          break;

        // case menu_item_type_t::show_qrcode:
        //   _drawer.reset(new menu_drawer_qrcode_t(current_index, menu_item));
        //   break;
        }
      }
    }
    _prev_level = show ? current_level : -1;
    if (_is_visible && _is_closing && getClientRect() == getTargetRect()) {
      _is_visible = false;
    }

    if (show && _drawer) {
      _drawer->update(this, param, offset_x, offset_y);
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (_is_visible && _drawer) {
      _drawer->draw(this, param, canvas, offset_x, offset_y, clip_rect);
    }
  }
};
static ui_menu_body_t ui_menu_bodys[2] = {
  ui_menu_body_t(0),
  ui_menu_body_t(1),
};

struct ui_filename_t : public ui_base_t
{
  std::string _filename;
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto fn = file_manage.getDisplayFileName();
    if (_filename != fn) {
      _filename = fn;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override {
    canvas->setTextSize(1, 1);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(m5gfx::textdatum_t::bottom_left);
    canvas->drawString(_filename.c_str(), offset_x, offset_y + _client_rect.h);
  }
};
ui_filename_t ui_filename;

static void update_header_container_width(void);

struct ui_left_icon_container_t : public ui_container_t
{
protected:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    int w = 0;
    for (auto ui : _ui_child) {
      ui->update(param, offset_x, offset_y);
      auto tr = ui->getTargetRect();
      if (tr.x != w) {
        param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
        tr.x = w;
        ui->setTargetRect(tr);
        param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
      }
      w += tr.w;
      if (tr.w) {
        w += 2;
      }
    }
    update_header_container_width();
    ui_base_t::update_impl(param, offset_x, offset_y);
  }
};
ui_left_icon_container_t ui_left_icon_container;

struct ui_right_icon_container_t : public ui_container_t
{
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    int32_t w = 0;
    for (auto ui : _ui_child) {
      ui->update(param, offset_x, offset_y);
      auto tr = ui->getTargetRect();
      tr.x = w;
      if (tr.w) {
        w += 2;
      }
      w += tr.w;
      ui->setTargetRect(tr);
    }
    _target_rect.w = w;
    int32_t new_x = disp_width - w;
    int32_t diff = _target_rect.x - new_x;
    _target_rect.x = new_x;
    _client_rect = _target_rect;

    w = 0;
    if (diff) {
      for (auto ui : _ui_child) {
        auto tr = ui->getClientRect();
        tr.x += diff;
        // param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
        // param->addInvalidatedRect({offset_x +    x, offset_y, tr.w, tr.h});
        ui->setClientRect(tr);
      }
    }
    update_header_container_width();
    ui_base_t::update_impl(param, offset_x, offset_y);
  }
};
ui_right_icon_container_t ui_right_icon_container;

static void update_header_container_width(void)
{
  auto left = ui_left_icon_container.getTargetRect();
  auto right = ui_right_icon_container.getTargetRect();
  int32_t w = right.x - left.x - 1;
  if (w < 0) { w = 0; }
  if (left.w != w) {
    left.w = w;
    ui_left_icon_container.setTargetRect(left);
  }
}

struct ui_raw_wave_t : public ui_base_t
{
protected:
  int _raw_wave_pos;
  int _prev_min_y = UINT8_MAX;
  int _prev_max_y = 0;
  int _min_x;
  int _max_x;
  bool _is_visible = false;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    auto visible = system_registry->user_setting.getGuiWaveView();
    visible &= system_registry->runtime_info.getCurrentMode() == def::playmode::playmode_t::chord_mode;
    if (_is_visible != visible) {
      if (visible) {
        setTargetRect({ 0, disp_height - main_btns_height, disp_width, main_btns_height });
        _is_visible = visible;
      } else {
        setTargetRect({ 0, disp_height - (main_btns_height >> 1), disp_width, 0 });
        if (getClientRect().empty()) {
          _is_visible = false;
        }
      }
    }

    ui_base_t::update_impl(param, offset_x, offset_y);

    if (!_is_visible) { return; }

    int start_pos = system_registry->raw_wave_pos - disp_width;
    if (start_pos < 0) {
      start_pos += system_registry->raw_wave_length;
    }
    _raw_wave_pos = start_pos;

    uint8_t min_y = UINT8_MAX;
    uint8_t max_y = 0;
    int min_x = 0;
    int max_x = 0;
    auto wave = system_registry->raw_wave;
    const int loopend = system_registry->raw_wave_length;
    for (int x = 0; x < loopend; ++x) {
      if (min_y > wave[x].first) {
        min_y = wave[x].first;
        min_x = x;
      }
      if (max_y < wave[x].second) {
        max_y = wave[x].second;
        max_x = x;
      }
    }
    min_x -= start_pos;
    if (min_x < 0)
    {
      min_x += system_registry->raw_wave_length;
    }
    _min_x = min_x;

    max_x -= start_pos;
    if (max_x < 0)
    {
      max_x += system_registry->raw_wave_length;
    }
    _max_x = max_x;

    const int ch = _client_rect.h;
    int y0 = min_y < _prev_min_y ? min_y : _prev_min_y;
    int y1 = max_y > _prev_max_y ? max_y : _prev_max_y;
    y0 = (y0 * ch) >> 8;
    y1 = (y1 * ch) >> 8;

    _prev_min_y = (++_prev_min_y < min_y) ? _prev_min_y : min_y;
    _prev_max_y = (--_prev_max_y > max_y) ? _prev_max_y : max_y;

    if (y0 > 0) { --y0; }
    if (y1 < ch-1) { ++y1; }

    param->addInvalidatedRect({offset_x, offset_y + y0, _client_rect.w, y1 - y0});
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override {
    if (!_is_visible || _client_rect.empty()) { return; }

    image_dark_shift(canvas, offset_x, offset_y, _client_rect.w, _client_rect.h);

    const int ch = _client_rect.h;
    const int min_y = (_prev_min_y * ch) >> 8;
    const int max_y = (_prev_max_y * ch) >> 8;
    for (int i = 1; i < 8; ++i) {
      int yy = (i * ch) >> 3;
      image_color_or( canvas
                    , offset_x
                    , offset_y + yy
                    , _client_rect.w, 1
                    , 0x03F0u);
    }
    image_color_or( canvas
                  , offset_x
                  , offset_y + min_y
                  , _client_rect.w, 1
                  , 0xC618u);
    image_color_or( canvas
                  , offset_x
                  , offset_y + max_y
                  , _client_rect.w, 1
                  , 0xC618u);
    { // 波形を描画する
      const int xe = clip_rect->right() - offset_x;
      const int ye = clip_rect->bottom() - offset_y;
      const auto wid = canvas->width();
      auto wave = system_registry->raw_wave;
      auto buf = (m5gfx::swap565_t*)(canvas->getBuffer());
      const auto raw_wave_length = system_registry->raw_wave_length;
      for (int x = -offset_x; x < xe; ++x) {
        int i = _raw_wave_pos + x;
        while (i >= raw_wave_length) {
          i -= raw_wave_length;
        }
        while (i < 0) {
          i += raw_wave_length;
        }
        int y0 = ((wave[i].first  * ch) >> 8) + offset_y;
        int y1 = ((wave[i].second * ch) >> 8) + offset_y;
        if (y0 < 0) { y0 = 0; }
        if (y1 > ch) { y1 = ch; }
        for (int y = y0; y < y1 && y < ye; ++y) { buf[y * wid].raw |= __builtin_bswap16(0xC600); }
        ++buf;
      }
    }
  }
};
ui_raw_wave_t ui_raw_wave;

void gui_t::init(void)
{
  _gfx = &M5.Display;
  _gfx->setRotation(0);
  _gfx->setColorDepth(color_depth);
  _gfx->setFont(&fonts::efontJA_16_b);

#if !defined (M5UNIFIED_PC_BUILD)
  // メモリブロックの断片化への対策として、小さい断片化領域から使用するため、敢えて最大領域を先回りして確保する。
  auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  // 上記の処理により、以下のメモリ確保は二番目に小さい領域から確保されることになる。
  for (int i = 0; i < disp_buf_count; ++i) {
    _draw_buffer[i] = (uint16_t*)m5gfx::heap_alloc_dma(max_disp_buf_pixels * color_depth >> 3);
  }
  // 先回りして確保しておいた領域を解放する。
  m5gfx::heap_free(dummy);
#endif
  for (int i = 0; i < disp_buf_count; ++i) {
    if (_draw_buffer[i] == nullptr) {
      _draw_buffer[i] = (uint16_t*)m5gfx::heap_alloc_dma(max_disp_buf_pixels * color_depth >> 3);
    }
  }

  // disp_width  = _gfx->width();
  // disp_height = _gfx->height();
  ui_background.setTargetRect({ 0,0,disp_width,disp_height });
  ui_background.setClientRect({ 0,0,disp_width,disp_height });
/*
  ui_header.setClientRect({0,0,disp_width,0});
  ui_header.setTargetRect({0,0,disp_width,header_height});
*/

  const int32_t battery_icon_width = 14;
  ui_main_buttons.setTargetRect({ 0, disp_height - main_btns_height, disp_width, main_btns_height });
  ui_main_buttons.setClientRect({ 0, disp_height, disp_width, 0 });

  // ui_sequence_timeline.setTargetRect({ disp_width, disp_height - main_btns_height, 0, main_btns_height });
  ui_sequence_timeline.setTargetRect({ 0, disp_height, disp_width, 0 });
  ui_sequence_timeline.setClientRect(ui_sequence_timeline.getTargetRect());

  ui_raw_wave.setTargetRect({ 0, disp_height - (main_btns_height >> 1), disp_width, 0 });

  ui_sub_buttons.setTargetRect({ 0, disp_height - main_btns_height - sub_btns_height, disp_width, sub_btns_height });
  ui_sub_buttons.setClientRect({ 0, disp_height, disp_width, 0 });

  ui_left_icon_container.setTargetRect({ 0, 0, disp_width, header_height });
  ui_left_icon_container.setClientRect({ 0, 0, 0, header_height });

  ui_right_icon_container.setTargetRect({ 0, 0, disp_width, header_height });
  ui_right_icon_container.setClientRect({ 0, 0, disp_width, 0 });

  ui_popup_notify.setClientRect({ disp_width >> 1, disp_height >> 1, 0, 0 });
  ui_popup_notify.setTargetRect(ui_popup_notify.getClientRect());
  ui_popup_notify.setHideRect(ui_popup_notify.getClientRect());

  ui_popup_qr.setClientRect({ disp_width>>1, disp_height>>1, 0, 0 });
  ui_popup_qr.setTargetRect(ui_popup_qr.getClientRect());

  int x = disp_width;// - header_height;
  ui_volume_info.setTargetRect({ x, 0, header_height , header_height });
  ui_volume_info.setClientRect({ x, 0, header_height , 0 });
  ui_battery_info.setTargetRect({ x, 0, battery_icon_width, header_height });
  ui_battery_info.setClientRect({ x, 0, battery_icon_width, 0 });

  rect_t r = { x, 0, 0, header_height };
  ui_wifi_sta_info.setTargetRect(r);
  ui_wifi_sta_info.setClientRect(r);
  ui_wifi_ap_info.setTargetRect(r);
  ui_wifi_ap_info.setClientRect(r);
  ui_icon_auto_play.setTargetRect(r);
  ui_icon_auto_play.setClientRect(r);
  ui_midiport_info.setTargetRect(r);
  ui_midiport_info.setClientRect(r);

  r.x = 0;
  ui_playkey_info.setClientRect(r);
  ui_song_modified.setClientRect(r);
  ui_song_modified.setTargetRect(r);
  ui_filename.setClientRect(r);

  r.w = 48;
  ui_playkey_info.setTargetRect(r);
  ui_filename.setTargetRect({ 0, 0, disp_width, header_height });

  ui_playkey_select.setClientRect({ 0, 160-80,  0, 160 });
  ui_playkey_select.setTargetRect(ui_playkey_select.getClientRect());
  ui_playkey_select.setHideRect(ui_playkey_select.getClientRect());
  ui_playkey_select.setShowRect({ 0, 160-80, 60, 160 });


  ui_chord_part_container.setTargetRect({ 0, header_height, disp_width, disp_height - (header_height + main_btns_height + sub_btns_height) });

  auto side_width = 0;//24;
  auto arp_width = disp_width - side_width;
  auto arp_height = disp_height - (header_height + main_btns_height + sub_btns_height);
  {
    int32_t w = arp_width / 3;
    int32_t h = arp_height >> 1;
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      ui_partinfo_list[i].setPartIndex(i);
      int x = (i % 3) * w;
      int y = (i / 3) * h;// + header_height;
      ui_partinfo_list[i].setClientRect( { x, y, w, 0 } );
      ui_partinfo_list[i].setTargetRect( { x, y, w, h } );
      ui_chord_part_container.addChild(&ui_partinfo_list[i]);
    }
  }
  ui_chord_part_container.addChild(&ui_arpeggio_edit);
  ui_chord_part_container.shrink_to_fit();

  ui_left_icon_container.addChild(&ui_playkey_info);
  ui_left_icon_container.addChild(&ui_song_modified);
  ui_left_icon_container.addChild(&ui_filename);
  ui_left_icon_container.shrink_to_fit();

  ui_right_icon_container.addChild(&ui_icon_auto_play);  
  ui_right_icon_container.addChild(&ui_midiport_info);
  ui_right_icon_container.addChild(&ui_wifi_ap_info);
  ui_right_icon_container.addChild(&ui_wifi_sta_info);
  ui_right_icon_container.addChild(&ui_battery_info);
  ui_right_icon_container.addChild(&ui_volume_info);
  ui_right_icon_container.shrink_to_fit();

  ui_background.addChild(&ui_chord_part_container);
  ui_background.addChild(&ui_main_buttons);
  ui_background.addChild(&ui_sub_buttons);
  ui_background.addChild(&ui_sequence_timeline);
  ui_background.addChild(&ui_raw_wave);
  ui_background.addChild(&ui_left_icon_container);
  ui_background.addChild(&ui_right_icon_container);
  for (auto &ui : ui_menu_bodys) {
    ui_background.addChild(&ui);
  }
  ui_background.addChild(&ui_menu_header);
  ui_background.addChild(&ui_playkey_select);
  ui_background.addChild(&ui_popup_notify);
  ui_background.addChild(&ui_popup_qr);
  ui_background.shrink_to_fit();

//-------------------------------------------------------------------------

  for (int i = 0; i < disp_buf_count; ++i) {
    disp_buf[i].setPsram(false);
    disp_buf[i].setColorDepth(color_depth);
    // disp_buf[i].setBuffer(_draw_buffer[i], disp_height, disp_buf_width, depth);
    // disp_buf[i].setRotation(0);
    // disp_buf[i].createSprite(disp_buf_width, disp_height);
    disp_buf[i].setFont(&fonts::efontJA_16_b);
  }
  _draw_param.resetClipRect();
  uint32_t msec = M5.millis();
  _draw_param.prev_msec = msec;
  _draw_param.current_msec = msec;
// ティアリング対策 (LCDリフレッシュレートを下げる)
/*
  static constexpr const uint8_t B1_DIVA = 0x01;
  static constexpr const uint8_t B1_RTNA = 0x1C;
  static constexpr const uint8_t B5_VFP = 0x1E;
  static constexpr const uint8_t B5_VBP = 0x02;
  _gfx->startWrite();
  _gfx->writeCommand(0xB1);
  _gfx->writeData(B1_DIVA);
  _gfx->writeData(B1_RTNA);
  _gfx->writeCommand(0xB5);
  _gfx->writeData(B5_VFP);
  _gfx->writeData(B5_VBP);
  _gfx->writeData(0x16);
  _gfx->writeData(0x06);
  _gfx->endWrite();
//*/
_gfx->startWrite();
_gfx->writeCommand(0xB5);
_gfx->writeData(8);
_gfx->writeData(8);
_gfx->writeData(0x0A);
_gfx->writeData(0x14);
_gfx->endWrite();

  // _gfx->startWrite();
}

bool gui_t::update(void)
{
  // if (!app_core.getGuiEnabled()) {
  //   gfx->endWrite();
  //   do {
  //     M5.delay(1);
  //   } while (!app_core.getGuiEnabled());
  //   gfx->startWrite();
  // }

#if defined (M5UNIFIED_PC_BUILD)
  M5.delay(8);
#else

#if defined ( DEBUG_GUI )
  ++_fps_counter;
  uint32_t sec = M5.millis() / 1000;
  static uint32_t prev_sec = 0;
  if (prev_sec != sec) {
    prev_sec = sec;
    M5_LOGV("fps:%d  delay:%d", _fps_counter, _delay_counter);
    _fps_counter = 0;
    _delay_counter = 0;
    M5.delay(1);
  }
#endif

//*/
#endif

  auto param = &_draw_param;
  param->resetInvalidatedRect();
  uint32_t msec = M5.millis();
  uint32_t prev_msec = param->current_msec;
  int diff = msec - prev_msec;
  param->current_msec = msec;
  if (diff <= 0) { return false; }
  if (diff > 255) { diff = 255; }
  param->prev_msec = prev_msec;
  param->smooth_step = diff;
  // param->flg_update = false;
  ui_background.update(param, 0, 0);
  if (!param->hasInvalidated) {
  // if (!param->flg_update) {
// static uint32_t prev_btnbitmask;
// uint32_t btnbitmask = system_registry->internal_input.getButtonBitmask();
// if (prev_btnbitmask != btnbitmask) {
//   prev_btnbitmask = btnbitmask;
//   _gfx->setCursor(0, 0);
//   _gfx->printf("%08x", (unsigned int)btnbitmask);
// }
    return false;
  }

  bool suspended = false;

#if defined ( DEBUG_GUI )
  uint32_t backcolor = rand();
#else
  uint32_t backcolor = 0;// param->color_set->background;
#endif

  for (int32_t i = 0; i < draw_param_t::max_clip_rect; ++i)
  {
    rect_t update_rect = param->invalidated_rect[i];
    auto canvas = &disp_buf[disp_buf_idx];
    if (!update_rect.empty()) {
      if (suspended) {
        system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
        suspended = false;
      }
      // clip_rect.x -= x;


      // canvas->deleteSprite();
//       if (nullptr == canvas->createSprite(update_rect.w, update_rect.h)) {
// M5_LOGE("createSprite failed");
// continue;
//       }
// M5_LOGE("rect x:%d y:%d w:%d h:%d", update_rect.x, update_rect.y, update_rect.w, update_rect.h);
      assert(update_rect.w * update_rect.h < max_disp_buf_pixels && "rect size overflow !!");

      if (update_rect.w * update_rect.h >= max_disp_buf_pixels) {
        M5_LOGE("rect size overflow !!  w:%d h:%d", update_rect.w, update_rect.h);
        abort();
      }
      canvas->setBuffer(_draw_buffer[disp_buf_idx], update_rect.w, update_rect.h, color_depth);

      if (++disp_buf_idx == disp_buf_count) { disp_buf_idx = 0; }

      canvas->clearClipRect();
      canvas->fillScreen(backcolor);
      // canvas->drawRect(0,0,canvas->width(),canvas->height(),backcolor);
      auto clip_rect = update_rect;
      clip_rect.x = 0;
      clip_rect.y = 0;
      ui_background.draw(param, canvas, -update_rect.x, -update_rect.y, &clip_rect);
// canvas->fillScreen(rand());
    }
    if (!suspended) {
      system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
      suspended = true;
    }

    if (!update_rect.empty()) {
#if !defined (M5UNIFIED_PC_BUILD)
      if (_gfx->dmaBusy()) {
        M5.delay(1);
        ++_delay_counter;
        do { taskYIELD(); } while (_gfx->dmaBusy());
      }

      if (update_rect.w < 128) {
        uint8_t l = _gfx->getScanLine() - update_rect.x;
        // ティアリング対策ウェイト
        if (l < update_rect.w) {
          M5.delay(1 + ((update_rect.w - l) * 9 >> 7));
        }
      }
#endif
      canvas->pushSprite(_gfx, update_rect.x, update_rect.y);
    }
  }
  if (suspended) {
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
  }
  return true;
}

void gui_t::procTouchControl(const m5::touch_detail_t& td)
{
// この関数はSPIタスクではなくI2Cタスクから実行される
// 従ってSPIに影響のある処理は行わないこと
  auto ui = ui_background.searchUI(td.base_x, td.base_y);
  if (ui != nullptr) {
    // M5_LOGV("touch ui:%08x x:%d y:%d p:%d", ui, td.x, td.y, td.isPressed());
    ui->touchControl(&_draw_param, td);

    static uint32_t part_change_mask;
    if (td.wasPressed())
    { // 画面に触れたタイミングでパートパネル上に指があればenableチェンジができるようにする
      part_change_mask = 0;
      // 押した場所がボタンの位置にあるか否か
      for (int i = 0; i < def::app::max_chord_part; ++i)
      { // タッチ操作によるパート有効・無効が機能するようにしておく
        if (ui == &ui_partinfo_list[i])
        {
          part_change_mask = ~0u;
        }
      }
    }

    ui = ui_background.searchUI(td.x, td.y);
    for (int i = 0; i < def::app::max_chord_part; ++i)
    { // タッチ操作によるパート有効・無効の切替
      if (ui != &ui_partinfo_list[i]) { continue; }

      if (td.wasHold())
      { // 長押しで編集モードに移行
        system_registry->operator_command.addQueue( { def::command::part_edit, i+1 } );
      } else
      {
        auto partinfo = &(system_registry->current_slot->chord_part[i].part_info);
        bool next_enabled = partinfo->getEnabled();
        if (td.wasClicked()
          || td.isFlicking()
          || (td.wasPressed() && !next_enabled))
        {
          if ((part_change_mask & (1 << i))) {
            part_change_mask &= ~(1 << i);
            partinfo->setEnabled(!next_enabled);
          }
        }
      }
      break;
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

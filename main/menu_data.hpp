// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_MENU_DATA_HPP
#define KANPLAY_MENU_DATA_HPP

#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// メニューカテゴリ
// enum class menu_category_t : uint8_t {
//   none = 0,
//   system,
//   part,
//   slot,
//   file,
// };

enum class menu_item_type_t : uint8_t {
  // 未定義 (エラー扱いとする)
  mt_unknown,

  // 子階層をもつメニュー
  mt_tree,

  // 標準的な選択肢一覧タイプ (enum)
  mt_normal,

  // QRコードを表示する
  show_qrcode,

  // プログレスバーを表示する
  show_progress,

  // 数値入力を受け付ける
  input_number,

/*
  // メニューの値を変更する
  int_value,

  // メニューの値を変更する (bool)
  bool_switch,
*/
};

struct value_rule_t {
  int minimum;
  int maximum;
  int step;
};

struct draw_param_t;
struct rect_t;

struct menu_item_t {
  constexpr menu_item_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : _title { title }, _menu_id { menu_id }, _category { cate }, _level(level) {}
  def::menu_category_t getCategory(void) const { return _category; }
  uint16_t getMenuID(void) const { return _menu_id; }
  uint8_t getLevel(void) const { return _level; }
  virtual menu_item_type_t getType(void) const = 0;//{ return menu_item_type_t::mt_unknown; }
  virtual const char* getTitleText(void) const { return _title.get(); }
  virtual const char* getValueText(void) const { return "..."; }

// ↓ task_operator側から操作される関数群
  // メニューに入る
  virtual bool enter(void) const;
  // メニューを閉じる
  virtual bool exit(void) const;
  // 数値を入力する
  virtual bool inputNumber(uint8_t number) const { return false; }
  // 上下入力
  virtual bool inputUpDown(int updown) const { return false; };
  // メニューを実行する
  virtual bool execute(void) const { return true; }
// ↑ task_operator側から操作される関数群

// ↓ gui側から操作される関数群
  // 選択肢の文字列を取得する
  virtual const char* getSelectorText(size_t index) const { return nullptr; }
  // 選択肢の数を取得する
  virtual size_t getSelectorCount(void) const { return 0; }
  // 表示文字列を取得する
  virtual std::string getString(void) const { return ""; }

  // 設定可能な最小値を取得する
  virtual int getMinValue(void) const { return 1; }
  // 設定可能な最大値を取得する
  virtual int getMaxValue(void) const { return getMinValue() + getSelectorCount() - 1; }
  // 現在設定されている値を取得する
  virtual int getValue(void) const { return 0; }
  // GUI上でカーソルが示している値を取得する
  virtual int getSelectingValue(void) const { return 1; }
// ↑ gui側から操作される関数群


protected:
  // 選択中の値を変更する (GUI上の見せ方のみで、実際の値は変更しない)
  virtual bool setSelectingValue(int value) const { return (getMinValue() <= value) && (value <= getMaxValue()); }
  // 値を反映する
  virtual bool setValue(int value) const { return (getMinValue() <= value) && (value <= getMaxValue()); }

  localize_text_t _title;
  uint16_t _menu_id;    // メニューID (メニュー一覧の中での自身の通し番号)
  def::menu_category_t _category;
  uint8_t _level;   // 自身が所属する階層の深さ

};

typedef const menu_item_t* menu_item_ptr;
typedef const menu_item_ptr* menu_item_ptr_array;

struct menu_control_t {
  void openMenu(def::menu_category_t category);
  bool enter(void);
  bool exit(void);
  bool inputNumber(uint8_t number);
  bool inputUpDown(int updown);

  menu_item_ptr getItemByLevel(uint8_t level) { return _menu_array[level ? system_registry.menu_status.getSelectIndex(level-1):0]; }

  int getChildrenMenuIDList(std::vector<uint16_t>* index_list, uint16_t parent_index);

  menu_item_ptr getItemByMenuID(uint16_t menu_id) { return _menu_array[menu_id]; }

protected:
  menu_item_ptr_array _menu_array;
  def::menu_category_t _category;
};

extern menu_control_t menu_control;

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

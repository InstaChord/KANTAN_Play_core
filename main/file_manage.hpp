// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_FILE_MANAGE_HPP
#define KANPLAY_FILE_MANAGE_HPP

/*
 - ファイル入出力
*/

#include <vector>
#include <string>

#include "common_define.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
struct file_info_string_t {
  std::string filename;
  size_t filesize;
};
struct file_info_t {
  char* filename;
  size_t filesize;
};

// SDカードなどのファイル入出力を管理するクラス
class storage_base_t
{
protected:
  bool _is_begin = false;

public:
  virtual ~storage_base_t() = default;
  storage_base_t() = default;

  bool isBegin(void) const { return _is_begin; }

  virtual bool beginStorage(void) { return true; }

  virtual void endStorage(void) {}

  virtual int getFileSize(const char* path) { return -1; }

  // 指定されたファイルをメモリに読み込む
  virtual int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) { return 0; }

  // メモリのデータを指定されたファイルに書き込む
  virtual int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) { return 0; }

  // ファイルのリストを取得する
  virtual int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") { return 0; }

  // ディレクトリを作成する
  virtual bool makeDirectory(const char* path) { return false; }

  // ファイルを削除する
  virtual int removeFile(const char* path) { return 0; }

  // ファイルをリネームする
  virtual int renameFile(const char* path, const char* newpath) { return 0; }
};

class storage_sd_t : public storage_base_t
{
public:
  bool beginStorage(void) override;
  void endStorage(void) override;
  int getFileSize(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") override;
  bool makeDirectory(const char* path) override;
  int removeFile(const char* path) override;
  int renameFile(const char* path, const char* newpath) override;
};
extern storage_sd_t storage_sd;


class storage_littlefs_t : public storage_base_t
{
public:
  bool beginStorage(void) override;
  void endStorage(void) override;
  int getFileSize(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") override;
  bool makeDirectory(const char* path) override;
  int removeFile(const char* path) override;
  int renameFile(const char* path, const char* newpath) override;
};
extern storage_littlefs_t storage_littlefs;


class storage_incbin_t : public storage_base_t
{
public:
  bool beginStorage(void) override;
  void endStorage(void) override;
  int getFileSize(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") override;
  bool makeDirectory(const char* path) override;
  int removeFile(const char* path) override;
  int renameFile(const char* path, const char* newpath) override;
};
extern storage_incbin_t storage_incbin;


// 特定のディレクトリ内部のファイル情報を保持・管理するクラス
class dir_manage_t
{
public:
  dir_manage_t (storage_base_t* storage, const char *path) : _storage { storage }, _path { path } {}
  storage_base_t* getStorage(void) { return _storage; }
  bool isEmpty(void) const { return _file_list_count == 0; }
  size_t getCount(void) const { return _file_list_count; }
  const file_info_t* getInfo(size_t index) const { return &_file_list[index]; }
  bool update(void);
  int search(const char* filename) const;
  std::string getFullPath(size_t index);
  std::string makeFullPath(const char* filename) const;
protected:
  storage_base_t* _storage = nullptr;
  const char* _path;

  char* _all_filenames = nullptr;
  file_info_t* _file_list = nullptr;
  size_t _file_list_count = 0;
};


// ファイルの読み込み・保存に使用するメモリ情報を保持する構造体
struct memory_info_t {
  memory_info_t(uint8_t index) : index { index } {}

  std::string filename;      // ファイル名 (フルパスではない)
  uint8_t* data = nullptr;
  size_t size = 0;
  def::app::data_type_t dir_type;

  void release(void);

  const uint8_t index; 
};

class file_manage_t
{
  static constexpr const size_t max_memory_info = 4;
  memory_info_t _memory_info[max_memory_info] = { {0}, {1}, {2}, {3} };
  uint8_t _load_queue_index = 0;
  std::string _latest_file_name;
  std::string _display_file_name;
  def::app::data_type_t _latest_data_type;
  int _latest_file_index = -1;
public:
  void setLatestFileInfo(def::app::data_type_t data_type, const char* filename);

  // 現在使用中のファイル名 (拡張子あり)
  const std::string& getLatestFileName(void) const { return _latest_file_name; }

  // 現在使用中のファイルのデータタイプ
  def::app::data_type_t getLatestDataType(void) const { return _latest_data_type; }

  // 現在使用中のファイルのインデックス番号
  int getLatestFileIndex(void) const { return _latest_file_index; }

  // GUI表示用、現在使用中のファイル名 (拡張子なし)
  const std::string& getDisplayFileName(void) const { return _display_file_name; }

  // ファイルアクセス用のメモリを確保しポインタを取得する
  memory_info_t* createMemoryInfo(size_t length);

  // 既にあるファイルアクセス用のメモリをインデクス番号を指定して取得する
  memory_info_t* getMemoryInfoByIndex(size_t index) { return &_memory_info[index]; }

  dir_manage_t* getDirManage(def::app::data_type_t dir_type);

  const file_info_t* getFileInfo(def::app::data_type_t dir_type, size_t index);

  // ファイルリストを更新する
  bool updateFileList(def::app::data_type_t dir_type);

  // ファイルを読み込む。
  memory_info_t* loadFile(def::app::data_type_t dir_type, size_t file_index);

  // ファイルを読み込む。
  memory_info_t* loadFile(def::app::data_type_t dir_type, const char* file_name);

  // ファイルを保存する。保存が終わったら system_registry経由でcommandを発行する
  bool saveFile(def::app::data_type_t dir_type, size_t memory_index);

  // ファイル保存用のメモリバッファを取得する
  // memory_info_t* getSaveMemory(size_t length);
};

extern file_manage_t file_manage;

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

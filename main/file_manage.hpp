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
struct file_info_t {
  std::string filename;
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

  virtual bool fileExists(const char* path) { return false; }

  // 指定されたファイルをメモリに読み込む
  virtual int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index = -1) { return 0; }

  // メモリのデータを指定されたファイルに書き込む
  virtual int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) { return 0; }

  // ファイルのリストを取得する
  virtual int getFileList(const char* path, std::vector<file_info_t>& list) { return 0; }

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
  bool fileExists(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index = -1) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(const char* path, std::vector<file_info_t>& list) override;
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
  bool fileExists(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index = -1) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(const char* path, std::vector<file_info_t>& list) override;
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
  bool fileExists(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index = -1) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(const char* path, std::vector<file_info_t>& list) override;
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
  bool isEmpty(void) const { return _files.empty(); }
  size_t getCount(void) { return _files.size(); }
  const file_info_t* getInfo(size_t index) { return &_files[index]; }
  bool update(void);
  int search(const char* filename);
  std::string getFullPath(size_t index);
  std::string makeFullPath(const char* filename) const;
protected:
  storage_base_t* _storage;
  std::string _path;
  std::vector<file_info_t> _files;
};


// ファイルの読み込み・保存に使用するメモリ情報を保持する構造体
struct memory_info_t {
  memory_info_t(uint8_t index) : index { index } {}
  const uint8_t index; 

  std::string filename;      // ファイル名 (フルパスではない)
  uint8_t* data = nullptr;
  size_t size = 0;
  def::app::data_type_t dir_type;

  void release(void);
};

class file_manage_t
{
  static constexpr const size_t max_memory_info = 4;
  memory_info_t _memory_info[max_memory_info] = { {0}, {1}, {2}, {3} };
  uint8_t _load_queue_index = 0;
  std::string _latest_file_name;
  std::string _display_file_name;
  def::app::data_type_t _latest_data_type;
public:
  void setLatestFileInfo(const char* filename, def::app::data_type_t data_type);

  // 現在使用中のファイル名 (拡張子あり)
  const std::string& getLatestFileName(void) const { return _latest_file_name; }

  // 現在使用中のファイルのデータタイプ
  def::app::data_type_t getLatestDataType(void) const { return _latest_data_type; }

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
  const memory_info_t* loadFile(def::app::data_type_t dir_type, size_t file_index);

  // ファイルを保存する。保存が終わったら system_registry経由でcommandを発行する
  bool saveFile(def::app::data_type_t dir_type, size_t memory_index);

  // ファイル保存用のメモリバッファを取得する
  // memory_info_t* getSaveMemory(size_t length);
};

extern file_manage_t file_manage;


#if 0
class file_manage_t
{
public:
  enum lockstate_t {
    LOCKSTATE_UNLOCK,
    LOCKSTATE_LOCK,
    LOCKSTATE_PRESET,
  };

  struct fileinfo_t {
    std::string filename;
    lockstate_t lockstate;
    int makeFullPath(char* dst, size_t len) const;
  };

  size_t getFileCount(void) { return _config_files.size(); };
  fileinfo_t* getFileInfo(size_t index) { return &_config_files[index]; };


  // bool hasRequest(void) const { return _has_request; }
  bool copyPresetToLocked(void);
  bool updateFileList(void);
  int saveToFile(const char* path, const uint8_t* data, size_t length);

  // 引数のファイルインデックスをファイル数以内に収まるように調整する関数
  uint16_t adjustFileIndex(uint16_t index) const;
  uint8_t getFileIndex(void) const { return _loaded_fileindex; }

  int loadFromFile(const char* path);
  int loadFromFileList(uint_fast16_t index);

  bool isEmpty(void) const { return _config_files.empty(); }

  const fileinfo_t* getPartsetFileInfo(void) const { return _config_files.empty() ? nullptr : &_config_files[_loaded_fileindex]; }

  bool getLoadFileBuffer(uint8_t** buffer, size_t* size) const { *buffer = _file_load_buffer; *size = _file_load_size; return _file_load_buffer != nullptr; };
  bool getFileName(uint8_t** buffer, size_t* size) const { *buffer = _file_load_buffer; *size = _file_load_size; return _file_load_buffer != nullptr; };

  uint8_t* getSaveFileBuffer(int index, size_t length);

  // task_operator_t からセットされるファイル保存要求
  bool setSaveRequest(const std::string& filename, int index, size_t length);

  // task_spi_t から実行されるファイル保存処理
  void procSaveRequest(void);

  bool changeLock(void);

private:
  void clearFileBuffer(void);
  bool sdBegin(void);
  void sdEnd(void);
  bool copydir(const char* src, const char* dst);
  bool updateFileList_inner(const char* path, lockstate_t lockstate);

  std::vector<fileinfo_t> _config_files;
  uint16_t _loaded_fileindex;
  uint8_t* _file_load_buffer = nullptr;
  size_t _file_load_size = 0;

  struct save_info_t {
    uint8_t* buffer = nullptr;
    size_t buffer_size = 0;
    std::string filename;
    size_t data_size = 0;
    bool is_request = false;
  };

  static constexpr const size_t max_save_buffers = 2;
  save_info_t _save_info[max_save_buffers];

  enum sd_status_t {
    SD_STATUS_NONE,
    SD_STATUS_MOUNTED,
    SD_STATUS_ERROR,
  };
  sd_status_t _sd_status = SD_STATUS_NONE;
};

extern file_manage_t file_manage;
#endif

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

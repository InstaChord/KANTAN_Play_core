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

// Long-running sequential SD reads use an opaque backend handle so callers
// do not need to know whether the build uses SdFat, Arduino SD or stdio.
// Each read/seek takes the shared SPI lock only for that operation.
struct storage_read_stream_t {
  void* handle = nullptr;
  size_t size = 0;
  size_t position = 0;
  uint32_t media_generation = 0;

  bool isOpen(void) const { return handle != nullptr; }
};

enum class sd_media_state_t : uint8_t {
  uninitialized,
  mounted,
  ejecting,
  safe_to_remove,
  missing,
  error,
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

  // 既存ファイルの末尾へデータを追記する。大きな録音PCMを一括で複製せず、
  // WAVとしてストリーム保存する用途に使う。
  virtual int appendFromMemoryToFile(const char* path, const uint8_t* data, size_t length) { return -1; }

  // Rewrite a small fixed region without truncating the file. Streaming WAV
  // capture uses this to finalize RIFF/data sizes after its PCM payload.
  virtual int patchFromMemoryToFile(const char* path, size_t offset, const uint8_t* data, size_t length) { return -1; }

  // ファイルのリストを取得する
  virtual int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") { return 0; }

  // 指定ディレクトリ直下のフォルダ一覧を取得する
  virtual int getDirectoryList(std::vector<file_info_string_t>& list, const char* path) { return 0; }

  // ディレクトリを作成する
  virtual bool makeDirectory(const char* path) { return false; }

  // ファイルを削除する
  virtual bool removeFile(const char* path) { return false; }

  // ファイルをリネームする
  virtual bool renameFile(const char* path, const char* newpath) { return false; }
};

class storage_sd_t : public storage_base_t
{
public:
  bool beginStorage(void) override;
  // Explicit user recovery path. Unlike beginStorage(), this may leave the
  // SAFE TO REMOVE / MISSING / ERROR states and probe newly inserted media.
  bool loadStorage(void);
  bool ejectStorage(void);
  void endStorage(void) override;
  sd_media_state_t mediaState(void) const { return _media_state; }
  uint32_t mediaGeneration(void) const { return _media_generation; }
  bool isMounted(void) const { return _media_state == sd_media_state_t::mounted && _is_begin; }
  bool takeMediaError(void);
  int getFileSize(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int appendFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int patchFromMemoryToFile(const char* path, size_t offset, const uint8_t* data, size_t length) override;
  int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") override;
  int getDirectoryList(std::vector<file_info_string_t>& list, const char* path) override;
  bool makeDirectory(const char* path) override;
  bool removeFile(const char* path) override;
  bool renameFile(const char* path, const char* newpath) override;
  bool openReadStream(const char* path, storage_read_stream_t* stream);
  int readStream(storage_read_stream_t* stream, uint8_t* dst, size_t length);
  bool seekStream(storage_read_stream_t* stream, size_t position);
  void closeReadStream(storage_read_stream_t* stream);
private:
  bool mountStorage(void);
  void unmountStorage(void);
  void noteMediaError(void);

  volatile sd_media_state_t _media_state = sd_media_state_t::uninitialized;
  volatile uint32_t _media_generation = 1;
  volatile bool _media_error_pending = false;
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
  bool removeFile(const char* path) override;
  bool renameFile(const char* path, const char* newpath) override;
};
extern storage_littlefs_t storage_littlefs;


struct incbin_file_t {
  const char* filename;
  const uint8_t* data;
  size_t size;
};

class storage_incbin_t : public storage_base_t
{
public:
  storage_incbin_t(const incbin_file_t* files, size_t file_count)
    : _files{files}, _file_count{file_count} {}

  bool beginStorage(void) override;
  void endStorage(void) override;
  int getFileSize(const char* path) override;
  int loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length) override;
  int saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length) override;
  int getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix = "") override;

  const incbin_file_t* getFiles(void) const { return _files; }
  size_t getFileCount(void) const { return _file_count; }
private:
  const incbin_file_t* _files;
  size_t _file_count;
};


// 特定のディレクトリ内部のファイル情報を保持・管理するクラス
class dir_manage_t
{
public:
  dir_manage_t (storage_base_t* storage, const char *path) : _storage { storage }, _path { path } {}
  storage_base_t* getStorage(void) { return _storage; }
  bool isEmpty(void) const { return _file_list_count == 0; }
  size_t getCount(void) const { return _file_list_count; }
  const file_info_t* getInfo(size_t index) const { return &_file_list[index]; }
  bool updateFileList(void);
  void invalidateFileList(void);
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

  // ファイルを削除する
  bool removeFile(def::app::data_type_t dir_type, const char* filename);

  // ファイルをリネームする (同一 dir_type 内でのみ)
  bool renameFile(def::app::data_type_t dir_type, const char* old_name, const char* new_name);
  void invalidateStorage(storage_base_t* storage);
};

extern file_manage_t file_manage;

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif

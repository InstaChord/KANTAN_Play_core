// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.


#include <M5Unified.h>

#if __has_include(<LittleFS.h>)
 #include <LittleFS.h>
#endif

#if __has_include(<SdFat.h>)
 #define DISABLE_FS_H_WARNING
 #include <SdFat.h>
 SdFat SD;
#elif __has_include(<SD.h>)
// #include <SD.h>
#else
 #include <filesystem>
 #include <stdio.h>
#endif


#include "file_manage.hpp"

#include "system_registry.hpp"

#include <set>


// ファイルインポートマクロ

// m1mac用のインポートマクロ
#if defined (__APPLE__) && defined (__MACH__) && defined (__arm64__)

#define IMPORT_FILE(section, filename, symbol) \
static constexpr const char* filename_##symbol = filename; \
extern const uint8_t symbol[], sizeof_##symbol[]; \
asm (\
  ".section __DATA,__data \n"\
  ".balign 4\n_"\
  #symbol ":\n"\
  ".incbin \"incbin/preset/" filename "\"\n"\
  ".global _sizeof_" #symbol "\n"\
  ".set _sizeof_" #symbol ", . - _" #symbol "\n"\
  ".global _" #symbol "\n"\
  ".balign 4\n"\
  ".text \n")

#else

#define IMPORT_FILE(section, filename, symbol) \
static constexpr const char* filename_##symbol = filename; \
extern const uint8_t symbol[], sizeof_##symbol[]; \
asm (\
  ".section " #section "\n"\
  ".balign 4\n"\
  ".global " #symbol "\n"\
  #symbol ":\n"\
  ".incbin \"incbin/preset/" filename "\"\n"\
  ".global sizeof_" #symbol "\n"\
  ".set sizeof_" #symbol ", . - " #symbol "\n"\
  ".balign 4\n"\
  ".section \".text\"\n")
#endif

IMPORT_FILE(.rodata, "Simple_Guitar.json"       ,  preset_00 );
IMPORT_FILE(.rodata, "Simple_Guitarx2.json"     ,  preset_01 );
IMPORT_FILE(.rodata, "Simple_Piano.json"        ,  preset_02 );
IMPORT_FILE(.rodata, "Pop01_16beatSw.json"      ,  preset_11 );
IMPORT_FILE(.rodata, "Pop02_BlueW.json"         ,  preset_12 );
IMPORT_FILE(.rodata, "Pop03_Yobikomi.json"      ,  preset_13 );
IMPORT_FILE(.rodata, "Pop04_ObLaDi.json"        ,  preset_14 );
IMPORT_FILE(.rodata, "Pop05_Ageha.json"         ,  preset_15 );
IMPORT_FILE(.rodata, "Pop06_Sofmap.json"        ,  preset_16 );
IMPORT_FILE(.rodata, "Pop07_DonQui.json"        ,  preset_17 );
IMPORT_FILE(.rodata, "Pop08_Aozora.json"        ,  preset_18 );
IMPORT_FILE(.rodata, "Pop09_Standard.json"      ,  preset_19 );
IMPORT_FILE(.rodata, "Pop10_Kaiju.json"         ,  preset_1A );
IMPORT_FILE(.rodata, "Rock01_Iine.json"         ,  preset_21 );
IMPORT_FILE(.rodata, "Rock02_GtrKids.json"      ,  preset_22 );
IMPORT_FILE(.rodata, "Rock03_LovePhntm.json"    ,  preset_23 );
IMPORT_FILE(.rodata, "Rock04_Standard.json"     ,  preset_24 );
IMPORT_FILE(.rodata, "Rock05_Train.json"        ,  preset_25 );
IMPORT_FILE(.rodata, "Rock06_Ketobase.json"     ,  preset_26 );
IMPORT_FILE(.rodata, "Rock07_8Beat.json"        ,  preset_27 );
IMPORT_FILE(.rodata, "Folk01_Tombo.json"        ,  preset_31 );
IMPORT_FILE(.rodata, "Folk02_Stand.json"        ,  preset_32 );
IMPORT_FILE(.rodata, "Ballade01_Lovin.json"     ,  preset_41 );
IMPORT_FILE(.rodata, "Ballade02_Shonen.json"    ,  preset_42 );
IMPORT_FILE(.rodata, "Ballade03_Yell.json"      ,  preset_43 );
IMPORT_FILE(.rodata, "Ballade04_Hakujitsu.json" ,  preset_44 );
IMPORT_FILE(.rodata, "Ballade05_Lovex3.json"    ,  preset_45 );
IMPORT_FILE(.rodata, "Dance01_GetWild.json"     ,  preset_51 );
IMPORT_FILE(.rodata, "Dance02_USA.json"         ,  preset_52 );
IMPORT_FILE(.rodata, "Dance03_Euro.json"        ,  preset_53 );
IMPORT_FILE(.rodata, "Dance04_Virtual.json"     ,  preset_54 );
IMPORT_FILE(.rodata, "Punk01_Linda.json"        ,  preset_61 );
IMPORT_FILE(.rodata, "Punk02_Natsu.json"        ,  preset_62 );
IMPORT_FILE(.rodata, "Game01_Star.json"         ,  preset_71 );
IMPORT_FILE(.rodata, "Game02_Chrono.json"       ,  preset_72 );
IMPORT_FILE(.rodata, "Samba_1.json"             ,  preset_81 );
IMPORT_FILE(.rodata, "Ska_1.json"               ,  preset_82 );
IMPORT_FILE(.rodata, "Orchestra_1.json"         ,  preset_83 );
IMPORT_FILE(.rodata, "Orchestra_2.json"         ,  preset_84 );

namespace kanplay_ns {

struct incbin_file_t {
  const char* filename;
  const uint8_t* data;
  size_t size;
};
static const incbin_file_t incbin_files[] = {
  { filename_preset_00, preset_00, (size_t)sizeof_preset_00 },
  { filename_preset_01, preset_01, (size_t)sizeof_preset_01 },
  { filename_preset_02, preset_02, (size_t)sizeof_preset_02 },
  { filename_preset_11, preset_11, (size_t)sizeof_preset_11 },
  { filename_preset_12, preset_12, (size_t)sizeof_preset_12 },
  { filename_preset_13, preset_13, (size_t)sizeof_preset_13 },
  { filename_preset_14, preset_14, (size_t)sizeof_preset_14 },
  { filename_preset_15, preset_15, (size_t)sizeof_preset_15 },
  { filename_preset_16, preset_16, (size_t)sizeof_preset_16 },
  { filename_preset_17, preset_17, (size_t)sizeof_preset_17 },
  { filename_preset_18, preset_18, (size_t)sizeof_preset_18 },
  { filename_preset_19, preset_19, (size_t)sizeof_preset_19 },
  { filename_preset_1A, preset_1A, (size_t)sizeof_preset_1A },
  { filename_preset_21, preset_21, (size_t)sizeof_preset_21 },
  { filename_preset_22, preset_22, (size_t)sizeof_preset_22 },
  { filename_preset_23, preset_23, (size_t)sizeof_preset_23 },
  { filename_preset_24, preset_24, (size_t)sizeof_preset_24 },
  { filename_preset_25, preset_25, (size_t)sizeof_preset_25 },
  { filename_preset_26, preset_26, (size_t)sizeof_preset_26 },
  { filename_preset_27, preset_27, (size_t)sizeof_preset_27 },
  { filename_preset_31, preset_31, (size_t)sizeof_preset_31 },
  { filename_preset_32, preset_32, (size_t)sizeof_preset_32 },
  { filename_preset_41, preset_41, (size_t)sizeof_preset_41 },
  { filename_preset_42, preset_42, (size_t)sizeof_preset_42 },
  { filename_preset_43, preset_43, (size_t)sizeof_preset_43 },
  { filename_preset_44, preset_44, (size_t)sizeof_preset_44 },
  { filename_preset_45, preset_45, (size_t)sizeof_preset_45 },
  { filename_preset_51, preset_51, (size_t)sizeof_preset_51 },
  { filename_preset_52, preset_52, (size_t)sizeof_preset_52 },
  { filename_preset_53, preset_53, (size_t)sizeof_preset_53 },
  { filename_preset_54, preset_54, (size_t)sizeof_preset_54 },
  { filename_preset_61, preset_61, (size_t)sizeof_preset_61 },
  { filename_preset_62, preset_62, (size_t)sizeof_preset_62 },
  { filename_preset_71, preset_71, (size_t)sizeof_preset_71 },
  { filename_preset_72, preset_72, (size_t)sizeof_preset_72 },
  { filename_preset_81, preset_81, (size_t)sizeof_preset_81 },
  { filename_preset_82, preset_82, (size_t)sizeof_preset_82 },
  { filename_preset_83, preset_83, (size_t)sizeof_preset_83 },
  { filename_preset_84, preset_84, (size_t)sizeof_preset_84 },
};
  
  
// extern instance
storage_sd_t storage_sd;
storage_littlefs_t storage_littlefs;
storage_incbin_t storage_incbin;
file_manage_t file_manage;

static dir_manage_t dir_manage[def::app::data_type_t::data_type_max] =
{ { nullptr          ,                     "" }, // data_unknown
  { &storage_sd      , def::app::data_path[0] }, // data_song_users
  { &storage_sd      , def::app::data_path[1] }, // data_song_extra
  { &storage_incbin  , def::app::data_path[2] }, // data_song_preset
  { &storage_littlefs, def::app::data_path[3] }, // data_setting
  { &storage_littlefs, def::app::data_path[4] }, // data_resume
  { &storage_littlefs, def::app::data_path[5] }, // data_mapping
};

static std::string trimExtension(const std::string& filename)
{
  auto pos = filename.find_last_of('.');
  if (pos != std::string::npos) {
    return filename.substr(0, pos);
  }
  return filename;
}

void memory_info_t::release(void) {
  filename.clear();
  size = 0;
  if (data) {
    m5gfx::heap_free(data);
    data = nullptr;
  }
}

//-------------------------------------------------------------------------

bool storage_sd_t::beginStorage(void)
{
  if (_is_begin) { return true; }

#if __has_include(<SdFat.h>)
  SdSpiConfig spiConfig(M5.getPin(m5::pin_name_t::sd_spi_cs), SHARED_SPI, SD_SCK_MHZ(25), &SPI);
  _is_begin = SD.begin(spiConfig);
#elif __has_include(<SD.h>)
  _is_begin = SD.begin(M5.getPin(m5::pin_name_t::sd_spi_cs));
#else
  _is_begin = true;
#endif

  if (!_is_begin) {
    system_registry->popup_notify.setPopup(false, def::notify_type_t::NOTIFY_STORAGE_OPEN);
    M5_LOGE("SD card mount failed");
  } else {
    for (auto& dm : dir_manage) {
      if (dm.getStorage() == this) {
        makeDirectory(dm.makeFullPath("").c_str());
      }
    }
  }

  return _is_begin;
}
void storage_sd_t::endStorage(void)
{
  if (!_is_begin) { return; }
  _is_begin = false;
#if __has_include(<SdFat.h>)
  SD.end();
#elif __has_include(<SD.h>)
  SD.end();
#else
#endif
}

bool storage_sd_t::fileExists(const char* path)
{
#if __has_include(<SdFat.h>)
  return SD.exists(path);
#elif __has_include (<SD.h>)
  return SD.exists(path);
#else
  if (path[0] == '/') { ++path; }
  return std::filesystem::exists(path);
#endif
}

int storage_sd_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index)
{
  if (!_is_begin) { return -1; }

  int len = -1;
#if __has_include(<SdFat.h>)
  auto file = SD.open(path, O_READ);
  if (!file) { return len; }
  len = file.dataLength();
  if (len) {
    if (len > max_length) {
      len = max_length;
    }
    len = file.read(dst, len);
  }
  file.close();

#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_READ);
  if (!file) { return len; }
  len = file.size();
  if (len) {
    if (len > max_length) {
      len = max_length;
    }
    len = file.read(dst, len);
  }
  file.close();

#else
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "r");
  if (!FP) { return len; }
  fseek(FP, 0, SEEK_END);
  len = ftell(FP);
  if (len) {
    if (len > max_length) {
      len = max_length;
    }
    fseek(FP, 0, SEEK_SET);
    len = fread(dst, 1, len, FP);
  }
  fclose(FP);
#endif

  return len;
}

int storage_sd_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  if (!_is_begin) { return -1; }

#if __has_include(<SdFat.h>)
  auto file = SD.open(path, O_CREAT | O_WRITE | O_TRUNC);
  if (!file) {
    return -1;
  }
  length = file.write(data, length);

  auto now = time(nullptr);
  auto tm = gmtime(&now);
  file.timestamp(T_CREATE|T_WRITE, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

  file.close();

#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_WRITE);
  if (!file) {
    return -1;
  }
  length = file.write(data, length);
  file.close();

#else
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "w");
  if (!FP) { return -1; }
  length = fwrite(data, 1, length, FP);
  fclose(FP);
#endif

  return length;
}

int storage_sd_t::getFileList(const char* path, std::vector<file_info_t>& list)
{
  if (!_is_begin) { return -1; }

  file_info_t info;

#if __has_include(<SdFat.h>)
  auto dir = SD.open(path);
  if (false == dir) { return -1; }
  if (dir.isDirectory()) {
    FsFile file;
    while (false != (file = dir.openNextFile())) {
      char filename[256];
      file.getName(filename, sizeof(filename));
      // mac系不可視ファイル無視
      if (memcmp(filename, "._", 2) == 0) { continue; }
      info.filename = filename;
      info.filesize = file.fileSize();
      list.push_back( info );
//M5_LOGV("file %s %d", filename, info.filesize);
    }
    dir.close();
  } else {
    info.filename = "";
    info.filesize = dir.fileSize();
    list.push_back( info );
  }
#elif __has_include (<SD.h>)
  auto dir = SD.open(path);
  if (false == dir) { return -1; }
  if (dir.isDirectory()) {
    fs::File file;
    while (false != (file = dir.openNextFile())) {
      info.filename = file.name();
      info.filesize = file.size();
      list.push_back( info );
    }
    dir.close();
  }
#else

if (path[0] == '/') { ++path; }
bool result = std::filesystem::exists(path);
M5_LOGE("exists:%d , %s", result, path);
  if (result) {
    result = std::filesystem::is_directory(path);
    M5_LOGE("is_dir:%d , %s", result, path);
    if (result) {
      for (const auto& file : std::filesystem::directory_iterator(path)) {
        // list.push_back({file.path().filename().u8string().c_str(), file.file_size()});
        size_t size = file.file_size();
        list.push_back({file.path().filename().string(), size});
      }
    } else {
      size_t size = std::filesystem::file_size(path);
      list.push_back({ "", size });
M5_LOGE("file size:%d , %s", size, path);
    }
  }
#endif

  if (!list.empty()) {
    std::sort(list.begin(), list.end(), [](const file_info_t& a, const file_info_t& b) { return a.filename < b.filename; });
  }

  return list.size();
}

bool storage_sd_t::makeDirectory(const char* path)
{
#if __has_include (<SdFat.h>)
  return SD.mkdir(path);
#elif __has_include (<SD.h>)
  return SD.mkdir(path);
#else
  if (path[0] == '/') { ++path; }
  return std::filesystem::create_directory(path);
#endif
}

int storage_sd_t::removeFile(const char* path)
{
  return 0;
}

int storage_sd_t::renameFile(const char* path, const char* newpath)
{
  return 0;
}

//-------------------------------------------------------------------------

bool storage_littlefs_t::beginStorage(void)
{
  if (_is_begin) { return true; }

#if __has_include(<LittleFS.h>)
  _is_begin = LittleFS.begin(true);
#else
  _is_begin = true;
#endif

  if (!_is_begin) {
    system_registry->popup_notify.setPopup(false, def::notify_type_t::NOTIFY_STORAGE_OPEN);
    M5_LOGE("LittleFS mount failed");
  }

  return _is_begin;
}
void storage_littlefs_t::endStorage(void)
{
  if (!_is_begin) { return; }
  _is_begin = false;
#if __has_include(<LittleFS.h>)
  LittleFS.end();
#else
#endif
}

bool storage_littlefs_t::fileExists(const char* path)
{
#if __has_include(<LittleFS.h>)
  return LittleFS.exists(path);
#else
  if (path[0] == '/') { ++path; }
  return std::filesystem::exists(path);
#endif
}

int storage_littlefs_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index)
{
  if (!_is_begin) { return -1; }

  int len = -1;
#if __has_include(<LittleFS.h>)
bool exists = LittleFS.exists(path);
M5_LOGE("LittleFS open:%s exists:%d", path, exists);
  if (!LittleFS.exists(path)) { return len; }
  auto file = LittleFS.open(path);
  if (!file) { return len; }
  len = file.size();
  if (len) {
    if (len > max_length) {
      len = max_length;
    }
    len = file.read(dst, len);
  }
  file.close();
#else
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "r");
  if (!FP) { return len; }
  fseek(FP, 0, SEEK_END);
  len = ftell(FP);
  if (len) {
    if (len > max_length) {
      len = max_length;
    }
    fseek(FP, 0, SEEK_SET);
    len = fread(dst, 1, len, FP);
  }
  fclose(FP);
#endif

  return len;
}

int storage_littlefs_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  if (!_is_begin) { return -1; }

  const char* tmpfile = "/.tmpsave.tmp";
  size_t writelen = 0;
#if __has_include(<LittleFS.h>)
  // 一旦テンポラリファイルに保存する。
  auto file = LittleFS.open(tmpfile, FILE_WRITE, true);
  if (!file) {
    return -1;
  }
  writelen = file.write(data, length);
  file.close();
  taskYIELD();
  // 元のファイルを削除してリネームする。
  LittleFS.remove(path);
  LittleFS.rename(tmpfile, path);

#else
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "w");
  if (!FP) { return -1; }
  writelen = fwrite(data, 1, length, FP);
  fclose(FP);
#endif

  return writelen;
}

int storage_littlefs_t::getFileList(const char* path, std::vector<file_info_t>& list)
{
  if (!_is_begin) { return -1; }

  file_info_t info;

#if __has_include(<LittleFS.h>)
  if (LittleFS.exists(path)) {
    auto dir = LittleFS.open(path);
    if (false == dir) { return -1; }
    if (dir.isDirectory()) {
      dir.close();
    } else {
      info.filename = "";
      info.filesize = dir.size();
      list.push_back( info );
    }
  }
#else

if (path[0] == '/') { ++path; }
bool result = std::filesystem::exists(path);
M5_LOGD("exists:%d , %s", result, path);
  if (result) {
    result = std::filesystem::is_directory(path);
    M5_LOGD("is_dir:%d , %s", result, path);
    if (result == false) {
      size_t size = std::filesystem::file_size(path);
      list.push_back({ "", size });
M5_LOGD("file size:%d , %s", size, path);
    }
  }
#endif

  return list.size();
}

bool storage_littlefs_t::makeDirectory(const char* path)
{
  return false;
}

int storage_littlefs_t::removeFile(const char* path)
{
  return 0;
}

int storage_littlefs_t::renameFile(const char* path, const char* newpath)
{
  return 0;
}

//-------------------------------------------------------------------------

bool storage_incbin_t::beginStorage(void)
{
  return true;
}

void storage_incbin_t::endStorage(void)
{
}

bool storage_incbin_t::fileExists(const char* path)
{
  return true;
}

int storage_incbin_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length, int dir_index)
{
  if (dir_index < 0 || dir_index >= (int)sizeof(incbin_files) / sizeof(incbin_files[0])) {
    return -1;
  }
  auto size = incbin_files[dir_index].size;
  if (size > max_length) {
    size = max_length;
  }
  memcpy(dst, incbin_files[dir_index].data, size);
  return size;
}

int storage_incbin_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  return -1;
}

int storage_incbin_t::getFileList(const char* path, std::vector<file_info_t>& list)
{
  file_info_t info;
  for (auto file : incbin_files) {
    info.filename = file.filename;
    info.filesize = file.size;
    list.push_back( info );
  }
  return list.size();
}

bool storage_incbin_t::makeDirectory(const char* path)
{
  return false;
}

int storage_incbin_t::removeFile(const char* path)
{
  return 0;
}

int storage_incbin_t::renameFile(const char* path, const char* newpath)
{
  return 0;
}

//-------------------------------------------------------------------------

bool dir_manage_t::update(void)
{
  std::vector<file_info_t> list;
  int result = _storage->getFileList(_path.c_str(), list);

  _files = list;
  return result >= 0;
}

int dir_manage_t::search(const char* filename)
{
  for (size_t i = 0; i < _files.size(); ++i) {
    if (_files[i].filename == filename) {
      return i;
    }
  }
  return -1;
}

std::string dir_manage_t::getFullPath(size_t index)
{
  if (index >= _files.size()) { return ""; }
  return _path + _files[index].filename;
}

std::string dir_manage_t::makeFullPath(const char* filename) const
{
  return _path + filename;
}

//-------------------------------------------------------------------------
dir_manage_t* file_manage_t::getDirManage(def::app::data_type_t dir_type)
{
  assert(dir_type < def::app::data_type_t::data_type_max && "dir_type is out of range");
  return &dir_manage[dir_type];
}

const file_info_t* file_manage_t::getFileInfo(def::app::data_type_t dir_type, size_t index)
{
  return dir_manage[dir_type].getInfo(index);
}

bool file_manage_t::updateFileList(def::app::data_type_t dir_type)
{
  M5_LOGV("updateFileList");
  auto dir = getDirManage(dir_type);
  if (dir == nullptr) { return false; }
  if (!dir->update()) {
    auto st = dir->getStorage();
    st->endStorage();
    st->beginStorage();
    return !dir->update();
  }
  return true;
}

void file_manage_t::setLatestFileInfo(const char* filename, def::app::data_type_t data_type)
{
  if (data_type == def::app::data_type_t::data_song_preset
   || data_type == def::app::data_type_t::data_song_users
   || data_type == def::app::data_type_t::data_song_extra) {
    _latest_data_type = data_type;
    _latest_file_name = (filename != nullptr) ? filename : "";
    _display_file_name = trimExtension(_latest_file_name);
  }
}

memory_info_t* file_manage_t::createMemoryInfo(size_t length)
{
  if ((int32_t)length < 0) { return nullptr; }
  auto new_queue_index = (_load_queue_index + 1) % max_memory_info;

  _memory_info[new_queue_index].release();

  auto tmp = (uint8_t*)m5gfx::heap_alloc_psram(length);
  if (tmp == nullptr) {
    M5_LOGE("getLoadMemory:heap_alloc_psram failed. size:%d", length);
    return nullptr;
  }
  _memory_info[new_queue_index].data = tmp;
  _memory_info[new_queue_index].size = length;
  _load_queue_index = new_queue_index;
  return &_memory_info[new_queue_index];
}

const memory_info_t* file_manage_t::loadFile(def::app::data_type_t dir_type, size_t index)
{
  auto dir = getDirManage(dir_type);
  if (index >= dir->getCount()) {
    updateFileList(dir_type);
  }
  if ((int16_t)index < 0) { // マイナス指定されている場合は末尾側として扱えるようにindexを加算する
    index = ((int16_t)index) + dir->getCount();
  }
  if (index < dir->getCount())
  {
    auto info = dir->getInfo(index);
    M5_LOGD("file_manage_t::loadFile : index:%d %s", index, info->filename.c_str());
    if (info->filesize == 0) { return nullptr; }
    auto memory = createMemoryInfo(info->filesize);
    if (memory != nullptr) {
      memory->dir_type = dir_type;
      auto fullpath = dir->getFullPath(index);
      memory->filename = fullpath;
      auto storage = dir->getStorage();
      if (0 <= storage->loadFromFileToMemory(fullpath.c_str(), memory->data, memory->size, index)) {
        setLatestFileInfo(info->filename.c_str(), dir_type);
        return memory;
      }
    }
  }
  return nullptr;
}

bool file_manage_t::saveFile(def::app::data_type_t dir_type, size_t memory_index)
{
  auto mem = getMemoryInfoByIndex(memory_index);
  auto dir = getDirManage(dir_type);
  auto st = dir->getStorage();

  if (mem == nullptr) {
    return false;
  }
  if (mem->data[0] != '{') {
    return false;
  }

  auto path = dir->makeFullPath(mem->filename.c_str());
  auto result = st->saveFromMemoryToFile(path.c_str(), mem->data, mem->size);
  if (result != mem->size) {
    st->endStorage();
    st->beginStorage();
    result = st->saveFromMemoryToFile(path.c_str(), mem->data, mem->size);
  }
// M5_LOGV("save:%s size:%d result:%d", path.c_str(), mem->size, result);

  dir->update();

  if (result != mem->size) {
    return false;
  }
  if (!mem->filename.empty()) {
    setLatestFileInfo(mem->filename.c_str(), dir_type);
  }

  return true;
}


//-------------------------------------------------------------------------
}; // namespace kanplay_ns

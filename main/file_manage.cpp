// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.


#include <M5Unified.h>

// ファイルシステム実装の切り替え
// LittleFSとSDで別々のフラグを持つ。
//   LittleFS: 既定でESP-IDF VFSを使用 (Arduino/ESP-IDF両方で動作)
//   SD:       Arduino環境ではSdFat、SdFatが無い場合のみVFSを使用
//             (ArduinoのSPIバス管理とesp_vfs_fat_sdspi_mountの競合回避のため)
//
// 強制切り替えはplatformio.iniのbuild_flagsで指定:
//   -DKANPLAY_USE_VFS_LITTLEFS=0  : LittleFSでArduino LittleFSを使用
//   -DKANPLAY_USE_VFS_LITTLEFS=1  : LittleFSでVFSを強制使用 (既定)
//   -DKANPLAY_USE_VFS_SD=0        : SDでSdFat/Arduino SDを使用 (既定: SdFat有)
//   -DKANPLAY_USE_VFS_SD=1        : SDでVFSを強制使用 (要: SPIバスが競合しない環境)

#ifndef KANPLAY_USE_VFS_LITTLEFS
  #if defined(M5UNIFIED_PC_BUILD) || defined(ARDUINO)
    #define KANPLAY_USE_VFS_LITTLEFS 0
  #else
    #define KANPLAY_USE_VFS_LITTLEFS 1
  #endif
#endif

#ifndef KANPLAY_USE_VFS_SD
  #if defined(M5UNIFIED_PC_BUILD) || defined(ARDUINO)
    #define KANPLAY_USE_VFS_SD 0
  #else
    #define KANPLAY_USE_VFS_SD 1
  #endif
#endif

#if defined(M5UNIFIED_PC_BUILD)
  #include <filesystem>
  #include <stdio.h>
#else

  #include <fcntl.h>
  #include <unistd.h>

  #if KANPLAY_USE_VFS_LITTLEFS
    #include <esp_littlefs.h>
  #elif __has_include(<LittleFS.h>)
    #include <LittleFS.h>
  #endif
  
  #if KANPLAY_USE_VFS_SD
    #include <esp_vfs_fat.h>
    #include <sdmmc_cmd.h>
    #include <driver/sdspi_host.h>
  #elif __has_include(<SdFat.h>)
    #define DISABLE_FS_H_WARNING
    #include <SdFat.h>
    SdFat SD;
  // #elif __has_include(<SD.h>)
  // #include <SD.h>     // LDF 対策(コメントアウトしないとSD.hがLDFによって有効化してしまう)
  #endif

  #if (KANPLAY_USE_VFS_LITTLEFS || KANPLAY_USE_VFS_SD)
    #include <sys/stat.h>
    #include <dirent.h>
    #include <utime.h>
  #endif

#endif

#include "file_manage.hpp"

#include "system_registry.hpp"

#include <set>

// ファイルインポートマクロ

// m1mac用のインポートマクロ
#if defined (__APPLE__) && defined (__MACH__) && defined (__arm64__)

#define IMPORT_FILE(section, path, filename, symbol) \
static constexpr const char* filename_##symbol = filename; \
extern const uint8_t symbol[], sizeof_##symbol[]; \
asm (\
  ".section __DATA,__data \n"\
  ".balign 4\n_"\
  #symbol ":\n"\
  ".incbin \"incbin/preset/" path filename "\"\n"\
  ".global _sizeof_" #symbol "\n"\
  ".set _sizeof_" #symbol ", . - _" #symbol "\n"\
  ".global _" #symbol "\n"\
  ".balign 4\n"\
  ".text \n")

#else

#define IMPORT_FILE(section, path, filename, symbol) \
static constexpr const char* filename_##symbol = filename; \
extern const uint8_t symbol[], sizeof_##symbol[]; \
asm (\
  ".section " #section "\n"\
  ".balign 4\n"\
  ".global " #symbol "\n"\
  #symbol ":\n"\
  ".incbin \"incbin/preset/" path filename "\"\n"\
  ".global sizeof_" #symbol "\n"\
  ".set sizeof_" #symbol ", . - " #symbol "\n"\
  ".balign 4\n"\
  ".section \".text\"\n")
#endif

// ソングプリセット: Pop
#define ENTRY(idx, filename) IMPORT_FILE(.rodata, "song_genre/pop/", filename, sg_pop_##idx);
#include "../incbin/preset/song_genre/pop/_list.inl"
#undef ENTRY

// ソングプリセット: Rock
#define ENTRY(idx, filename) IMPORT_FILE(.rodata, "song_genre/rock/", filename, sg_rock_##idx);
#include "../incbin/preset/song_genre/rock/_list.inl"
#undef ENTRY

// ソングプリセット: Dance / Funk / R&B / Jazz / Latin / Acoustic / Ballad / Specialty
// (各カテゴリにデータが追加されたら IMPORT_FILE ブロックをここに追加する)

// ソングプリセット: 楽曲データ
#define ENTRY(idx, filename) IMPORT_FILE(.rodata, "song_song/", filename, ss_##idx);
#include "../incbin/preset/song_song/_list.inl"
#undef ENTRY

// コード進行プリセット
#define ENTRY(idx, filename) IMPORT_FILE(.rodata, "progression/", filename, prog_##idx);
#include "../incbin/preset/progression/_list.inl"
#undef ENTRY

// アルペジオプリセット: guitar
#define ENTRY(idx, filename) IMPORT_FILE(.rodata, "arp_guitar/", filename, arp_guitar_##idx);
#include "../incbin/preset/arp_guitar/_list.inl"
#undef ENTRY

// ソングリセット用ブランクデータ
IMPORT_FILE(.rodata, "", "Blank.json", song_blank);


namespace kanplay_ns {


void spi_lock(void);
void spi_unlock(void);

#if KANPLAY_USE_VFS_SD
static constexpr const char* SD_MOUNT_POINT = "/sdcard";

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  static constexpr spi_host_device_t SD_SPI_HOST = SPI2_HOST;
#elif defined(CONFIG_IDF_TARGET_ESP32)
  static constexpr spi_host_device_t SD_SPI_HOST = SPI3_HOST;
#else
  static constexpr spi_host_device_t SD_SPI_HOST = SPI2_HOST;
#endif

static sdmmc_card_t* _sd_card = nullptr;

static std::string sd_vfs_path(const char* path) {
  return std::string(SD_MOUNT_POINT) + path;
}
#endif // KANPLAY_USE_VFS_SD

#if !defined(M5UNIFIED_PC_BUILD)
static constexpr const char* LITTLEFS_MOUNT_POINT = "/littlefs";

static std::string littlefs_vfs_path(const char* path) {
  return std::string(LITTLEFS_MOUNT_POINT) + path;
}
#endif

#if KANPLAY_USE_VFS_SD || KANPLAY_USE_VFS_LITTLEFS
// --- VFS共通ヘルパー ---
// VFS用の汎用ファイルサイズ取得
static int vfs_getFileSize(const char* fullpath) {
  struct stat st;
  if (stat(fullpath, &st) == 0) {
    return (int)st.st_size;
  }
  return -1;
}

// VFS用の汎用ファイル読み込み
static int vfs_loadFromFile(const char* fullpath, uint8_t* dst, size_t max_length) {
  auto fp = fopen(fullpath, "rb");
  if (!fp) return -1;
  fseek(fp, 0, SEEK_END);
  int len = ftell(fp);
  if (len > 0) {
    if ((size_t)len > max_length) { len = max_length; }
    fseek(fp, 0, SEEK_SET);
    len = fread(dst, 1, len, fp);
  }
  fclose(fp);
  return len;
}

// VFS用の汎用ファイル書き込み
static int vfs_saveToFile(const char* fullpath, const uint8_t* data, size_t length) {
  auto fp = fopen(fullpath, "wb");
  if (!fp) return -1;
  int result = fwrite(data, 1, length, fp);
  fclose(fp);
  return result;
}

// VFS用の汎用ファイルリスト取得
static int vfs_getFileList(std::vector<file_info_string_t>& list, const char* fullpath, const char* suffix) {
  const size_t len_suffix = suffix ? strlen(suffix) : 0;
  struct stat path_stat;
  if (stat(fullpath, &path_stat) != 0) return -1;

  if (S_ISDIR(path_stat.st_mode)) {
    auto dir = opendir(fullpath);
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      const char* name = entry->d_name;
      // mac系不可視ファイル無視
      if (memcmp(name, "._", 2) == 0) continue;
      auto len_name = strlen(name);
      if (len_name < len_suffix) continue;
      if (len_suffix > 0 && strcmp(&name[len_name - len_suffix], suffix) != 0) continue;

      // ファイルサイズ取得
      std::string filepath = std::string(fullpath) + "/" + name;
      struct stat entry_stat;
      size_t filesize = 0;
      if (stat(filepath.c_str(), &entry_stat) == 0) {
        if (S_ISDIR(entry_stat.st_mode)) continue; // ディレクトリはスキップ
        filesize = entry_stat.st_size;
      }
      file_info_string_t info;
      info.filename = name;
      info.filesize = filesize;
      list.push_back(info);
    }
    closedir(dir);
  } else {
    file_info_string_t info;
    info.filename = "";
    info.filesize = path_stat.st_size;
    list.push_back(info);
  }
  return list.size();
}
#endif // KANPLAY_USE_VFS_SD || KANPLAY_USE_VFS_LITTLEFS


// ソングプリセット: Pop
#define ENTRY(idx, filename) { filename_sg_pop_##idx, sg_pop_##idx, (size_t)sizeof_sg_pop_##idx },
static const incbin_file_t incbin_song_genre_pop[] = {
#include "../incbin/preset/song_genre/pop/_list.inl"
};
#undef ENTRY

// ソングプリセット: Rock
#define ENTRY(idx, filename) { filename_sg_rock_##idx, sg_rock_##idx, (size_t)sizeof_sg_rock_##idx },
static const incbin_file_t incbin_song_genre_rock[] = {
#include "../incbin/preset/song_genre/rock/_list.inl"
};
#undef ENTRY

// ソングプリセット: 各カテゴリのデータ配列
// (データが追加されたら各カテゴリの ENTRY ブロックをここに追加する)

// ソングプリセット: 楽曲データ
#define ENTRY(idx, filename) { filename_ss_##idx, ss_##idx, (size_t)sizeof_ss_##idx },
static const incbin_file_t incbin_song_song[] = {
#include "../incbin/preset/song_song/_list.inl"
};
#undef ENTRY

// コード進行プリセット
#define ENTRY(idx, filename) { filename_prog_##idx, prog_##idx, (size_t)sizeof_prog_##idx },
static const incbin_file_t incbin_progression[] = {
#include "../incbin/preset/progression/_list.inl"
};
#undef ENTRY

// アルペジオプリセット: guitar
#define ENTRY(idx, filename) { filename_arp_guitar_##idx, arp_guitar_##idx, (size_t)sizeof_arp_guitar_##idx },
static const incbin_file_t incbin_arp_guitar[] = {
#include "../incbin/preset/arp_guitar/_list.inl"
};
#undef ENTRY

// ソングリセット用ブランクデータ（単一エントリ）
static const incbin_file_t incbin_song_blank[] = {
  { filename_song_blank, song_blank, (size_t)sizeof_song_blank },
};


// extern instance
storage_sd_t storage_sd;
storage_littlefs_t storage_littlefs;
file_manage_t file_manage;

static storage_incbin_t storage_incbin_progression { incbin_progression, sizeof(incbin_progression) / sizeof(incbin_progression[0]) };
static storage_incbin_t storage_incbin_song_song   { incbin_song_song,   sizeof(incbin_song_song)   / sizeof(incbin_song_song[0]) };
static storage_incbin_t storage_incbin_song_blank  { incbin_song_blank,  sizeof(incbin_song_blank)  / sizeof(incbin_song_blank[0]) };
static storage_incbin_t storage_incbin_arp_guitar  { incbin_arp_guitar,  sizeof(incbin_arp_guitar)  / sizeof(incbin_arp_guitar[0]) };
static storage_incbin_t storage_incbin_arp_empty   { nullptr, 0 };

// ジャンルプリセット カテゴリ別
// データが存在するカテゴリ: 配列サイズをそのまま使用
// データが空のカテゴリ: nullptr, 0 で初期化 (空配列はC++で未定義動作になるため)
#define MAKE_INCBIN_STORAGE(arr) { arr, sizeof(arr) / sizeof(arr[0]) }
static storage_incbin_t storage_incbin_sg_pop      MAKE_INCBIN_STORAGE(incbin_song_genre_pop);
static storage_incbin_t storage_incbin_sg_rock     MAKE_INCBIN_STORAGE(incbin_song_genre_rock);
// data_song_preset_genre は起動時デフォルトとして参照される旧互換枠。実体はPopへ向ける。
static storage_incbin_t storage_incbin_song_genre  MAKE_INCBIN_STORAGE(incbin_song_genre_pop);
static storage_incbin_t storage_incbin_sg_dance    { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_funk     { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_rnb      { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_jazz     { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_latin    { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_acoustic { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_ballad   { nullptr, 0 }; // データ未追加
static storage_incbin_t storage_incbin_sg_specialty{ nullptr, 0 }; // データ未追加
#undef MAKE_INCBIN_STORAGE

using dt = def::app::data_type_t;
static dir_manage_t dir_manage[dt::data_type_max] =
{ { nullptr                    ,                             "" }, // data_unknown
  { &storage_littlefs          , def::app::data_path[dt::data_system             ] }, // data_system
  { &storage_sd                , def::app::data_path[dt::data_song_users         ] }, // data_song_users
  { &storage_sd                , def::app::data_path[dt::data_song_extra         ] }, // data_song_extra
  { &storage_sd                , def::app::data_path[dt::data_arpeggio_users     ] }, // data_arpeggio_user
  { &storage_sd                , def::app::data_path[dt::data_progression_users  ] }, // data_progression_users
  { &storage_incbin_progression, def::app::data_path[dt::data_progression_preset ] }, // data_progression_preset
  { &storage_incbin_song_genre , def::app::data_path[dt::data_song_preset_genre  ] }, // data_song_preset_genre
  { &storage_incbin_song_song  , def::app::data_path[dt::data_song_preset_song   ] }, // data_song_preset_song
  { &storage_incbin_song_blank , def::app::data_path[dt::data_song_blank         ] }, // data_song_blank (リセット用)
  { &storage_incbin_arp_empty  , def::app::data_path[dt::data_arpeggio_drum      ] }, // data_arpeggio_drum   (データ未追加)
  { &storage_incbin_arp_empty  , def::app::data_path[dt::data_arpeggio_bass      ] }, // data_arpeggio_bass   (データ未追加)
  { &storage_incbin_arp_guitar , def::app::data_path[dt::data_arpeggio_guitar    ] }, // data_arpeggio_guitar (データ未追加)
  { &storage_incbin_arp_empty  , def::app::data_path[dt::data_arpeggio_piano     ] }, // data_arpeggio_piano  (データ未追加)
  { &storage_incbin_arp_empty  , def::app::data_path[dt::data_arpeggio_other     ] }, // data_arpeggio_other  (データ未追加)
  // ジャンルプリセット カテゴリ別
  { &storage_incbin_sg_pop      , "" }, // data_song_preset_genre_pop
  { &storage_incbin_sg_rock     , "" }, // data_song_preset_genre_rock
  { &storage_incbin_sg_dance    , "" }, // data_song_preset_genre_dance
  { &storage_incbin_sg_funk     , "" }, // data_song_preset_genre_funk
  { &storage_incbin_sg_rnb      , "" }, // data_song_preset_genre_rnb
  { &storage_incbin_sg_jazz     , "" }, // data_song_preset_genre_jazz
  { &storage_incbin_sg_latin    , "" }, // data_song_preset_genre_latin
  { &storage_incbin_sg_acoustic , "" }, // data_song_preset_genre_acoustic
  { &storage_incbin_sg_ballad   , "" }, // data_song_preset_genre_ballad
  { &storage_incbin_sg_specialty, "" }, // data_song_preset_genre_specialty
  { &storage_incbin_song_genre  , "" }, // data_song_preset_genre_old
};

static std::string trimExtension(const std::string& filename)
{
  // "." が2つ以上ある場合、最初の "." より前は分類記号なので非表示にする
  // 例: "G1-01.Pop_Basic1.json" → "Pop_Basic1.json" → "Pop_Basic1"
  std::string result = filename;
  auto dot1 = result.find('.');
  if (dot1 != std::string::npos && result.find('.', dot1 + 1) != std::string::npos) {
    result = result.substr(dot1 + 1);
  }

  auto pos = result.find_last_of('.');
  if (pos != std::string::npos) {
    result = result.substr(0, pos);
  }
  return result;
}

void memory_info_t::release(void) {
  filename.clear();
  size = 0;
  if (data) {
    m5gfx::heap_free(data);
    data = nullptr;
  }
}

//=========================================================================
// storage_sd_t
//=========================================================================

bool storage_sd_t::beginStorage(void)
{
  if (isMounted()) { return true; }
  // An explicit Eject, a missing card, or an I/O failure must never be
  // defeated by an unrelated file picker or autosave. Only Load SD Card may
  // probe the medium again.
  if (_media_state != sd_media_state_t::uninitialized) { return false; }
  return mountStorage();
}

bool storage_sd_t::mountStorage(void)
{
  _is_begin = false;

  spi_lock();

#if defined(M5UNIFIED_PC_BUILD)
  _is_begin = true;
#elif KANPLAY_USE_VFS_SD
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_SPI_HOST;
  host.max_freq_khz = 25000;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = (gpio_num_t)M5.getPin(m5::pin_name_t::sd_spi_cs);
  slot_config.host_id = SD_SPI_HOST;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &_sd_card);
  _is_begin = (ret == ESP_OK);
  if (_is_begin) {
    M5_LOGI("SD card mounted at %s", SD_MOUNT_POINT);
  } else {
    M5_LOGE("SD mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
  }

#elif __has_include(<SdFat.h>)
  SdSpiConfig spiConfig(M5.getPin(m5::pin_name_t::sd_spi_cs), SHARED_SPI, SD_SCK_MHZ(25), &SPI);
  _is_begin = SD.begin(spiConfig);
#elif __has_include(<SD.h>)
  _is_begin = SD.begin(M5.getPin(m5::pin_name_t::sd_spi_cs));
#endif

  spi_unlock();

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

  _media_state = _is_begin ? sd_media_state_t::mounted : sd_media_state_t::missing;
  if (!_is_begin) { _media_generation = _media_generation + 1; }
  return _is_begin;
}

bool storage_sd_t::loadStorage(void)
{
  unmountStorage();
  _media_generation = _media_generation + 1;
  _media_error_pending = false;
  _media_state = sd_media_state_t::uninitialized;
  return mountStorage();
}

bool storage_sd_t::ejectStorage(void)
{
  if (!isMounted()) { return false; }
  _media_state = sd_media_state_t::ejecting;
  unmountStorage();
  _media_generation = _media_generation + 1;
  _media_error_pending = false;
  _media_state = sd_media_state_t::safe_to_remove;
  return true;
}

void storage_sd_t::endStorage(void)
{
  unmountStorage();
  _media_generation = _media_generation + 1;
  _media_state = sd_media_state_t::uninitialized;
}

void storage_sd_t::unmountStorage(void)
{
  if (!_is_begin) { return; }

  spi_lock();

  _is_begin = false;
#if defined(M5UNIFIED_PC_BUILD)
#elif KANPLAY_USE_VFS_SD
  if (_sd_card) {
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, _sd_card);
    _sd_card = nullptr;
  }
#elif __has_include(<SdFat.h>)
  SD.end();
#elif __has_include(<SD.h>)
  SD.end();
#endif
  spi_unlock();
}

void storage_sd_t::noteMediaError(void)
{
  if (_media_state != sd_media_state_t::mounted) { return; }
  // Keep the backend-mounted bit until the explicit Load path tears the
  // driver down. isMounted() still rejects every new operation immediately.
  _media_state = sd_media_state_t::error;
  _media_generation = _media_generation + 1;
  _media_error_pending = true;
}

bool storage_sd_t::takeMediaError(void)
{
  const bool pending = _media_error_pending;
  _media_error_pending = false;
  return pending;
}

int storage_sd_t::getFileSize(const char* path)
{
  if (!isMounted() || !path) { return -1; }
  int res = -1;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  if (std::filesystem::exists(path)) {
    res = std::filesystem::file_size(path);
  }
#elif KANPLAY_USE_VFS_SD
  res = vfs_getFileSize(sd_vfs_path(path).c_str());

#elif __has_include(<SdFat.h>)
  if (SD.exists(path)) {
    auto file = SD.open(path, O_READ);
    if (file) {
      res = file.fileSize();
      file.close();
    }
  }
#elif __has_include (<SD.h>)
  if (SD.exists(path)) {
    auto file = SD.open(path, FILE_READ);
    if (file) {
      res = file.size();
      file.close();
    }
  }
#endif
  spi_unlock();
  return res;
}

int storage_sd_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length)
{
  if (!isMounted() || !path || !dst) { return -1; }

  spi_lock();

  int len = -1;
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "r");
M5_LOGV("sd:loadFromFileToMemory : %s  open:%d\n", path, FP != nullptr);
  if (FP) {
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
  }
#elif KANPLAY_USE_VFS_SD
  len = vfs_loadFromFile(sd_vfs_path(path).c_str(), dst, max_length);

#elif __has_include(<SdFat.h>)
  auto file = SD.open(path, O_READ);
  if (file != false) {
    len = file.dataLength();
    if (len) {
      if (len > max_length) {
        len = max_length;
      }
      len = file.read(dst, len);
    }
    file.close();
  }

#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_READ);
  if (file) {
    len = file.size();
    if (len) {
      if (len > max_length) {
        len = max_length;
      }
      len = file.read(dst, len);
    }
    file.close();
  }

#endif
  spi_unlock();
  return len;
}

bool storage_sd_t::openReadStream(const char* path, storage_read_stream_t* stream)
{
  if (!isMounted() || !path || !stream) { return false; }
  closeReadStream(stream);
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  FILE* file = fopen(path, "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    stream->size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    stream->handle = file;
  }
#elif KANPLAY_USE_VFS_SD
  const auto fullpath = sd_vfs_path(path);
  FILE* file = fopen(fullpath.c_str(), "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    stream->size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    stream->handle = file;
  }
#elif __has_include(<SdFat.h>)
  FsFile* file = new FsFile(SD.open(path, O_READ));
  if (file && *file) {
    stream->size = (size_t)file->fileSize();
    stream->handle = file;
  } else {
    delete file;
  }
#elif __has_include(<SD.h>)
  File* file = new File(SD.open(path, FILE_READ));
  if (file && *file) {
    stream->size = (size_t)file->size();
    stream->handle = file;
  } else {
    delete file;
  }
#endif
  stream->position = 0;
  stream->media_generation = _media_generation;
  spi_unlock();
  return stream->isOpen();
}

int storage_sd_t::readStream(storage_read_stream_t* stream, uint8_t* dst, size_t length)
{
  if (!isMounted() || !stream || !stream->isOpen() || !dst || length == 0
   || stream->media_generation != _media_generation) { return -1; }
  spi_lock();
  int result = -1;
#if defined(M5UNIFIED_PC_BUILD) || KANPLAY_USE_VFS_SD
  result = (int)fread(dst, 1, length, static_cast<FILE*>(stream->handle));
#elif __has_include(<SdFat.h>)
  result = static_cast<FsFile*>(stream->handle)->read(dst, length);
#elif __has_include(<SD.h>)
  result = static_cast<File*>(stream->handle)->read(dst, length);
#endif
  if (result > 0) { stream->position += (size_t)result; }
  spi_unlock();
  if (result < 0 || (result == 0 && stream->position < stream->size)) { noteMediaError(); }
  return result;
}

bool storage_sd_t::seekStream(storage_read_stream_t* stream, size_t position)
{
  if (!isMounted() || !stream || !stream->isOpen() || position > stream->size
   || stream->media_generation != _media_generation) { return false; }
  spi_lock();
  bool result = false;
#if defined(M5UNIFIED_PC_BUILD) || KANPLAY_USE_VFS_SD
  result = fseek(static_cast<FILE*>(stream->handle), (long)position, SEEK_SET) == 0;
#elif __has_include(<SdFat.h>)
  result = static_cast<FsFile*>(stream->handle)->seekSet(position);
#elif __has_include(<SD.h>)
  result = static_cast<File*>(stream->handle)->seek(position);
#endif
  if (result) { stream->position = position; }
  spi_unlock();
  return result;
}

void storage_sd_t::closeReadStream(storage_read_stream_t* stream)
{
  if (!stream || !stream->isOpen()) { return; }
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD) || KANPLAY_USE_VFS_SD
  fclose(static_cast<FILE*>(stream->handle));
#elif __has_include(<SdFat.h>)
  static_cast<FsFile*>(stream->handle)->close();
  delete static_cast<FsFile*>(stream->handle);
#elif __has_include(<SD.h>)
  static_cast<File*>(stream->handle)->close();
  delete static_cast<File*>(stream->handle);
#endif
  stream->handle = nullptr;
  stream->size = 0;
  stream->position = 0;
  stream->media_generation = 0;
  spi_unlock();
}

int storage_sd_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  if (!isMounted()) { return -1; }

  int result = -1;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "w");
  if (FP) {
    result = fwrite(data, 1, length, FP);
    fclose(FP);
  }
#elif KANPLAY_USE_VFS_SD
  {
    auto fullpath = sd_vfs_path(path);
    result = vfs_saveToFile(fullpath.c_str(), data, length);
    // タイムスタンプ設定
    if (result >= 0) {
      auto now = time(nullptr);
      struct utimbuf times;
      times.actime = now;
      times.modtime = now;
      utime(fullpath.c_str(), &times);
    }
  }

#elif __has_include(<SdFat.h>)
  auto file = SD.open(path, O_CREAT | O_WRITE | O_TRUNC);
  if (file) {
    result = file.write(data, length);

    auto now = time(nullptr);
    auto tm = gmtime(&now);
    file.timestamp(T_CREATE|T_WRITE, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    file.close();
  }

#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_WRITE);
  if (file) {
    result = file.write(data, length);
    file.close();
  }

#endif
  spi_unlock();
  if (result < 0 || (size_t)result != length) { noteMediaError(); }
  return result;
}

int storage_sd_t::appendFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  if (!isMounted() || !path || !data) { return -1; }

  int result = -1;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "ab");
  if (FP) {
    result = fwrite(data, 1, length, FP);
    fclose(FP);
  }
#elif KANPLAY_USE_VFS_SD
  {
    auto fullpath = sd_vfs_path(path);
    auto FP = fopen(fullpath.c_str(), "ab");
    if (FP) {
      result = fwrite(data, 1, length, FP);
      fclose(FP);
    }
  }
#elif __has_include(<SdFat.h>)
  auto file = SD.open(path, O_WRITE | O_APPEND);
  if (file) {
    result = file.write(data, length);
    file.close();
  }
#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_APPEND);
  if (file) {
    result = file.write(data, length);
    file.close();
  }
#endif
  spi_unlock();
  if (result < 0 || (size_t)result != length) { noteMediaError(); }
  return result;
}

int storage_sd_t::patchFromMemoryToFile(const char* path, size_t offset,
                                        const uint8_t* data, size_t length)
{
  if (!isMounted() || !path || !data) { return -1; }
  int result = -1;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "r+b");
  if (FP) {
    if (fseek(FP, (long)offset, SEEK_SET) == 0) { result = fwrite(data, 1, length, FP); }
    fclose(FP);
  }
#elif KANPLAY_USE_VFS_SD
  {
    auto fullpath = sd_vfs_path(path);
    auto FP = fopen(fullpath.c_str(), "r+b");
    if (FP) {
      if (fseek(FP, (long)offset, SEEK_SET) == 0) { result = fwrite(data, 1, length, FP); }
      fclose(FP);
    }
  }
#elif __has_include(<SdFat.h>)
  auto file = SD.open(path, O_WRITE);
  if (file) {
    if (file.seekSet(offset)) { result = file.write(data, length); }
    file.close();
  }
#elif __has_include (<SD.h>)
  auto file = SD.open(path, FILE_WRITE);
  if (file) {
    if (file.seek(offset)) { result = file.write(data, length); }
    file.close();
  }
#endif
  spi_unlock();
  if (result < 0 || (size_t)result != length) { noteMediaError(); }
  return result;
}

int storage_sd_t::getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix)
{
  if (!isMounted()) { return -1; }

  file_info_string_t info;

  int result = -1;

  const size_t len_suffix = suffix ? strlen(suffix) : 0;

  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)

  if (path[0] == '/') { ++path; }
  bool hit = std::filesystem::exists(path);
  M5_LOGD("exists:%d , %s", hit, path);
  if (hit) {
    hit = std::filesystem::is_directory(path);
    M5_LOGD("is_dir:%d , %s", hit, path);
    if (hit) {
      for (const auto& file : std::filesystem::directory_iterator(path)) {
        // list.push_back({file.path().filename().u8string().c_str(), file.file_size()});
        auto name = file.path().filename().string();
        auto len_name = name.length();
        if ( len_name < len_suffix) continue;
        if (len_suffix > 0) {
          if (strcmp(&name[len_name - len_suffix], suffix) != 0) {
            continue;
          }
        }
        size_t size = file.file_size();
        list.push_back({file.path().filename().string(), size});
      }
    } else {
      size_t size = std::filesystem::file_size(path);
      list.push_back({ "", size });
M5_LOGD("file size:%d , %s", size, path);
    }
    result = list.size();
  }
#elif KANPLAY_USE_VFS_SD
  result = vfs_getFileList(list, sd_vfs_path(path).c_str(), suffix);

#elif __has_include(<SdFat.h>)
  auto dir = SD.open(path);
  if (false != dir) {
    if (dir.isDirectory()) {
      FsFile file;
      while (false != (file = dir.openNextFile())) {
        char filename[256];
        file.getName(filename, sizeof(filename));
        // mac系不可視ファイル無視
        if (memcmp(filename, "._", 2) == 0) { continue; }
        auto len_name = strlen(filename);
        if ( len_name < len_suffix) continue;
        if (len_suffix > 0) {
          if (strcmp(&filename[len_name - len_suffix], suffix) != 0) {
            continue;
          }
        }
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
    result = list.size();
  }
#elif __has_include (<SD.h>)
  auto dir = SD.open(path);
  if (dir) {
    if (dir.isDirectory()) {
      fs::File file;
      while (false != (file = dir.openNextFile())) {
        info.filename = file.name();
        info.filesize = file.size();
        list.push_back( info );
      }
      dir.close();
    }
    result = list.size();
  }
#endif

  spi_unlock();
  if (!list.empty()) {
    std::sort(list.begin(), list.end(), [](const file_info_string_t& a, const file_info_string_t& b) { return a.filename < b.filename; });
  }
  return result;
}

int storage_sd_t::getDirectoryList(std::vector<file_info_string_t>& list, const char* path)
{
  if (!isMounted() || !path) { return -1; }
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  const char* local_path = path[0] == '/' ? path + 1 : path;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(local_path, ec)) {
    if (!ec && entry.is_directory()) { list.push_back({ entry.path().filename().string(), 0 }); }
  }
#elif KANPLAY_USE_VFS_SD
  DIR* dir = opendir(sd_vfs_path(path).c_str());
  if (dir) {
    while (dirent* entry = readdir(dir)) {
      if (entry->d_name[0] == '.') { continue; }
      std::string full = sd_vfs_path(path) + "/" + entry->d_name;
      struct stat st {};
      if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { list.push_back({ entry->d_name, 0 }); }
    }
    closedir(dir);
  }
#elif __has_include(<SdFat.h>)
  auto dir = SD.open(path);
  if (dir && dir.isDirectory()) {
    FsFile file;
    while (false != (file = dir.openNextFile())) {
      if (file.isDirectory()) {
        char name[128] = {};
        file.getName(name, sizeof(name));
        if (name[0] != '.') { list.push_back({ name, 0 }); }
      }
      file.close();
    }
    dir.close();
  }
#elif __has_include(<SD.h>)
  auto dir = SD.open(path);
  if (dir && dir.isDirectory()) {
    fs::File file;
    while (false != (file = dir.openNextFile())) {
      if (file.isDirectory()) {
        const char* name = file.name();
        if (name && name[0] != '.') { list.push_back({ name, 0 }); }
      }
      file.close();
    }
    dir.close();
  }
#endif
  spi_unlock();
  std::sort(list.begin(), list.end(), [](const file_info_string_t& a, const file_info_string_t& b) { return a.filename < b.filename; });
  return (int)list.size();
}

bool storage_sd_t::makeDirectory(const char* path)
{
  if (!isMounted() || !path) { return false; }
  bool res = false;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  res = std::filesystem::create_directory(path);
#elif KANPLAY_USE_VFS_SD
  res = (mkdir(sd_vfs_path(path).c_str(), 0775) == 0);

#elif __has_include (<SdFat.h>)
  res = SD.mkdir(path);
#elif __has_include (<SD.h>)
  res = SD.mkdir(path);
#endif
  spi_unlock();
  return res;
}

bool storage_sd_t::removeFile(const char* path)
{
  if (!isMounted() || !path) { return false; }
  bool res = false;
  spi_lock();
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  res = std::filesystem::remove(path);
#elif KANPLAY_USE_VFS_SD
  res = (remove(sd_vfs_path(path).c_str()) == 0);

#elif __has_include (<SdFat.h>)
  res = SD.remove(path);
#elif __has_include (<SD.h>)
  res = SD.remove(path);
#endif
  spi_unlock();
  return res;
}

bool storage_sd_t::renameFile(const char* path, const char* newpath)
{
  if (!isMounted() || !path || !newpath) { return false; }
#if defined(M5UNIFIED_PC_BUILD)
  return false;
#elif KANPLAY_USE_VFS_SD
  spi_lock();
  bool res = (rename(sd_vfs_path(path).c_str(), sd_vfs_path(newpath).c_str()) == 0);
  spi_unlock();
  return res;
#elif __has_include(<SdFat.h>)
  spi_lock();
  bool res = SD.rename(path, newpath);
  spi_unlock();
  return res;
#elif __has_include(<SD.h>)
  spi_lock();
  bool res = SD.rename(path, newpath);
  spi_unlock();
  return res;
#else
  return false;
#endif
}

//=========================================================================
// storage_littlefs_t
//=========================================================================

bool storage_littlefs_t::beginStorage(void)
{
  if (_is_begin) { return true; }

#if defined(M5UNIFIED_PC_BUILD)
  _is_begin = true;
#elif KANPLAY_USE_VFS_LITTLEFS
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = LITTLEFS_MOUNT_POINT;
  conf.partition_label = "spiffs";
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;
  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  _is_begin = (ret == ESP_OK);
  if (_is_begin) {
    M5_LOGI("LittleFS mounted at %s", LITTLEFS_MOUNT_POINT);
  }

#elif __has_include(<LittleFS.h>)
  _is_begin = LittleFS.begin(true);
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
#if defined(M5UNIFIED_PC_BUILD)
#elif KANPLAY_USE_VFS_LITTLEFS
  esp_vfs_littlefs_unregister("spiffs");

#elif __has_include(<LittleFS.h>)
  LittleFS.end();
#endif
}

int storage_littlefs_t::getFileSize(const char* path)
{
  int res = -1;
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  if (std::filesystem::exists(path)) {
    res = std::filesystem::file_size(path);
  }
#elif KANPLAY_USE_VFS_LITTLEFS
  res = vfs_getFileSize(littlefs_vfs_path(path).c_str());

#elif __has_include(<LittleFS.h>)
  if (LittleFS.exists(path)) {
    auto file = LittleFS.open(path);
    if (file) {
      res = file.size();
      file.close();
    }
  }
#endif
  return res;
}

int storage_littlefs_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length)
{
  if (!_is_begin) { return -1; }

  int len = -1;
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "r");
M5_LOGV("littlefs:loadFromFileToMemory : %s  open:%d", path, FP != nullptr);
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
#elif KANPLAY_USE_VFS_LITTLEFS
  auto fullpath = littlefs_vfs_path(path);
  M5_LOGD("LittleFS open:%s", fullpath.c_str());
  len = vfs_loadFromFile(fullpath.c_str(), dst, max_length);

#elif __has_include(<LittleFS.h>)
  bool exists = LittleFS.exists(path);
  M5_LOGD("LittleFS open:%s exists:%d", path, exists);
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
#endif

  return len;
}

int storage_littlefs_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  if (!_is_begin) { return -1; }

  size_t writelen = 0;
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  auto FP = fopen(path, "w");
  if (!FP) { return -1; }
  writelen = fwrite(data, 1, length, FP);
  fclose(FP);
#elif KANPLAY_USE_VFS_LITTLEFS || __has_include(<LittleFS.h>)
  {
    // Avoid Arduino FS/File and stdio here. Both ultimately call fopen(),
    // which allocates a recursive newlib FILE lock and aborts instead of
    // returning an error when BLE + AMY leave only fragmented internal DRAM.
    char tmppath[48];
    char fullpath[256];
    snprintf(tmppath, sizeof(tmppath), "%s/.tmpsave.tmp", LITTLEFS_MOUNT_POINT);
    snprintf(fullpath, sizeof(fullpath), "%s%s", LITTLEFS_MOUNT_POINT, path);
    const int fd = ::open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { return -1; }
    while (writelen < length) {
      const ssize_t written = ::write(fd, data + writelen, length - writelen);
      if (written <= 0) { break; }
      writelen += (size_t)written;
    }
    ::close(fd);
    taskYIELD();
    if (writelen == length) {
      ::unlink(fullpath);
      if (::rename(tmppath, fullpath) != 0) { writelen = 0; }
    } else {
      ::unlink(tmppath);
    }
  }

#endif

  return writelen;
}

int storage_littlefs_t::getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix)
{
  if (!_is_begin) { return -1; }

  file_info_string_t info;

  const size_t len_suffix = suffix ? strlen(suffix) : 0;

#if defined(M5UNIFIED_PC_BUILD)

if (path[0] == '/' && path[1] != '\0') { ++path; }
bool result = std::filesystem::exists(path);
M5_LOGD("exists:%d , %s", result, path);
  if (result) {
    result = std::filesystem::is_directory(path);
M5_LOGD("is_dir:%d , %s", result, path);
    if (result) {
      const char* p = (path[0] == '/') ? "./" : path;
      for (const auto& file : std::filesystem::directory_iterator(p)) {
        if (std::filesystem::is_directory(file)) {
          M5_LOGD(" skip dir:%s", file.path().filename().string().c_str());
          continue;
        }
        // list.push_back({file.path().filename().u8string().c_str(), file.file_size()});

        auto name = file.path().filename().string();
        auto len_name = name.length();
        if ( len_name < len_suffix) continue;
        if (len_suffix > 0) {
          if (strcmp(&name[len_name - len_suffix], suffix) != 0) {
            continue;
          }
        }
M5_LOGD("file found:%s", file.path().filename().string().c_str());

        size_t size = file.file_size();
        list.push_back({file.path().filename().string(), size});
      }
    } else {
      size_t size = std::filesystem::file_size(path);
      list.push_back({ "", size });
M5_LOGD("file size:%d , %s", size, path);
    }
  }
#elif KANPLAY_USE_VFS_LITTLEFS
  auto fullpath = littlefs_vfs_path(path);
  M5_LOGV("LittleFS getFileList: %s", fullpath.c_str());
  return vfs_getFileList(list, fullpath.c_str(), suffix);

#elif __has_include(<LittleFS.h>)
  if (LittleFS.exists(path)) {
M5_LOGV("LittleFS check exists:%s found", path);
    auto dir = LittleFS.open(path);
    if (false == dir) { return -1; }
    if (dir.isDirectory()) {
      fs::File file;
      while (false != (file = dir.openNextFile())) {
        info.filename = file.name();
        info.filesize = file.size();
        auto len_name = info.filename.length();
        if (len_name < len_suffix) continue;
        if (len_suffix > 0) {
          if (strcmp(&info.filename[len_name - len_suffix], suffix) != 0) {
            continue;
          }
        }
        list.push_back( info );
M5_LOGV("file %s %d", info.filename.c_str(), info.filesize);
      }
      dir.close();
    } else {
      info.filename = "";
      info.filesize = dir.size();
      list.push_back( info );
    }
  } else {
    M5_LOGV("LittleFS check exists:%s not found", path);
  }
#endif

  return list.size();
}

bool storage_littlefs_t::makeDirectory(const char* path)
{
#if defined(M5UNIFIED_PC_BUILD)
  return false;
#elif KANPLAY_USE_VFS_LITTLEFS
  return (mkdir(littlefs_vfs_path(path).c_str(), 0775) == 0);
#else
  return false;
#endif
}

bool storage_littlefs_t::removeFile(const char* path)
{
  bool res = false;
#if defined(M5UNIFIED_PC_BUILD)
  if (path[0] == '/') { ++path; }
  res = std::filesystem::remove(path);
#elif KANPLAY_USE_VFS_LITTLEFS
  res = (remove(littlefs_vfs_path(path).c_str()) == 0);

#elif __has_include(<LittleFS.h>)
  res = LittleFS.remove(path);
#endif
  return res;
}

bool storage_littlefs_t::renameFile(const char* path, const char* newpath)
{
#if defined(M5UNIFIED_PC_BUILD)
  return false;
#elif KANPLAY_USE_VFS_LITTLEFS
  return (rename(littlefs_vfs_path(path).c_str(), littlefs_vfs_path(newpath).c_str()) == 0);
#else
  return false;
#endif
}

//=========================================================================
// storage_incbin_t
//=========================================================================

bool storage_incbin_t::beginStorage(void)
{
  return true;
}

void storage_incbin_t::endStorage(void)
{
}

int storage_incbin_t::getFileSize(const char* path)
{
  for (size_t i = 0; i < _file_count; ++i) {
    if (strcmp(_files[i].filename, path) == 0) {
      return _files[i].size;
    }
  }
  return -1;
}

int storage_incbin_t::loadFromFileToMemory(const char* path, uint8_t* dst, size_t max_length)
{
  for (size_t i = 0; i < _file_count; ++i) {
    if (strcmp(_files[i].filename, path) == 0) {
      auto size = _files[i].size;
      if (size > max_length) { size = max_length; }
      memcpy(dst, _files[i].data, size);
      return size;
    }
  }
  return -1;
}

int storage_incbin_t::saveFromMemoryToFile(const char* path, const uint8_t* data, size_t length)
{
  return -1;
}

int storage_incbin_t::getFileList(std::vector<file_info_string_t>& list, const char* path, const char* suffix)
{
  for (size_t i = 0; i < _file_count; ++i) {
    file_info_string_t info;
    info.filename = _files[i].filename;
    info.filesize = _files[i].size;
    list.push_back(info);
  }
  return list.size();
}

//=========================================================================
// dir_manage_t
//=========================================================================

bool dir_manage_t::updateFileList(void)
{
  _file_list_count = 0;
  if (_storage == nullptr) { return false; }
  std::vector<file_info_string_t> list;
  int result = _storage->getFileList(list, _path, def::app::fileext_song);
  if (result < 0 && _storage != &storage_sd) {
    _storage->endStorage();
    _storage->beginStorage();
    result = _storage->getFileList(list, _path, def::app::fileext_song);
  }

  if (result <= 0) {
    if (_file_list) {
      m5gfx::heap_free(_file_list);
      _file_list = nullptr;
    }
    if (_all_filenames) {
      m5gfx::heap_free(_all_filenames);
      _all_filenames = nullptr;
    }
    // マイナス値の場合はエラーとみなしてfalseを返す。
    return result == 0;
  }

  // 以下、得られたデータを std::vector から PSRAM 上のバッファに変換して格納する。
  {
    // ファイルリスト構造体配列をPSRAMに確保する。
    auto file_list = (file_info_t*)m5gfx::heap_alloc_psram(sizeof(file_info_t) * list.size());
    if (file_list == nullptr) {
      M5_LOGE("dir_manage_t::update:heap_alloc_psram failed. size:%d", sizeof(file_info_t) * list.size());
      return false;
    }

    // ファイル名をヌル区切りで連結したバッファを作成する。
    std::vector<char> all_filenames;
    file_info_t* p = file_list;
    for (auto& file : list) {
      p->filename = (char*)all_filenames.size();
      p->filesize = file.filesize;
      ++p;
      all_filenames.insert(all_filenames.end(), file.filename.begin(), file.filename.end());
      all_filenames.insert(all_filenames.end(), 2, '\0'); // ヌル終端
M5_LOGV("dir_manage_t::update file:%s size:%d", file.filename.c_str(), file.filesize);
    }

    // バッファをPSRAMにコピーする。
    char* psram_buffer = (char*)m5gfx::heap_alloc_psram(all_filenames.size());
    if (psram_buffer == nullptr) {
      M5_LOGE("dir_manage_t::update:heap_alloc_psram failed. size:%d", all_filenames.size());
      m5gfx::heap_free(file_list);
      return false;
    }
    memcpy(psram_buffer, all_filenames.data(), all_filenames.size());
    // ファイル名ポインタをPSRAM上のアドレスに変換する。
    p = file_list;
    for (size_t i = 0; i < list.size(); ++i) {
      p->filename = psram_buffer + (size_t)(p->filename);
      ++p;
    }

    auto prev_file_list = _file_list;
    _file_list = file_list;
    _file_list_count = list.size();
    auto prev_all_filenames = _all_filenames;
    _all_filenames = psram_buffer;
    if (prev_file_list) {
      m5gfx::heap_free(prev_file_list);
    }
    if (prev_all_filenames) {
      m5gfx::heap_free(prev_all_filenames);
    }
  }

  return true;
}

void dir_manage_t::invalidateFileList(void)
{
  _file_list_count = 0;
  if (_file_list) { m5gfx::heap_free(_file_list); _file_list = nullptr; }
  if (_all_filenames) { m5gfx::heap_free(_all_filenames); _all_filenames = nullptr; }
}

int dir_manage_t::search(const char* filename) const
{
  for (size_t i = 0; i < _file_list_count; ++i) {
    if (strcmp(_file_list[i].filename, filename) == 0) {
      return i;
    }
  }
  return -1;
}

std::string dir_manage_t::getFullPath(size_t index)
{
  if (index >= _file_list_count) { return ""; }
  return std::string(_path) + _file_list[index].filename;
}

std::string dir_manage_t::makeFullPath(const char* filename) const
{
  return std::string(_path) + filename;
}

//=========================================================================
// file_manage_t
//=========================================================================
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
  return dir->updateFileList();
}

void file_manage_t::setLatestFileInfo(def::app::data_type_t data_type, const char* filename)
{
  if (data_type == def::app::data_type_t::data_song_blank) {
    // ソングリセット時は最後に開いたファイル情報をクリアする
    // 表示用ファイル名は "new_YYYYMMDD_HHMMSS" 形式のデフォルトを生成しておく
    _latest_data_type = def::app::data_type_t::data_unknown;
    _latest_file_name.clear();
    _latest_file_index = -1;
    auto t = time(nullptr);
    auto tm = localtime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "new_%04d%02d%02d_%02d%02d%02d",
      tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
      tm->tm_hour, tm->tm_min, tm->tm_sec);
    _display_file_name = buf;
    return;
  }
  auto is_song_data_type = [](def::app::data_type_t t) {
    switch (t) {
    case def::app::data_type_t::data_song_preset_genre:
    case def::app::data_type_t::data_song_preset_genre_pop:
    case def::app::data_type_t::data_song_preset_genre_rock:
    case def::app::data_type_t::data_song_preset_genre_dance:
    case def::app::data_type_t::data_song_preset_genre_funk:
    case def::app::data_type_t::data_song_preset_genre_rnb:
    case def::app::data_type_t::data_song_preset_genre_jazz:
    case def::app::data_type_t::data_song_preset_genre_latin:
    case def::app::data_type_t::data_song_preset_genre_acoustic:
    case def::app::data_type_t::data_song_preset_genre_ballad:
    case def::app::data_type_t::data_song_preset_genre_specialty:
    case def::app::data_type_t::data_song_preset_genre_old:
    case def::app::data_type_t::data_song_preset_song:
    case def::app::data_type_t::data_song_users:
    case def::app::data_type_t::data_song_extra:
      return true;
    default: return false;
    }
  };
  if (is_song_data_type(data_type)) {
    _latest_data_type = data_type;
    _latest_file_name = (filename != nullptr) ? filename : "";
    _display_file_name = trimExtension(_latest_file_name);
    _latest_file_index = -1;
    auto dir = getDirManage(data_type);
    if (dir != nullptr) {
      if (dir->isEmpty()) {
        updateFileList(data_type);
      }
      _latest_file_index = dir->search(_latest_file_name.c_str());
    }
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

memory_info_t* file_manage_t::loadFile(def::app::data_type_t dir_type, size_t index)
{
  auto dir = getDirManage(dir_type);
  if (dir == nullptr) { return nullptr; }
  if (index >= dir->getCount()) {
    dir->updateFileList();
  }
  if ((int16_t)index < 0) { // マイナス指定されている場合は末尾側として扱えるようにindexを加算する
    index = ((int16_t)index) + dir->getCount();
  }
  if (index < dir->getCount())
  {
    auto info = dir->getInfo(index);
    // M5_LOGV("file_manage_t::loadFile : index:%d %s", index, info->filename);
    return loadFile(dir_type, info->filename);
  }
  return nullptr;
}

memory_info_t* file_manage_t::loadFile(def::app::data_type_t dir_type, const char* file_name)
{
  auto dir = getDirManage(dir_type);
  if (dir == nullptr) { return nullptr; }
  auto storage = dir->getStorage();
  if (storage == nullptr) { return nullptr; }

  if (storage->isBegin() == false) { storage->beginStorage(); }
  auto fullpath = dir->makeFullPath(file_name);
  auto filesize = storage->getFileSize(fullpath.c_str());
// M5_LOGV("file_manage_t::loadFile : %s size:%d", fullpath.c_str(), filesize);
  if (filesize > 0) {
    auto memory = createMemoryInfo(filesize);
// M5_LOGV("memory: create size:%d  result:%p", filesize, memory);
    if (memory != nullptr) {
      memory->dir_type = dir_type;
      memory->filename = file_name;
      if (0 <= storage->loadFromFileToMemory(fullpath.c_str(), memory->data, memory->size)) {
M5_LOGV(" loaded:%s size:%d", fullpath.c_str(), memory->size);
        return memory;
      }
      memory->release();
    }
  }
M5_LOGE(" load failed:%s", fullpath.c_str());
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
  if (result != mem->size && st != &storage_sd) {
    st->endStorage();
    st->beginStorage();
    result = st->saveFromMemoryToFile(path.c_str(), mem->data, mem->size);
  }
// M5_LOGV("save:%s size:%d result:%d", path.c_str(), mem->size, result);

  if (result != mem->size) {
    return false;
  }

  return true;
}

bool file_manage_t::removeFile(def::app::data_type_t dir_type, const char* filename)
{
  auto dir = getDirManage(dir_type);
  if (dir == nullptr) { return false; }
  auto storage = dir->getStorage();
  if (storage == nullptr) { return false; }
  auto fullpath = dir->makeFullPath(filename);
  return storage->removeFile(fullpath.c_str());
}

bool file_manage_t::renameFile(def::app::data_type_t dir_type, const char* old_name, const char* new_name)
{
  auto dir = getDirManage(dir_type);
  if (dir == nullptr) { return false; }
  auto storage = dir->getStorage();
  if (storage == nullptr) { return false; }
  if (storage->isBegin() == false) { storage->beginStorage(); }
  auto oldpath = dir->makeFullPath(old_name);
  auto newpath = dir->makeFullPath(new_name);
  return storage->renameFile(oldpath.c_str(), newpath.c_str());
}

void file_manage_t::invalidateStorage(storage_base_t* storage)
{
  for (auto& dir : dir_manage) {
    if (dir.getStorage() == storage) { dir.invalidateFileList(); }
  }
}
//-------------------------------------------------------------------------
}; // namespace kanplay_ns

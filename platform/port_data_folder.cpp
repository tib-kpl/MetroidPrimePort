#include "port_data_folder.h"
#include "port_strings.h"

#include "port_paths.h"
#include "port_remastered_storage.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(__ANDROID__)
#include "port_debug.h"
#include "port_log.h"
#include "port_remastered_import.h"
#include "port_utf8_path.h"

#include <imgui.h>
#include <jni.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <mutex>
#include <thread>
#endif

namespace PortDataFolder {
namespace {

namespace fs = std::filesystem;
using PortUtf8::FromUtf8;
using PortUtf8::ToUtf8;

const char* const kTempSuffix = ".mpcopy";
const char* const kIncomplete = ".mpcopy_incomplete";

std::string Relative(const fs::path& path, const fs::path& base) {
  const std::u8string text = path.lexically_relative(base).generic_u8string();
  return std::string(text.begin(), text.end());
}

using port::EndsWith;

// Every file a move copies, relative to `root` with '/' separators.
// `failed`, when given, is set if the walk stopped on an error: the list is then
// incomplete, which a copy must not take for the whole folder.
std::vector< std::string > ListFiles(const std::string& root, bool* failed = nullptr, bool everything = false) {
  std::vector< std::string > files;
  std::error_code ec;
  const fs::path base = FromUtf8(root);
  if (failed != nullptr) {
    *failed = false;
  }
  if (!fs::is_directory(base, ec)) {
    return files;
  }
  fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const std::string relative = Relative(it->path(), base);
    if (!everything && IsSkipped(relative)) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_regular_file(ec)) {
      files.push_back(relative);
    }
  }
  if (ec && failed != nullptr) {
    *failed = true;
  }
  return files;
}

} // namespace

std::string SharedFolder() {
#if defined(__ANDROID__)
  // EXTERNAL_STORAGE is usually /sdcard, a link to the primary volume of the
  // current user; the resolved path is what a file manager shows.
  const char* env = std::getenv("EXTERNAL_STORAGE");
  std::string root = env != nullptr && env[0] == '/' ? env : "/storage/emulated/0";
  std::error_code ec;
  const fs::path resolved = fs::canonical(FromUtf8(root), ec);
  if (!ec) {
    root = ToUtf8(resolved);
  }
  return PortPaths::detail::WithSeparator(root) + "MetroidPrime/";
#else
  return {};
#endif
}

bool IsSkipped(const std::string& relative) {
  // The built-in textures and initial pipeline cache are re-copied from the APK
  // on every launch, and the shader caches always stay in private storage.
  static const char* const kTopLevel[] = {"textures", "initial_pipeline_cache.db", "data_folder.txt",
                                          "texture_dumps", kIncomplete};
  static const char* const kCaches[] = {"pipeline_cache.db", "dawn_cache.db"};
  const size_t slash = relative.find('/');
  const std::string first = relative.substr(0, slash);
  // The built-in Remastered import belongs to the app (port_remastered_storage.h): moving the data
  // folder neither carries it along nor counts it, also when the data folder is the app's own
  // storage, which is where the import sits.
  if (first == PortRemasteredStorage::kRootFolderName) {
    return true;
  }
  if (slash == std::string::npos) {
    for (const char* name : kTopLevel) {
      if (first == name) {
        return true;
      }
    }
    for (const char* name : kCaches) {
      if (first.rfind(name, 0) == 0) {
        return true;
      }
    }
  } else if (first == "textures" || first == "texture_dumps") {
    return true;
  }
  const std::string name = relative.substr(relative.rfind('/') + 1);
  return name == ".port_write_test" || EndsWith(name, ".partial") || EndsWith(name, ".part") ||
         EndsWith(name, kTempSuffix);
}

Totals Measure(const std::string& from, bool everything) {
  Totals totals;
  const fs::path base = FromUtf8(from);
  for (const std::string& relative : ListFiles(from, nullptr, everything)) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(base / FromUtf8(relative), ec);
    if (!ec) {
      totals.bytes += size;
    }
    ++totals.files;
  }
  return totals;
}

bool HasData(const std::string& dir) {
  return !ListFiles(dir).empty();
}

bool IsComplete(const std::string& dir) {
  std::error_code ec;
  return !fs::exists(FromUtf8(dir) / kIncomplete, ec);
}

bool CopyTree(const std::string& from, const std::string& to, Progress& progress, std::string& error,
              bool changedOnly, bool everything) {
  const fs::path source = FromUtf8(from);
  const fs::path target = FromUtf8(to);
  const fs::path incomplete = target / kIncomplete;
  {
    std::error_code ec;
    fs::create_directories(target, ec);
    std::ofstream flag(incomplete, std::ios::binary | std::ios::trunc);
    if (!flag) {
      error = "Could not write to " + to;
      return false;
    }
  }
  std::vector< char > buffer(1 << 20);
  bool listFailed = false;
  const std::vector< std::string > files = ListFiles(from, &listFailed, everything);
  if (listFailed) {
    // The old data is left alone, and the incomplete flag stays in the target.
    error = "Could not read all of " + from + " (a folder could not be listed).";
    return false;
  }
  for (const std::string& relative : files) {
    const fs::path in = source / FromUtf8(relative);
    const fs::path out = target / FromUtf8(relative);
    fs::path temp = out;
    temp += kTempSuffix;
    std::error_code ec;
    if (changedOnly) {
      // The copy is written after the original, so a newer original changed since.
      std::error_code inEc;
      const uintmax_t inSize = fs::file_size(in, inEc);
      const uintmax_t outSize = fs::file_size(out, ec);
      const bool sameSize = !inEc && !ec && inSize == outSize;
      const auto inTime = fs::last_write_time(in, inEc);
      const auto outTime = fs::last_write_time(out, ec);
      if (sameSize && !inEc && !ec && outTime >= inTime) {
        progress.filesDone.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
    }
    fs::create_directories(out.parent_path(), ec);
    std::ifstream input(in, std::ios::binary);
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
      error = "Could not " + std::string(!input ? "read " : "write ") + ToUtf8(!input ? in : out);
      fs::remove(temp, ec);
      return false;
    }
    while (input) {
      if (progress.cancel.load(std::memory_order_relaxed)) {
        output.close();
        fs::remove(temp, ec);
        error = "Cancelled.";
        return false;
      }
      input.read(buffer.data(), static_cast< std::streamsize >(buffer.size()));
      const std::streamsize got = input.gcount();
      if (got > 0 && !output.write(buffer.data(), got)) {
        break;
      }
      progress.bytesDone.fetch_add(static_cast< uint64_t >(got), std::memory_order_relaxed);
    }
    output.close();
    if (input.bad() || !output) {
      fs::remove(temp, ec);
      error = "Writing " + ToUtf8(out) + " failed (is the storage full?)";
      return false;
    }
    fs::rename(temp, out, ec);
    if (ec) {
      fs::remove(temp, ec);
      error = "Could not replace " + ToUtf8(out);
      return false;
    }
    progress.filesDone.fetch_add(1, std::memory_order_relaxed);
  }
  std::error_code ec;
  fs::remove(incomplete, ec);
  if (ec) {
    error = "Could not finish the copy in " + to;
    return false;
  }
  return true;
}

void DeleteData(const std::string& dir, const std::function< bool(const std::string&) >& keep) {
  const fs::path base = FromUtf8(dir);
  std::error_code ec;
  for (const std::string& relative : ListFiles(dir)) {
    if (!keep || !keep(relative)) {
      fs::remove(base / FromUtf8(relative), ec);
    }
  }
  // Folders left empty by that, deepest first; a folder that still holds a
  // skipped file stays.
  std::vector< fs::path > dirs;
  fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const std::string relative = Relative(it->path(), base);
    if (IsSkipped(relative)) {
      if (it->is_directory(ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (it->is_directory(ec)) {
      dirs.push_back(it->path());
    }
  }
  for (auto dirIt = dirs.rbegin(); dirIt != dirs.rend(); ++dirIt) {
    if (fs::is_empty(*dirIt, ec)) {
      fs::remove(*dirIt, ec);
    }
  }
}

#if defined(__ANDROID__)
namespace {

// Calls a no-argument method of MetroidPrimeActivity; `boolResult` for "()Z".
bool CallActivity(const char* method, bool boolResult) {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return false;
  }
  bool result = false;
  jclass cls = env->GetObjectClass(activity);
  if (jmethodID id = env->GetMethodID(cls, method, boolResult ? "()Z" : "()V"); id != nullptr) {
    if (boolResult) {
      result = env->CallBooleanMethod(activity, id) == JNI_TRUE;
    } else {
      env->CallVoidMethod(activity, id);
      result = true;
    }
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    result = false;
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
  return result;
}

// Calls a no-argument method of MetroidPrimeActivity that returns a String[].
std::vector< std::string > CallActivityStrings(const char* method) {
  std::vector< std::string > out;
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return out;
  }
  jclass cls = env->GetObjectClass(activity);
  jmethodID id = env->GetMethodID(cls, method, "()[Ljava/lang/String;");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    id = nullptr;
  }
  if (id != nullptr) {
    jobjectArray array = static_cast< jobjectArray >(env->CallObjectMethod(activity, id));
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    } else if (array != nullptr) {
      const jsize count = env->GetArrayLength(array);
      for (jsize i = 0; i < count; ++i) {
        jstring item = static_cast< jstring >(env->GetObjectArrayElement(array, i));
        if (item == nullptr) {
          continue;
        }
        if (const char* text = env->GetStringUTFChars(item, nullptr); text != nullptr) {
          out.emplace_back(text);
          env->ReleaseStringUTFChars(item, text);
        }
        env->DeleteLocalRef(item);
      }
      env->DeleteLocalRef(array);
    }
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
  return out;
}

// The folders the data can go to, one per mounted storage volume (the shared internal storage, each SD
// card or USB drive): <volume>/MetroidPrime/, with a trailing separator. Asked at most every two seconds.
const std::vector< std::string >& MoveTargets() {
  static std::vector< std::string > sTargets;
  static uint64_t sLast = 0;
  const uint64_t now = SDL_GetTicks();
  if (sLast == 0 || now - sLast > 2000) {
    sLast = now;
    sTargets.clear();
    for (const std::string& root : CallActivityStrings("storageRoots")) {
      std::error_code ec;
      std::string resolved = root;
      if (const fs::path canonical = fs::canonical(FromUtf8(root), ec); !ec) {
        resolved = ToUtf8(canonical);
      }
      const std::string target = PortPaths::detail::WithSeparator(resolved) + "MetroidPrime/";
      if (std::find(sTargets.begin(), sTargets.end(), target) == sTargets.end()) {
        sTargets.push_back(target);
      }
    }
    if (sTargets.empty()) {
      sTargets.push_back(SharedFolder());
    }
  }
  return sTargets;
}

enum class EState { Idle, ConfirmOverwrite, Copying, Ready, Failed };

struct Move {
  std::mutex mutex;
  EState state = EState::Idle;
  std::string target;
  std::string message;
  bool copied = false;
  bool targetComplete = true;
  // Tells a finished copy thread whether its result is still the one shown.
  uint64_t generation = 0;
  // Set by the copy thread once it has measured the data.
  std::atomic< uint64_t > totalBytes{0};
  std::atomic< uint64_t > totalFiles{0};
  Progress progress;
};

Move& State() {
  static Move sMove;
  return sMove;
}

std::string Size(uint64_t bytes) {
  char text[32];
  if (bytes >= (1ull << 30)) {
    std::snprintf(text, sizeof(text), "%.1f GB", static_cast< double >(bytes) / (1ull << 30));
  } else {
    std::snprintf(text, sizeof(text), "%.0f MB", static_cast< double >(bytes) / (1ull << 20));
  }
  return text;
}

void StartCopy(const std::string& target) {
  Move& move = State();
  const std::string from = PortPaths::UserFolder();
  uint64_t generation = 0;
  {
    std::lock_guard lock(move.mutex);
    move.target = target;
    move.state = EState::Copying;
    move.copied = true;
    move.message.clear();
    move.totalBytes = 0;
    move.totalFiles = 0;
    move.progress.bytesDone = 0;
    move.progress.filesDone = 0;
    move.progress.cancel = false;
    generation = ++move.generation;
  }
  // Measuring walks the whole folder (mods can be thousands of files), so it
  // runs here and not on the render thread.
  std::thread([from, target, generation] {
    Move& move = State();
    const Totals totals = Measure(from);
    move.totalBytes = totals.bytes;
    move.totalFiles = totals.files;
    std::error_code ec;
    fs::create_directories(FromUtf8(target), ec);
    const fs::space_info space = fs::space(FromUtf8(target), ec);
    std::string error;
    bool ok = false;
    if (!ec && space.available < totals.bytes + (64ull << 20)) {
      error = "Not enough free space: the data needs " + Size(totals.bytes) + ", " + Size(space.available) +
              " is free.";
    } else {
      PortLog::Write("data folder: copying %" PRIu64 " files (%" PRIu64 " bytes) from %s to %s\n", totals.files,
                     totals.bytes, from.c_str(), target.c_str());
      ok = CopyTree(from, target, move.progress, error);
      PortLog::Write("data folder: copy %s%s\n", ok ? "done" : "failed: ", ok ? "" : error.c_str());
    }
    std::lock_guard lock(move.mutex);
    if (move.generation != generation) {
      return;
    }
    move.state = ok ? EState::Ready : EState::Failed;
    move.message = ok ? std::string() : error;
  }).detach();
}

// Points the next launch at the target and restarts into it.
void Switch() {
  Move& move = State();
  std::string target;
  bool copied = false;
  {
    std::lock_guard lock(move.mutex);
    target = move.target;
    copied = move.copied;
  }
  // A second pass for what changed since the copy: the settings, a save made
  // while it ran or after "Not now".
  PortDebug::SaveSettingsNow();
  if (!copied && !IsComplete(target)) {
    std::lock_guard lock(move.mutex);
    move.state = EState::Failed;
    move.message = target + " holds a copy that did not finish; copy the data again.";
    return;
  }
  if (copied) {
    Progress sync;
    std::string error;
    if (!CopyTree(PortPaths::UserFolder(), target, sync, error, true)) {
      std::lock_guard lock(move.mutex);
      move.state = EState::Failed;
      move.message = error;
      return;
    }
  }
  std::error_code ec;
  const std::string marker = PortPaths::detail::MarkerPath();
  bool written = false;
  if (target == PortPaths::detail::PrivateFolder()) {
    fs::remove(FromUtf8(marker), ec);
    written = !fs::exists(FromUtf8(marker), ec);
  } else {
    const std::string temp = marker + ".new";
    std::ofstream file(FromUtf8(temp), std::ios::binary | std::ios::trunc);
    file << target << '\n';
    file.close();
    written = !file.fail();
    if (written) {
      fs::rename(FromUtf8(temp), FromUtf8(marker), ec);
      written = !ec;
    }
  }
  std::lock_guard lock(move.mutex);
  if (!written) {
    move.state = EState::Failed;
    move.message = "Could not record the new folder in " + marker;
    return;
  }
  PortLog::Write("data folder: next launch uses %s\n", target.c_str());
  if (!CallActivity("restartApp", false)) {
    move.message = "Close the game and open it again to finish the move.";
  }
}

// Asked for once a second at most while the panel is open: the grant happens
// in the system settings, and coming back from there is not reported.
bool StorageAccess() {
  static uint64_t sLastCheck = 0;
  static bool sAccess = false;
  const uint64_t now = SDL_GetTicks();
  if (sLastCheck == 0 || now - sLastCheck > 1000) {
    sLastCheck = now;
    sAccess = CallActivity("hasStorageAccess", true);
  }
  return sAccess;
}

void RequestMove(const std::string& target, bool needsAccess) {
  Move& move = State();
  if (needsAccess && !StorageAccess()) {
    CallActivity("requestStorageAccess", false);
    std::lock_guard lock(move.mutex);
    move.state = EState::Idle;
    move.message = "Allow \"All files access\" for Metroid Prime, then come back and press the button again.";
    return;
  }
  if (HasData(target)) {
    const bool complete = IsComplete(target);
    std::lock_guard lock(move.mutex);
    move.target = target;
    move.targetComplete = complete;
    move.message.clear();
    move.state = EState::ConfirmOverwrite;
    return;
  }
  StartCopy(target);
}

// Copies the shader pipeline cache (app storage, which keeps it when the data moves) into
// <data>/shader_cache/, where a file manager or adb can read it; the pipeline seed
// (tools/pipeline_seed.py merge) takes it from there. The write-ahead log goes too: it holds the
// rows added since the last checkpoint, and SQLite reads it along with the file.
std::string ExportShaderCache(const std::string& appFolder, const std::string& dataFolder) {
  if (appFolder.empty() || dataFolder.empty()) {
    return "No app storage folder.";
  }
  const fs::path from = FromUtf8(appFolder);
  const fs::path to = FromUtf8(dataFolder) / "shader_cache";
  std::error_code ec;
  if (!fs::exists(from / "pipeline_cache.db", ec)) {
    return "There is no shader cache yet.";
  }
  fs::create_directories(to, ec);
  uint64_t bytes = 0;
  for (const char* name : {"pipeline_cache.db", "pipeline_cache.db-wal"}) {
    fs::remove(to / name, ec);
    if (!fs::exists(from / name, ec)) {
      continue;
    }
    if (!fs::copy_file(from / name, to / name, fs::copy_options::overwrite_existing, ec)) {
      return std::string("Could not copy ") + name + ": " + ec.message();
    }
    bytes += fs::file_size(to / name, ec);
  }
  PortLog::Write("data folder: shader cache copied to %s\n", ToUtf8(to).c_str());
  return "Copied the shader cache (" + Size(bytes) + ") to " + ToUtf8(to) + ".";
}


// ---- the Remastered import's own location ----
//
// The import (port_remastered_storage.h) is the biggest part of the data, and it stays in the app's storage
// unless the player moves it: the marker file in private storage names the folder it is in, and
// ImportRootFolder reads it. A move copies the installed import to <folder>/remastered-imports/, then
// records the folder and restarts.

enum class IState { Idle, Copying, Ready, Failed };

struct ImportMove {
  std::mutex mutex;
  IState state = IState::Idle;
  std::string target;   // the folder the marker will name (<volume>/MetroidPrime/), empty for app storage
  std::string message;
  std::atomic< uint64_t > totalBytes{0};
  std::atomic< uint64_t > totalFiles{0};
  uint64_t generation = 0;
  Progress progress;
};

ImportMove& ImportState() {
  static ImportMove sImport;
  return sImport;
}

std::string ImportMarker() {
  return PortPaths::detail::PrivateFolder() + PortRemasteredStorage::kChosenMarkerName;
}

// The folder the import is moved to, "" while it is in app storage.
std::string ChosenImport() {
  return PortRemasteredStorage::ReadChosenFolder(ImportMarker());
}

// The import's root for a chosen folder ("" = app storage), as ImportRootFolder works it out.
std::string ImportRoot(const std::string& chosen) {
  return PortRemasteredStorage::ResolveRoot(true, PortPaths::detail::PrivateFolder(), std::string(), chosen).folder;
}

// The installed import's size, measured once on a thread: walking it every frame would stutter.
bool ImportSize(const std::string& root, uint64_t& bytes) {
  static std::mutex sMutex;
  static std::string sRoot;
  static bool sDone = false, sMeasuring = false;
  static uint64_t sBytes = 0;
  std::lock_guard lock(sMutex);
  if (sRoot != root) {
    sRoot = root;
    sDone = false;
  }
  if (!sDone && !sMeasuring) {
    sMeasuring = true;
    const std::string measured = sRoot;
    std::thread([measured] {
      const uint64_t size = Measure(measured + PortRemasteredStorage::kInstalledModName + "/", true).bytes;
      std::lock_guard inner(sMutex);
      if (sRoot == measured) {
        sBytes = size;
        sDone = true;
      }
      sMeasuring = false;
    }).detach();
  }
  bytes = sBytes;
  return sDone;
}

void StartImportCopy(const std::string& fromRoot, const std::string& toRoot, const std::string& chosen) {
  ImportMove& move = ImportState();
  uint64_t generation = 0;
  {
    std::lock_guard lock(move.mutex);
    move.target = chosen;
    move.state = IState::Copying;
    move.message.clear();
    move.totalBytes = 0;
    move.totalFiles = 0;
    move.progress.bytesDone = 0;
    move.progress.filesDone = 0;
    move.progress.cancel = false;
    generation = ++move.generation;
  }
  std::thread([fromRoot, toRoot, generation] {
    ImportMove& move = ImportState();
    const std::string from = fromRoot + PortRemasteredStorage::kInstalledModName + "/";
    const std::string to = toRoot + PortRemasteredStorage::kInstalledModName + "/";
    const Totals totals = Measure(from, true);
    move.totalBytes = totals.bytes;
    move.totalFiles = totals.files;
    std::error_code ec;
    fs::create_directories(FromUtf8(toRoot), ec);
    const fs::space_info space = fs::space(FromUtf8(toRoot), ec);
    std::string error;
    bool ok = false;
    if (ec) {
      error = "Cannot use " + toRoot + ": " + ec.message() + ".";
    } else if (space.available < totals.bytes + (64ull << 20)) {
      error = "Not enough free space: the import needs " + Size(totals.bytes) + ", " + Size(space.available) +
              " is free.";
    } else {
      PortLog::Write("remastered import: copying %" PRIu64 " files (%" PRIu64 " bytes) from %s to %s\n", totals.files,
                     totals.bytes, from.c_str(), to.c_str());
      // changedOnly: a copy cut short resumes, and an identical file already there is not written again.
      ok = CopyTree(from, to, move.progress, error, true, true);
      PortLog::Write("remastered import: copy %s%s\n", ok ? "done" : "failed: ", ok ? "" : error.c_str());
    }
    std::lock_guard lock(move.mutex);
    if (move.generation != generation) {
      return;
    }
    move.state = ok ? IState::Ready : IState::Failed;
    move.message = ok ? std::string() : error;
  }).detach();
}

// Records where the import is and restarts into it.
void SwitchImport() {
  ImportMove& move = ImportState();
  std::string chosen;
  {
    std::lock_guard lock(move.mutex);
    chosen = move.target;
  }
  std::error_code ec;
  const std::string marker = ImportMarker();
  bool written = false;
  if (chosen.empty()) {
    fs::remove(FromUtf8(marker), ec);
    written = !fs::exists(FromUtf8(marker), ec);
  } else {
    const std::string temp = marker + ".new";
    std::ofstream file(FromUtf8(temp), std::ios::binary | std::ios::trunc);
    file << chosen << '\n';
    file.close();
    written = !file.fail();
    if (written) {
      fs::rename(FromUtf8(temp), FromUtf8(marker), ec);
      written = !ec;
    }
  }
  std::lock_guard lock(move.mutex);
  if (!written) {
    move.state = IState::Failed;
    move.message = "Could not record the new folder in " + marker;
    return;
  }
  PortLog::Write("remastered import: next launch uses %s\n", chosen.empty() ? "app storage" : chosen.c_str());
  if (!CallActivity("restartApp", false)) {
    move.message = "Close the game and open it again to finish the move.";
  }
}

void RequestImportMove(const std::string& chosen) {
  ImportMove& move = ImportState();
  if (PortRemastered::ImportStatus().running) {
    std::lock_guard lock(move.mutex);
    move.message = "Wait for the import in progress to finish (or cancel it) before moving it.";
    return;
  }
  if (!chosen.empty() && !CallActivity("hasStorageAccess", true)) {
    CallActivity("requestStorageAccess", false);
    std::lock_guard lock(move.mutex);
    move.message = "Allow \"All files access\" for Metroid Prime, then come back and press the button again.";
    return;
  }
  StartImportCopy(ImportRoot(ChosenImport()), ImportRoot(chosen), chosen);
}

void DrawImportPanel() {
  ImGui::SeparatorText("Remastered import");
  ImportMove& move = ImportState();
  const std::string chosen = ChosenImport();
  const std::string root = ImportRoot(chosen);
  const std::string appRoot = ImportRoot(std::string());
  ImGui::TextWrapped("The Remastered models are in %s%s.", root.c_str(), chosen.empty() ? " (app storage)" : "");
  uint64_t bytes = 0;
  if (ImportSize(root, bytes) && bytes > 0) {
    ImGui::TextWrapped("The import takes %s.", Size(bytes).c_str());
  }

  std::unique_lock lock(move.mutex);
  const IState state = move.state;
  const std::string message = move.message;
  const std::string target = move.target;
  lock.unlock();
  const std::string targetName = target.empty() ? "app storage" : target;

  switch (state) {
  case IState::Idle:
  case IState::Failed:
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (!chosen.empty() && ImGui::Button("Move the import back to app storage")) {
      RequestImportMove(std::string());
    }
    for (const std::string& volume : MoveTargets()) {
      if (volume == chosen) {
        continue;
      }
      const std::string label = "Move the import to " + volume + "##import";
      if (ImGui::Button(label.c_str())) {
        RequestImportMove(volume);
      }
    }
    ImGui::TextWrapped(
        "Copies the import there and restarts. Making or updating an import on an SD card is slower than in app "
        "storage. It needs the \"All files access\" permission.");
    if (!chosen.empty()) {
      // What the move left behind in app storage.
      static bool sConfirmDelete = false;
      static std::atomic< bool > sDeleting{false};
      ImGui::TextWrapped("The import is on %s, so app storage is free of it.", chosen.c_str());
      std::error_code ec;
      const bool oldCopy = fs::is_directory(FromUtf8(appRoot), ec) && !fs::is_empty(FromUtf8(appRoot), ec);
      if (oldCopy && !sDeleting.load()) {
        ImGui::TextWrapped("App storage still holds the earlier import.");
        if (!sConfirmDelete) {
          if (ImGui::Button("Delete the earlier import")) {
            sConfirmDelete = true;
          }
        } else {
          if (ImGui::Button("Delete it for good")) {
            sConfirmDelete = false;
            sDeleting = true;
            std::thread([appRoot] {
              std::error_code removeEc;
              fs::remove_all(FromUtf8(appRoot), removeEc);
              sDeleting = false;
            }).detach();
          }
          ImGui::SameLine();
          if (ImGui::Button("Keep it")) {
            sConfirmDelete = false;
          }
        }
      } else if (sDeleting.load()) {
        ImGui::TextWrapped("Deleting the earlier import...");
      }
    }
    break;
  case IState::Copying: {
    const uint64_t totalBytes = move.totalBytes.load();
    const uint64_t totalFiles = move.totalFiles.load();
    if (totalFiles == 0) {
      ImGui::TextWrapped("Counting files...");
    } else {
      const uint64_t done = move.progress.bytesDone.load(std::memory_order_relaxed);
      const float fraction = totalBytes > 0 ? static_cast< float >(done) / totalBytes : 0.f;
      const std::string overlay = Size(done) + " / " + Size(totalBytes);
      ImGui::TextWrapped("Copying the import to %s... (%" PRIu64 " of %" PRIu64 " files)", targetName.c_str(),
                         move.progress.filesDone.load(std::memory_order_relaxed), totalFiles);
      ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f), overlay.c_str());
    }
    if (ImGui::Button("Cancel##importmove")) {
      move.progress.cancel = true;
    }
    break;
  }
  case IState::Ready:
    ImGui::TextWrapped("The import is in %s. The game restarts to switch to it; the original stays until deleted.",
                       targetName.c_str());
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (ImGui::Button("Restart now##import")) {
      SwitchImport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now##import")) {
      lock.lock();
      move.state = IState::Idle;
      lock.unlock();
    }
    break;
  }
}

} // namespace

void DrawPanel() {
  ImGui::SeparatorText("Data folder");
  Move& move = State();
  const std::string& current = PortPaths::UserFolder();
  const std::string priv = PortPaths::detail::PrivateFolder();
  const bool inPrivate = current == priv;
  ImGui::TextWrapped("Settings, memory card, mods, save states and the texture pack are in %s%s.",
                     current.c_str(), inPrivate ? " (app storage, hidden from file managers)" : "");
  if (const std::string lost = PortPaths::UnavailableFolder(); !lost.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f), "%s could not be opened, so app storage is used this time.",
                       lost.c_str());
    ImGui::TextWrapped("Allow \"All files access\" for Metroid Prime and restart, or stay in app storage.");
    if (ImGui::Button("Allow access")) {
      CallActivity("requestStorageAccess", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stay in app storage")) {
      std::error_code ec;
      fs::remove(FromUtf8(PortPaths::detail::MarkerPath()), ec);
    }
  }

  std::unique_lock lock(move.mutex);
  const EState state = move.state;
  const std::string target = move.target;
  const std::string message = move.message;
  lock.unlock();
  const std::string targetName = target == priv ? "app storage" : target;

  switch (state) {
  case EState::Idle:
  case EState::Failed:
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (inPrivate) {
      // One button per storage volume: the internal storage, and the SD card when there is one.
      for (const std::string& volume : MoveTargets()) {
        const std::string label = "Move to " + volume;
        if (ImGui::Button(label.c_str())) {
          RequestMove(volume, true);
        }
      }
      ImGui::TextWrapped(
          "Copies the data to the storage you pick (an SD card keeps it off the main memory) and restarts. It "
          "stays there after an uninstall, and needs the \"All files access\" permission.");
    } else {
      if (ImGui::Button("Move back to app storage")) {
        RequestMove(priv, false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Copy the shader cache here")) {
        const std::string result = ExportShaderCache(priv, current);
        lock.lock();
        move.message = result;
        lock.unlock();
      }
      // Another storage volume than the one the data is on.
      for (const std::string& volume : MoveTargets()) {
        if (volume == current) {
          continue;
        }
        const std::string label = "Move to " + volume;
        if (ImGui::Button(label.c_str())) {
          RequestMove(volume, true);
        }
      }
      // Measured once: walking a big mods folder every frame would stutter,
      // and nothing else writes to app storage while the data is elsewhere.
      static bool sConfirmDelete = false;
      static bool sOldMeasured = false;
      static uint64_t sOldBytes = 0;
      if (!sOldMeasured) {
        sOldMeasured = true;
        sOldBytes = Measure(priv).bytes;
      }
      if (sOldBytes > 0) {
        ImGui::TextWrapped("App storage still holds the copy from before the move (%s).", Size(sOldBytes).c_str());
        if (!sConfirmDelete) {
          if (ImGui::Button("Delete the old copy")) {
            sConfirmDelete = true;
          }
        } else {
          if (ImGui::Button("Delete it for good")) {
            // The disc copy is kept when it is the only one (a move made
            // before the disc was copied, or "Use the data already there"),
            // and the file the settings point at, whatever its name.
            const bool discElsewhere = fs::exists(FromUtf8(current + "disc.iso"));
            const char* configured = PortDebug::DiscPath();
            const fs::path privBase = FromUtf8(priv);
            DeleteData(priv, [discElsewhere, configured, privBase](const std::string& rel) {
              if (rel == "disc.iso" && !discElsewhere) {
                return true;
              }
              if (configured == nullptr) {
                return false;
              }
              std::error_code same;
              return fs::equivalent(privBase / FromUtf8(rel), FromUtf8(configured), same) && !same;
            });
            sOldMeasured = false;
            sConfirmDelete = false;
          }
          ImGui::SameLine();
          if (ImGui::Button("Keep it")) {
            sConfirmDelete = false;
          }
        }
      }
    }
    break;
  case EState::ConfirmOverwrite:
    if (move.targetComplete) {
      ImGui::TextWrapped("%s already holds game data (from an earlier install or move).", targetName.c_str());
      if (ImGui::Button("Use the data already there")) {
        lock.lock();
        move.copied = false;
        move.state = EState::Ready;
        lock.unlock();
      }
      ImGui::SameLine();
    } else {
      ImGui::TextWrapped("%s holds an earlier copy that did not finish.", targetName.c_str());
    }
    if (ImGui::Button("Copy this data over it")) {
      StartCopy(target);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      lock.lock();
      move.state = EState::Idle;
      lock.unlock();
    }
    break;
  case EState::Copying: {
    const uint64_t totalBytes = move.totalBytes.load();
    const uint64_t totalFiles = move.totalFiles.load();
    if (totalFiles == 0) {
      ImGui::TextWrapped("Counting files...");
    } else {
      const uint64_t done = move.progress.bytesDone.load(std::memory_order_relaxed);
      const float fraction = totalBytes > 0 ? static_cast< float >(done) / totalBytes : 0.f;
      const std::string overlay = Size(done) + " / " + Size(totalBytes);
      ImGui::TextWrapped("Copying to %s... (%" PRIu64 " of %" PRIu64 " files)", targetName.c_str(),
                         move.progress.filesDone.load(std::memory_order_relaxed), totalFiles);
      ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f), overlay.c_str());
    }
    if (ImGui::Button("Cancel##datafolder")) {
      move.progress.cancel = true;
    }
    break;
  }
  case EState::Ready:
    ImGui::TextWrapped("The data is in %s. The game restarts to switch to it; the original stays until deleted.",
                       targetName.c_str());
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (ImGui::Button("Restart now")) {
      Switch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now")) {
      lock.lock();
      move.state = EState::Idle;
      lock.unlock();
    }
    break;
  }
  DrawImportPanel();
}
#endif

} // namespace PortDataFolder

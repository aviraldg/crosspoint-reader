#include <HalStorage.h>
#include <Logging.h>
#include <PlaydateRuntime.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define TAG "STORAGE"

HalStorage HalStorage::instance;

namespace {

std::string normalizeRelPath(const char* rawPath) {
  if (!rawPath) return "";
  std::string p(rawPath);
  while (!p.empty() && p.front() == '/') {
    p.erase(p.begin());
  }
  while (!p.empty() && p.back() == '/') {
    p.pop_back();
  }
  if (p == ".") return "";
  return p;
}

std::string extractBasename(const std::string& relPath) {
  if (relPath.empty()) return "/";
  const size_t slash = relPath.rfind('/');
  return slash == std::string::npos ? relPath : relPath.substr(slash + 1);
}

uint32_t packFatDateTime(const FileStat& st) {
  const uint32_t year = (st.m_year >= 1980) ? static_cast<uint32_t>(st.m_year - 1980) : 0U;
  const uint32_t month = static_cast<uint32_t>(std::clamp(st.m_month, 1, 12));
  const uint32_t day = static_cast<uint32_t>(std::clamp(st.m_day, 1, 31));
  const uint32_t hour = static_cast<uint32_t>(std::clamp(st.m_hour, 0, 23));
  const uint32_t minute = static_cast<uint32_t>(std::clamp(st.m_minute, 0, 59));
  const uint32_t second = static_cast<uint32_t>(std::clamp(st.m_second, 0, 59));
  return (year << 25) | (month << 21) | (day << 16) | (hour << 11) | (minute << 5) | (second >> 1);
}

bool statPath(const std::string& relPath, FileStat& outStat) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return false;
  if (relPath.empty()) {
    outStat = {};
    outStat.isdir = 1;
    outStat.m_year = 2026;
    outStat.m_month = 1;
    outStat.m_day = 1;
    return true;
  }
  return pd->file->stat(relPath.c_str(), &outStat) == 0;
}

void ensureParentDirectories(const std::string& relPath) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || relPath.empty()) return;
  size_t pos = 0;
  while ((pos = relPath.find('/', pos)) != std::string::npos) {
    const std::string parent = relPath.substr(0, pos);
    if (!parent.empty()) {
      pd->file->mkdir(parent.c_str());
    }
    pos++;
  }
}

void collectDirEntryCallback(const char* filename, void* userdata) {
  if (!filename || !userdata) return;
  std::string name(filename);
  while (!name.empty() && name.back() == '/') {
    name.pop_back();
  }
  if (name.empty() || name == "." || name == "..") return;
  auto* entries = static_cast<std::vector<std::string>*>(userdata);
  if (std::find(entries->begin(), entries->end(), name) == entries->end()) {
    entries->push_back(std::move(name));
  }
}

std::vector<std::string> listDirEntries(const std::string& relDirPath) {
  std::vector<std::string> entries;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return entries;
  const char* queryPath = relDirPath.empty() ? "." : relDirPath.c_str();
  pd->file->listfiles(queryPath, collectDirEntryCallback, &entries, /*showhidden=*/1);
  return entries;
}

}  // namespace

class HalStorage::StorageLock {};

class HalFile::Impl {
 public:
  std::string relPath;
  std::string baseName;
  bool isOpen = false;
  bool isDir = false;
  bool writable = false;
  uint32_t cachedSize = 0;
  uint32_t mtime = 0;
  size_t cursor = 0;

  SDFile* readHandle = nullptr;
  SDFile* writeHandle = nullptr;

  std::vector<std::string> dirEntries;
  size_t dirIndex = 0;

  ~Impl() { close(); }

  void close() {
    PlaydateAPI* pd = playdate_hal::pd();
    if (readHandle && pd && pd->file) {
      pd->file->close(readHandle);
    }
    if (writeHandle && pd && pd->file) {
      pd->file->flush(writeHandle);
      pd->file->close(writeHandle);
    }
    readHandle = nullptr;
    writeHandle = nullptr;
    isOpen = false;
    dirEntries.clear();
    dirIndex = 0;
  }

  bool ensureReadHandle() {
    if (readHandle) return true;
    PlaydateAPI* pd = playdate_hal::pd();
    if (!pd || !pd->file || relPath.empty()) return false;
    readHandle = pd->file->open(relPath.c_str(), static_cast<FileOptions>(kFileReadData | kFileRead));
    if (!readHandle) return false;
    if (cursor > 0) {
      pd->file->seek(readHandle, static_cast<int>(cursor), SEEK_SET);
    }
    return true;
  }
};

HalStorage::HalStorage() = default;

bool HalStorage::begin() {
  initialized = (playdate_hal::pd() != nullptr);
  return initialized;
}

bool HalStorage::ready() const { return initialized; }

void HalStorage::prepareForDeepSleep() {}

bool HalStorage::beginUsbDrive() { return false; }

bool HalStorage::disconnectUsbDriveHost() { return false; }

void HalStorage::endUsbDrive() {}

UsbDriveState HalStorage::usbDriveState() const { return UsbDriveState::Unsupported; }

bool HalStorage::usbDriveHostSuspended() const { return false; }

uint64_t HalStorage::sdTotalBytes() { return 4ULL * 1024ULL * 1024ULL * 1024ULL; }

bool HalStorage::sdUsedBytes(uint64_t& usedBytes) {
  usedBytes = 64ULL * 1024ULL * 1024ULL;
  return true;
}

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  std::vector<String> ret;
  HalFile root = open(path);
  if (!root || !root.isDirectory()) return ret;
  int count = 0;
  char name[256];
  for (HalFile f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    ret.push_back(String(name));
    count++;
  }
  return ret;
}

String HalStorage::readFile(const char* path) {
  HalFile f = open(path, O_RDONLY);
  if (!f || f.isDirectory()) return String("");
  const size_t sz = f.size();
  std::string content(sz, '\0');
  if (sz > 0) {
    const int n = f.read(content.data(), sz);
    if (n < 0) return String("");
    content.resize(static_cast<size_t>(n));
  }
  return String(content);
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HalFile f = open(path, O_RDONLY);
  if (!f || f.isDirectory()) return false;
  const size_t bufLen = chunkSize == 0 ? 256 : chunkSize;
  std::vector<uint8_t> buf(bufLen);
  while (f.available() > 0) {
    const int r = f.read(buf.data(), bufLen);
    if (r <= 0) break;
    out.write(buf.data(), static_cast<size_t>(r));
  }
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0) return 0;
  HalFile f = open(path, O_RDONLY);
  if (!f || f.isDirectory()) {
    buffer[0] = '\0';
    return 0;
  }
  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : std::min(maxBytes, bufferSize - 1);
  const int r = f.read(buffer, maxToRead);
  const size_t total = r > 0 ? static_cast<size_t>(r) : 0;
  buffer[total] = '\0';
  return total;
}

bool HalStorage::readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out) {
  out.clear();
  HalFile file;
  if (!openFileForRead(moduleName, path, file)) return false;
  if (file.isDirectory()) return false;
  const uint64_t size = file.fileSize64();
  if (size == 0 || size > cap) return false;
  out.resize(static_cast<size_t>(size));
  const int bytesRead = file.read(out.data(), static_cast<size_t>(size));
  if (bytesRead < 0 || static_cast<uint64_t>(bytesRead) != size) {
    out.clear();
    return false;
  }
  return true;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HalFile f = open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!f) return false;
  const size_t expected = content.length();
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(content.c_str()), expected);
  f.close();
  return written == expected;
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  if (!path) return false;
  if (exists(path)) {
    HalFile dir = open(path);
    return dir && dir.isDirectory();
  }
  return mkdir(path, true);
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || !path) return HalFile();

  const std::string relPath = normalizeRelPath(path);
  const bool wantWrite = (oflag & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;

  if (!wantWrite) {
    FileStat st{};
    if (!statPath(relPath, st)) {
      return HalFile();
    }
    auto impl = std::make_unique<HalFile::Impl>();
    impl->relPath = relPath;
    impl->baseName = extractBasename(relPath);
    impl->isDir = (st.isdir != 0);
    impl->cachedSize = st.size;
    impl->mtime = packFatDateTime(st);
    if (impl->isDir) {
      impl->isOpen = true;
      impl->dirEntries = listDirEntries(relPath);
      return HalFile(std::move(impl));
    }
    impl->readHandle = pd->file->open(relPath.c_str(), static_cast<FileOptions>(kFileReadData | kFileRead));
    if (!impl->readHandle) {
      return HalFile();
    }
    impl->isOpen = true;
    return HalFile(std::move(impl));
  }

  ensureParentDirectories(relPath);

  FileStat existingStat{};
  const bool existed = statPath(relPath, existingStat) && existingStat.isdir == 0;
  if ((oflag & O_EXCL) != 0 && existed) {
    return HalFile();
  }
  if ((oflag & O_CREAT) == 0 && !existed) {
    return HalFile();
  }

  const bool truncateFile = (oflag & O_TRUNC) != 0 || !existed;
  const FileOptions openMode = truncateFile ? kFileWrite : kFileAppend;

  SDFile* wf = pd->file->open(relPath.c_str(), openMode);
  if (!wf) {
    return HalFile();
  }

  auto impl = std::make_unique<HalFile::Impl>();
  impl->relPath = relPath;
  impl->baseName = extractBasename(relPath);
  impl->isOpen = true;
  impl->isDir = false;
  impl->writable = true;
  impl->writeHandle = wf;
  impl->cachedSize = truncateFile ? 0 : existingStat.size;
  impl->mtime = existed ? packFatDateTime(existingStat) : 0;

  if ((oflag & (O_APPEND | O_AT_END)) != 0 && !truncateFile) {
    impl->cursor = impl->cachedSize;
    pd->file->seek(wf, static_cast<int>(impl->cursor), SEEK_SET);
  } else {
    impl->cursor = 0;
    pd->file->seek(wf, 0, SEEK_SET);
  }

  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || !path) return false;
  const std::string relPath = normalizeRelPath(path);
  if (relPath.empty()) return true;
  if (pFlag) {
    ensureParentDirectories(relPath);
  }
  if (pd->file->mkdir(relPath.c_str()) == 0) return true;
  return exists(path);
}

bool HalStorage::exists(const char* path) {
  if (!path) return false;
  FileStat st{};
  return statPath(normalizeRelPath(path), st);
}

bool HalStorage::remove(const char* path) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || !path) return false;
  const std::string relPath = normalizeRelPath(path);
  if (relPath.empty()) return false;
  return pd->file->unlink(relPath.c_str(), /*recursive=*/0) == 0;
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || !oldPath || !newPath) return false;
  const std::string oldRel = normalizeRelPath(oldPath);
  const std::string newRel = normalizeRelPath(newPath);
  if (oldRel.empty() || newRel.empty()) return false;
  if (oldRel == newRel) return true;
  ensureParentDirectories(newRel);
  return pd->file->rename(oldRel.c_str(), newRel.c_str()) == 0;
}

bool HalStorage::replaceFile(const char* tmpPath, const char* path) {
  if (!tmpPath || !path) return false;
  if (exists(path)) {
    remove(path);
  }
  return rename(tmpPath, path);
}

bool HalStorage::rmdir(const char* path) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file || !path) return false;
  const std::string relPath = normalizeRelPath(path);
  if (relPath.empty()) return false;
  return pd->file->unlink(relPath.c_str(), /*recursive=*/1) == 0;
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  (void)moduleName;
  if (!exists(path)) {
    LOG_ERR(moduleName, "File does not exist: %s", path);
    return false;
  }
  file = open(path, O_RDONLY);
  if (!file) {
    LOG_ERR(moduleName, "Failed to open file for reading: %s", path);
    return false;
  }
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  (void)moduleName;
  file = open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR(moduleName, "Failed to open file for writing: %s", path);
    return false;
  }
  return true;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { return rmdir(path); }

HalFile::HalFile() : impl(std::make_unique<Impl>()) {}

HalFile::HalFile(std::unique_ptr<Impl> implIn) : impl(std::move(implIn)) {}

HalFile::~HalFile() = default;

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&&) = default;

void HalFile::flush() {
  PlaydateAPI* pd = playdate_hal::pd();
  if (impl && impl->writeHandle && pd && pd->file) {
    pd->file->flush(impl->writeHandle);
  }
}

size_t HalFile::getName(char* name, size_t len) {
  if (!name || len == 0) return 0;
  if (!impl || !impl->isOpen) {
    name[0] = '\0';
    return 0;
  }
  const size_t copyLen = std::min(len - 1, impl->baseName.size());
  std::memcpy(name, impl->baseName.data(), copyLen);
  name[copyLen] = '\0';
  return copyLen;
}

size_t HalFile::size() { return fileSize(); }

size_t HalFile::fileSize() { return (impl && impl->isOpen && !impl->isDir) ? impl->cachedSize : 0; }

uint64_t HalFile::fileSize64() { return fileSize(); }

uint32_t HalFile::modificationTime() { return (impl && impl->isOpen) ? impl->mtime : 0; }

bool HalFile::seek(size_t pos) {
  if (!impl || !impl->isOpen) return false;
  if (impl->isDir) {
    if (pos > impl->dirEntries.size()) return false;
    impl->dirIndex = pos;
    return true;
  }
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return false;
  if (impl->writeHandle) {
    pd->file->flush(impl->writeHandle);
    if (pd->file->seek(impl->writeHandle, static_cast<int>(pos), SEEK_SET) != 0) return false;
  }
  if (impl->readHandle) {
    if (pd->file->seek(impl->readHandle, static_cast<int>(pos), SEEK_SET) != 0) return false;
  }
  impl->cursor = pos;
  return true;
}

bool HalFile::seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }

bool HalFile::seekCur(int64_t offset) {
  if (!impl || !impl->isOpen) return false;
  const int64_t base = static_cast<int64_t>(position());
  const int64_t target = base + offset;
  if (target < 0) return false;
  return seek(static_cast<size_t>(target));
}

bool HalFile::seekSet(size_t offset) { return seek(offset); }

bool HalFile::truncate(uint64_t length) {
  if (!impl || !impl->isOpen || impl->isDir || !impl->writable) return false;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return false;

  if (impl->writeHandle) {
    pd->file->flush(impl->writeHandle);
    pd->file->close(impl->writeHandle);
    impl->writeHandle = nullptr;
  }
  if (impl->readHandle) {
    pd->file->close(impl->readHandle);
    impl->readHandle = nullptr;
  }

  const size_t keepLen = static_cast<size_t>(std::min<uint64_t>(length, impl->cachedSize));
  std::vector<uint8_t> kept(keepLen);
  if (keepLen > 0) {
    SDFile* rf = pd->file->open(impl->relPath.c_str(), static_cast<FileOptions>(kFileReadData | kFileRead));
    if (!rf) return false;
    const int n = pd->file->read(rf, kept.data(), static_cast<unsigned int>(keepLen));
    pd->file->close(rf);
    if (n < 0 || static_cast<size_t>(n) != keepLen) return false;
  }

  SDFile* wf = pd->file->open(impl->relPath.c_str(), kFileWrite);
  if (!wf) return false;
  if (keepLen > 0) {
    const int written = pd->file->write(wf, kept.data(), static_cast<unsigned int>(keepLen));
    if (written < 0 || static_cast<size_t>(written) != keepLen) {
      pd->file->close(wf);
      return false;
    }
  }
  pd->file->flush(wf);
  impl->writeHandle = wf;
  impl->cachedSize = static_cast<uint32_t>(keepLen);
  impl->cursor = std::min(impl->cursor, keepLen);
  pd->file->seek(impl->writeHandle, static_cast<int>(impl->cursor), SEEK_SET);
  return true;
}

int HalFile::available() const {
  if (!impl || !impl->isOpen || impl->isDir) return 0;
  if (impl->cursor >= impl->cachedSize) return 0;
  return static_cast<int>(impl->cachedSize - impl->cursor);
}

size_t HalFile::position() const {
  if (!impl || !impl->isOpen) return 0;
  return impl->isDir ? impl->dirIndex : impl->cursor;
}

int HalFile::read(void* buf, size_t count) {
  if (!impl || !impl->isOpen || impl->isDir || !buf) return -1;
  if (count == 0) return 0;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return -1;

  if (impl->writable) {
    if (impl->writeHandle) {
      pd->file->flush(impl->writeHandle);
    }
    SDFile* tempRead = pd->file->open(impl->relPath.c_str(), static_cast<FileOptions>(kFileReadData | kFileRead));
    if (!tempRead) return -1;
    if (pd->file->seek(tempRead, static_cast<int>(impl->cursor), SEEK_SET) != 0) {
      pd->file->close(tempRead);
      return -1;
    }
    const int n = pd->file->read(tempRead, buf, static_cast<unsigned int>(count));
    pd->file->close(tempRead);
    if (n > 0) {
      impl->cursor += static_cast<size_t>(n);
      if (impl->writeHandle) {
        pd->file->seek(impl->writeHandle, static_cast<int>(impl->cursor), SEEK_SET);
      }
    }
    return n;
  }

  if (!impl->ensureReadHandle()) return -1;
  const int n = pd->file->read(impl->readHandle, buf, static_cast<unsigned int>(count));
  if (n > 0) {
    impl->cursor += static_cast<size_t>(n);
  }
  return n;
}

int HalFile::read() {
  uint8_t b = 0;
  return read(&b, 1) == 1 ? static_cast<int>(b) : -1;
}

size_t HalFile::write(const uint8_t* buf, size_t count) {
  if (!impl || !impl->isOpen || impl->isDir || !impl->writeHandle || !buf) return 0;
  if (count == 0) return 0;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->file) return 0;
  const int written = pd->file->write(impl->writeHandle, buf, static_cast<unsigned int>(count));
  if (written <= 0) return 0;
  impl->cursor += static_cast<size_t>(written);
  if (impl->cursor > impl->cachedSize) {
    impl->cachedSize = static_cast<uint32_t>(impl->cursor);
  }
  return static_cast<size_t>(written);
}

size_t HalFile::write(const void* buf, size_t count) { return write(static_cast<const uint8_t*>(buf), count); }

size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::rename(const char* newPath) {
  if (!impl || !impl->isOpen || !newPath) return false;
  const std::string oldRel = impl->relPath;
  impl->close();
  return Storage.rename(oldRel.c_str(), newPath);
}

bool HalFile::isDirectory() const { return impl && impl->isOpen && impl->isDir; }

void HalFile::rewindDirectory() {
  if (impl && impl->isOpen && impl->isDir) {
    impl->dirIndex = 0;
  }
}

bool HalFile::close() {
  if (!impl) return true;
  impl->close();
  return true;
}

HalFile HalFile::openNextFile() {
  if (!impl || !impl->isOpen || !impl->isDir) return HalFile();
  while (impl->dirIndex < impl->dirEntries.size()) {
    const std::string& entryName = impl->dirEntries[impl->dirIndex++];
    const std::string childRelPath = impl->relPath.empty() ? entryName : (impl->relPath + "/" + entryName);
    FileStat st{};
    if (!statPath(childRelPath, st)) continue;

    auto child = std::make_unique<Impl>();
    child->relPath = childRelPath;
    child->baseName = entryName;
    child->isOpen = true;
    child->isDir = (st.isdir != 0);
    child->cachedSize = st.size;
    child->mtime = packFatDateTime(st);
    if (child->isDir) {
      child->dirEntries = listDirEntries(childRelPath);
    }
    return HalFile(std::move(child));
  }
  return HalFile();
}

bool HalFile::isOpen() const { return impl && impl->isOpen; }

HalFile::operator bool() const { return isOpen(); }

#include "MediaManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "DisplayConfig.h"
#include "DisplayManager.h"
#include "SyncGuards.h"

namespace {
constexpr char kSavedImagePath[] = "/saved_image.jpg";
constexpr char kAnimationDirectory[] = "/animation";
constexpr char kAnimationManifest[] = "/animation/manifest.json";
constexpr uint16_t kAnimationWidth = DisplayConfig::kWidth;
constexpr uint16_t kAnimationHeight = DisplayConfig::kHeight;

class MediaLock {
public:
    explicit MediaLock(SemaphoreHandle_t mutex, TickType_t ticks = pdMS_TO_TICKS(1000))
        : _mutex(mutex) {
        if (_mutex != nullptr) {
            _acquired = (xSemaphoreTakeRecursive(_mutex, ticks) == pdTRUE);
        } else {
            _acquired = true;
        }
    }
    ~MediaLock() {
        if (_acquired && _mutex != nullptr) {
            xSemaphoreGiveRecursive(_mutex);
        }
    }
    bool locked() const { return _acquired; }

private:
    SemaphoreHandle_t _mutex;
    bool _acquired = false;
};
} // namespace

MediaManager::MediaManager(DisplayManager* display) : _display(display) {}

MediaManager::~MediaManager() {
    clearRamCache();
    if (_streamStagingBuffer != nullptr) {
        free(_streamStagingBuffer);
        _streamStagingBuffer = nullptr;
    }
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

void MediaManager::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateRecursiveMutex();
    }
    MediaLock lock(_mutex);
    _streamFrameCount.store(0, std::memory_order_release);
    _testImageActive.store(false, std::memory_order_release);

    {
        FsLock fsLock;
        if (!fsLock.locked() || !LittleFS.begin(true)) {
            Serial.println("LittleFS mount failed in MediaManager");
            return;
        }
        if (LittleFS.exists(kSavedImagePath)) {
            _testImageActive.store(true, std::memory_order_release);
        }
    }

    String error;
    if (!reloadAnimation(&error) && error.length()) {
        Serial.printf("Animation unavailable: %s\n", error.c_str());
    } else if (_animationValid) {
        cacheFramesInRam();
    }
}

void MediaManager::loop(bool navigationActive) {
    if (navigationActive) {
        // Navigation always has priority over a manually uploaded test image.
        _testImageActive.store(false, std::memory_order_release);
        _wasNavigationActive = true;
        _playbackStartMs = 0;
        _lastRenderedFrame = 0xFFFF;
        return;
    }
    if (_testImageActive.load(std::memory_order_acquire)) return;

    uint16_t targetIndex = 0;
    const uint8_t* ramFrameData = nullptr;
    size_t ramFrameLen = 0;
    String flashFramePath;

    {
        MediaLock lock(_mutex, pdMS_TO_TICKS(15));
        if (!lock.locked()) return;
        if (static_cast<int32_t>(millis() - _externalStreamUntilMs) < 0) return;
        if (!_animationValid || _display == nullptr || _animationFrames == 0 || _animationFps == 0) return;

        const uint32_t now = millis();
        if (_wasNavigationActive || _playbackStartMs == 0 || now < _playbackStartMs) {
            _wasNavigationActive = false;
            _playbackStartMs = now;
            _lastRenderedFrame = 0xFFFF;
        }

        const uint32_t totalDurationMs = (_animationFrames * 1000UL) / _animationFps;
        if (totalDurationMs == 0) return;

        const uint32_t elapsedMs = now - _playbackStartMs;
        const uint32_t loopTimeMs = elapsedMs % totalDurationMs;
        targetIndex = (loopTimeMs * _animationFps) / 1000UL;
        if (targetIndex >= _animationFrames) {
            targetIndex = _animationFrames - 1;
        }

        if (targetIndex == _lastRenderedFrame) {
            return;
        }

        if (_ramCacheActive && _cachedFrames[targetIndex].data != nullptr) {
            ramFrameData = _cachedFrames[targetIndex].data;
            ramFrameLen = _cachedFrames[targetIndex].length;
        } else {
            flashFramePath = framePath(targetIndex);
        }

        // Draw while holding _mutex if reading from PSRAM cache so cache cannot be freed mid-decode
        if (ramFrameData != nullptr) {
            _display->drawJpeg(ramFrameData, ramFrameLen);
            _lastRenderedFrame = targetIndex;
            _playbackIndex = targetIndex;
            return;
        }
    }

    // Flash fallback outside MediaLock before acquiring FsLock (avoid holding MediaLock over long SPI Flash read)
    if (flashFramePath.length() > 0) {
        if (!_display->drawJpegFile(flashFramePath)) {
            Serial.printf("Failed to draw animation frame %u\n", targetIndex);
        } else {
            MediaLock lock(_mutex, pdMS_TO_TICKS(15));
            if (lock.locked()) {
                _lastRenderedFrame = targetIndex;
                _playbackIndex = targetIndex;
            }
        }
    }
}

bool MediaManager::queueStreamJpeg(const uint8_t* data, size_t length) {
    if (data == nullptr || length < 128 || length > kMaximumTestImageBytes) {
        return false;
    }
    MediaLock lock(_mutex);
    if (!lock.locked()) return false;

    if (_streamStagingBuffer == nullptr) {
        _streamStagingBuffer = static_cast<uint8_t*>(ps_malloc(kMaximumTestImageBytes));
        if (_streamStagingBuffer == nullptr) {
            _streamStagingBuffer = static_cast<uint8_t*>(malloc(kMaximumTestImageBytes));
        }
        if (_streamStagingBuffer == nullptr) {
            return false;
        }
    }
    memcpy(_streamStagingBuffer, data, length);
    _streamStagingLength = length;
    _streamStagingPending = true;
    _testImageActive.store(false, std::memory_order_release);
    _streamFrameCount.fetch_add(1, std::memory_order_relaxed);
    _externalStreamUntilMs = millis() + 750;
    UiAction action{};
    action.type = UiActionType::DrawStreamFrame;
    return UiActionQueue::push(action);
}

bool MediaManager::drawPendingStreamFrame() {
    if (_display == nullptr) return false;
    MediaLock lock(_mutex, pdMS_TO_TICKS(50));
    if (!lock.locked() || !_streamStagingPending || _streamStagingBuffer == nullptr || _streamStagingLength == 0) {
        return false;
    }
    _streamStagingPending = false;
    return _display->drawJpeg(_streamStagingBuffer, _streamStagingLength);
}

bool MediaManager::saveTestImageOnly(const uint8_t* data, size_t length) {
    if (data == nullptr || length < 128 || length > kMaximumTestImageBytes) {
        return false;
    }
    if (data[0] != 0xFF || data[1] != 0xD8 || data[length - 2] != 0xFF || data[length - 1] != 0xD9) {
        return false;
    }

    {
        FsLock fsLock;
        if (!fsLock.locked()) return false;
        File file = LittleFS.open(kSavedImagePath, "w");
        if (!file) {
            Serial.println("Failed to open LittleFS file to save test image");
            return false;
        }
        const size_t written = file.write(data, length);
        file.close();
        if (written != length) {
            return false;
        }
        Serial.printf("Saved test image to %s (%u bytes)\n", kSavedImagePath, length);
    }

    MediaLock lock(_mutex);
    if (lock.locked()) {
        _streamFrameCount.fetch_add(1, std::memory_order_relaxed);
        _externalStreamUntilMs = 0;
        _testImageActive.store(true, std::memory_order_release);
    }
    UiAction action{};
    action.type = UiActionType::ShowSavedImage;
    return UiActionQueue::push(action);
}

bool MediaManager::handleJpeg(const uint8_t* data, size_t length) {
    if (_display == nullptr) return false;
    const bool ok = _display->drawJpeg(data, length);
    if (ok) {
        MediaLock lock(_mutex);
        _testImageActive.store(false, std::memory_order_release);
        _streamFrameCount.fetch_add(1, std::memory_order_relaxed);
        _externalStreamUntilMs = millis() + 750;
    }
    return ok;
}

bool MediaManager::showTestImage(const uint8_t* data, size_t length) {
    if (_display == nullptr || data == nullptr || length < 128 ||
        length > kMaximumTestImageBytes) {
        return false;
    }

    const bool ok = _display->drawJpeg(data, length);
    if (ok) {
        MediaLock lock(_mutex);
        _streamFrameCount.fetch_add(1, std::memory_order_relaxed);
        _externalStreamUntilMs = 0;
        _testImageActive.store(true, std::memory_order_release);
        FsLock fsLock;
        if (fsLock.locked()) {
            File file = LittleFS.open(kSavedImagePath, "w");
            if (file) {
                file.write(data, length);
                file.close();
                Serial.printf("Saved test image to %s (%u bytes)\n", kSavedImagePath, length);
            } else {
                Serial.println("Failed to open LittleFS file to save test image");
            }
        }
    }
    return ok;
}

void MediaManager::clearTestImage() {
    _testImageActive.store(false, std::memory_order_release);
    FsLock fsLock;
    if (fsLock.locked() && LittleFS.exists(kSavedImagePath)) {
        LittleFS.remove(kSavedImagePath);
        Serial.printf("Removed %s\n", kSavedImagePath);
    }
}

bool MediaManager::hasTestImage() const { return _testImageActive.load(std::memory_order_acquire); }

bool MediaManager::hasSavedImage() const {
    FsLock fsLock(pdMS_TO_TICKS(200));
    if (!fsLock.locked()) return false;
    return LittleFS.exists(kSavedImagePath);
}

bool MediaManager::showSavedImage() {
    if (_display == nullptr || !hasSavedImage()) {
        return false;
    }
    const bool ok = _display->drawJpegFile(kSavedImagePath);
    if (ok) {
        MediaLock lock(_mutex);
        _testImageActive.store(true, std::memory_order_release);
        _externalStreamUntilMs = 0;
    }
    return ok;
}

void MediaManager::setTestImageActive(bool active) {
    _testImageActive.store(active, std::memory_order_release);
}

uint32_t MediaManager::frameCount() const { return _streamFrameCount.load(std::memory_order_acquire); }

bool MediaManager::hasAnimation() const {
    MediaLock lock(_mutex, pdMS_TO_TICKS(100));
    return lock.locked() ? _animationValid : false;
}

uint16_t MediaManager::animationFrameCount() const {
    MediaLock lock(_mutex, pdMS_TO_TICKS(100));
    return lock.locked() ? _animationFrames : 0;
}

uint8_t MediaManager::animationFps() const {
    MediaLock lock(_mutex, pdMS_TO_TICKS(100));
    return lock.locked() ? _animationFps : 0;
}

uint32_t MediaManager::animationBytes() const {
    MediaLock lock(_mutex, pdMS_TO_TICKS(100));
    return lock.locked() ? _animationTotalBytes : 0;
}

MediaManager::MediaStats MediaManager::statsSnapshot() const {
    MediaStats stats{};
    stats.streamFrameCount = _streamFrameCount.load(std::memory_order_acquire);
    stats.testImageActive = _testImageActive.load(std::memory_order_acquire);
    MediaLock lock(_mutex, pdMS_TO_TICKS(100));
    if (lock.locked()) {
        stats.animationValid = _animationValid;
        stats.animationFrames = _animationFrames;
        stats.animationFps = _animationFps;
        stats.animationBytes = _animationTotalBytes;
    }
    return stats;
}

String MediaManager::framePath(uint16_t index) const {
    char path[32];
    snprintf(path, sizeof(path), "/animation/f%03u.jpg", index);
    return String(path);
}

bool MediaManager::removeAnimationFiles(String* /*error*/) {
    FsLock fsLock;
    if (!fsLock.locked()) return false;
    if (_uploadFile) _uploadFile.close();

    // Directly remove files by index.
    // NEVER use openNextFile() while deleting in LittleFS as it corrupts the internal directory iterator.
    uint16_t count = _animationFrames;
    if (count == 0 && LittleFS.exists(kAnimationManifest)) {
        File manifest = LittleFS.open(kAnimationManifest, "r");
        if (manifest) {
            JsonDocument doc;
            if (!deserializeJson(doc, manifest)) {
                count = doc["frames"] | 0;
            }
            manifest.close();
        }
    }
    if (count == 0 || count > kMaximumFrames) {
        count = kMaximumFrames;
    }

    for (uint16_t i = 0; i < count; ++i) {
        LittleFS.remove(framePath(i));
    }
    if (LittleFS.exists(kAnimationManifest)) {
        LittleFS.remove(kAnimationManifest);
    }
    return true;
}

bool MediaManager::clearAnimation(String* error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        if (error != nullptr) *error = "media_busy";
        return false;
    }
    clearRamCache();
    removeAnimationFiles(error);
    _animationValid = false;
    _animationFrames = 0;
    _animationFps = 0;
    _animationTotalBytes = 0;
    _playbackIndex = 0;
    _playbackStartMs = 0;
    _lastRenderedFrame = 0xFFFF;
    _nextFrameAtMs = 0;
    _uploadId = "";
    memset(_uploadMask, 0, sizeof(_uploadMask));
    _uploadTotalBytes = 0;
    return true;
}

bool MediaManager::beginAnimationUpload(uint16_t frameCount,
                                        uint8_t fps,
                                        String& uploadId,
                                        String& error) {
    if (frameCount == 0 || frameCount > kMaximumFrames || fps == 0 || fps > kMaximumFps) {
        error = "invalid_animation_settings";
        return false;
    }
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        error = "media_busy";
        return false;
    }
    clearTestImage();
    if (!clearAnimation(&error)) return false;

    {
        FsLock fsLock;
        if (!fsLock.locked() ||
            (!LittleFS.exists(kAnimationDirectory) && !LittleFS.mkdir(kAnimationDirectory))) {
            error = "animation_directory_failed";
            return false;
        }
    }

    _uploadExpectedFrames = frameCount;
    _uploadFps = fps;
    memset(_uploadMask, 0, sizeof(_uploadMask));
    _uploadTotalBytes = 0;
    _uploadId = String(esp_random(), HEX) + String(millis(), HEX);
    uploadId = _uploadId;
    return true;
}

bool MediaManager::beginAnimationFrame(const String& uploadId,
                                       uint16_t index,
                                       String& error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        error = "media_busy";
        return false;
    }
    if (uploadId.length() == 0 || uploadId != _uploadId) {
        error = "invalid_upload_id";
        return false;
    }
    if (index >= _uploadExpectedFrames) {
        error = "invalid_frame_index";
        return false;
    }

    FsLock fsLock;
    if (!fsLock.locked()) {
        error = "fs_busy";
        return false;
    }
    if (_uploadFile) _uploadFile.close();
    _uploadFramePath = framePath(index);
    _uploadFile = LittleFS.open(_uploadFramePath, "w");
    if (!_uploadFile) {
        error = "frame_open_failed";
        return false;
    }
    _uploadFrameIndex = index;
    _uploadFrameBytes = 0;
    _uploadFirstBytes[0] = _uploadFirstBytes[1] = 0;
    _uploadLastBytes[0] = _uploadLastBytes[1] = 0;
    return true;
}

bool MediaManager::writeAnimationFrameChunk(const uint8_t* data,
                                            size_t length,
                                            String& error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        error = "media_busy";
        return false;
    }
    FsLock fsLock;
    if (!fsLock.locked()) {
        error = "fs_busy";
        return false;
    }
    if (!_uploadFile || data == nullptr || length == 0) {
        error = "frame_not_started";
        return false;
    }
    if (_uploadFrameBytes + length > kMaximumFrameBytes ||
        _uploadTotalBytes + _uploadFrameBytes + length > kMaximumAnimationBytes) {
        error = "animation_too_large";
        return false;
    }

    for (size_t index = 0; index < length; ++index) {
        if (_uploadFrameBytes + index < 2) {
            _uploadFirstBytes[_uploadFrameBytes + index] = data[index];
        }
        _uploadLastBytes[0] = _uploadLastBytes[1];
        _uploadLastBytes[1] = data[index];
    }
    if (_uploadFile.write(data, length) != length) {
        error = "frame_write_failed";
        return false;
    }
    _uploadFrameBytes += length;
    return true;
}

bool MediaManager::finishAnimationFrame(String& error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        error = "media_busy";
        return false;
    }
    FsLock fsLock;
    if (!fsLock.locked()) {
        error = "fs_busy";
        return false;
    }
    if (!_uploadFile) {
        error = "frame_not_started";
        return false;
    }
    _uploadFile.close();
    if (_uploadFrameBytes < 128 ||
        _uploadFirstBytes[0] != 0xFF || _uploadFirstBytes[1] != 0xD8 ||
        _uploadLastBytes[0] != 0xFF || _uploadLastBytes[1] != 0xD9) {
        LittleFS.remove(_uploadFramePath);
        error = "invalid_jpeg_frame";
        return false;
    }

    const size_t byteIdx = _uploadFrameIndex / 8;
    const uint8_t bit = 1 << (_uploadFrameIndex % 8);
    if ((_uploadMask[byteIdx] & bit) == 0) {
        _uploadMask[byteIdx] |= bit;
        _uploadTotalBytes += _uploadFrameBytes;
    }
    return true;
}

void MediaManager::abortAnimationFrame() {
    MediaLock lock(_mutex);
    if (!lock.locked()) return;
    FsLock fsLock;
    if (!fsLock.locked()) return;
    if (_uploadFile) _uploadFile.close();
    if (_uploadFramePath.length()) LittleFS.remove(_uploadFramePath);
}

bool MediaManager::commitAnimationUpload(const String& uploadId, String& error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        error = "media_busy";
        return false;
    }
    if (uploadId != _uploadId || _uploadExpectedFrames == 0) {
        error = "invalid_upload_id";
        return false;
    }
    for (uint16_t i = 0; i < _uploadExpectedFrames; ++i) {
        if ((_uploadMask[i / 8] & (1 << (i % 8))) == 0) {
            error = "animation_frames_missing";
            return false;
        }
    }

    {
        FsLock fsLock;
        if (!fsLock.locked()) {
            error = "fs_busy";
            return false;
        }
        JsonDocument document;
        document["version"] = 1;
        document["width"] = kAnimationWidth;
        document["height"] = kAnimationHeight;
        document["fps"] = _uploadFps;
        document["frames"] = _uploadExpectedFrames;
        document["total_bytes"] = _uploadTotalBytes;
        File manifest = LittleFS.open(kAnimationManifest, "w");
        if (!manifest || serializeJson(document, manifest) == 0) {
            if (manifest) manifest.close();
            error = "manifest_write_failed";
            return false;
        }
        manifest.close();
    }

    _uploadId = "";
    const bool ok = reloadAnimation(&error);
    if (ok) {
        cacheFramesInRam();
    }
    return ok;
}

bool MediaManager::reloadAnimation(String* error) {
    MediaLock lock(_mutex);
    if (!lock.locked()) {
        if (error != nullptr) *error = "media_busy";
        return false;
    }
    _animationValid = false;

    uint16_t frames = 0;
    uint8_t fps = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t totalBytes = 0;

    {
        FsLock fsLock;
        if (!fsLock.locked()) {
            if (error != nullptr) *error = "fs_busy";
            return false;
        }
        if (!LittleFS.exists(kAnimationManifest)) return false;
        File manifest = LittleFS.open(kAnimationManifest, "r");
        if (!manifest) {
            if (error != nullptr) *error = "manifest_open_failed";
            return false;
        }
        JsonDocument document;
        const DeserializationError jsonError = deserializeJson(document, manifest);
        manifest.close();
        if (jsonError) {
            if (error != nullptr) *error = "manifest_invalid";
            return false;
        }

        frames = document["frames"] | 0;
        fps = document["fps"] | 0;
        width = document["width"] | 0;
        height = document["height"] | 0;
        totalBytes = document["total_bytes"] | 0;
        if (frames == 0 || frames > kMaximumFrames || fps == 0 || fps > kMaximumFps ||
            width != kAnimationWidth || height != kAnimationHeight ||
            totalBytes > kMaximumAnimationBytes) {
            if (error != nullptr) *error = "manifest_values_invalid";
            return false;
        }
        for (uint16_t index = 0; index < frames; ++index) {
            if (!LittleFS.exists(framePath(index))) {
                if (error != nullptr) *error = "animation_frame_missing";
                return false;
            }
        }
    }

    _animationFrames = frames;
    _animationFps = fps;
    _animationTotalBytes = totalBytes;
    _animationValid = true;
    _playbackIndex = 0;
    _playbackStartMs = 0;
    _lastRenderedFrame = 0xFFFF;
    _nextFrameAtMs = 0;
    Serial.printf("Animation ready: %u frames @ %u fps, %lu bytes\n",
                  _animationFrames,
                  _animationFps,
                  static_cast<unsigned long>(_animationTotalBytes));
    return true;
}

void MediaManager::cacheFramesInRam() {
    MediaLock lock(_mutex);
    if (!lock.locked()) return;
    clearRamCache();
    if (!_animationValid || _animationFrames == 0) return;

    const size_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
    const size_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[MEDIA] Preloading animation (%lu bytes, %u frames @ %u fps)... Free PSRAM: %u, Free Heap: %u\n",
                  static_cast<unsigned long>(_animationTotalBytes),
                  _animationFrames,
                  _animationFps,
                  static_cast<unsigned int>(freePsram),
                  static_cast<unsigned int>(freeHeap));

    if (freePsram < (_animationTotalBytes + 256000) && freeHeap < (_animationTotalBytes + 50000)) {
        Serial.printf("[MEDIA] Animation (%lu bytes) not cached in RAM (insufficient memory), playing from flash\n",
                      static_cast<unsigned long>(_animationTotalBytes));
        return;
    }

    FsLock fsLock;
    if (!fsLock.locked()) return;

    bool allAllocated = true;
    for (uint16_t i = 0; i < _animationFrames; ++i) {
        const String path = framePath(i);
        File f = LittleFS.open(path, "r");
        if (!f) {
            allAllocated = false;
            break;
        }
        const size_t size = f.size();
        uint8_t* buf = nullptr;
        if (psramFound() && freePsram >= (_animationTotalBytes + 128000)) {
            buf = static_cast<uint8_t*>(ps_malloc(size));
        }
        if (buf == nullptr) {
            buf = static_cast<uint8_t*>(malloc(size));
        }
        if (!buf) {
            f.close();
            allAllocated = false;
            break;
        }
        if (f.read(buf, size) != size) {
            free(buf);
            f.close();
            allAllocated = false;
            break;
        }
        f.close();
        _cachedFrames[i].data = buf;
        _cachedFrames[i].length = size;
    }

    if (allAllocated) {
        _ramCacheActive = true;
        Serial.printf("[MEDIA] Animation PSRAM cache READY: %u frames, %lu bytes preloaded. Ultra-smooth %u FPS playback active!\n",
                      _animationFrames, static_cast<unsigned long>(_animationTotalBytes), _animationFps);
    } else {
        clearRamCache();
        Serial.println("[MEDIA] RAM cache allocation incomplete, falling back to flash.");
    }
}

void MediaManager::clearRamCache() {
    MediaLock lock(_mutex);
    if (!lock.locked()) return;
    _ramCacheActive = false;
    for (uint16_t i = 0; i < kMaximumFrames; ++i) {
        if (_cachedFrames[i].data != nullptr) {
            free(_cachedFrames[i].data);
            _cachedFrames[i].data = nullptr;
            _cachedFrames[i].length = 0;
        }
    }
}

#include "MediaManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "DisplayConfig.h"
#include "DisplayManager.h"

namespace {
constexpr char kSavedImagePath[] = "/saved_image.jpg";
constexpr char kAnimationDirectory[] = "/animation";
constexpr char kAnimationManifest[] = "/animation/manifest.json";
constexpr uint16_t kAnimationWidth = DisplayConfig::kWidth;
constexpr uint16_t kAnimationHeight = DisplayConfig::kHeight;
}

MediaManager::MediaManager(DisplayManager* display) : _display(display) {}

MediaManager::~MediaManager() {
    clearRamCache();
}

void MediaManager::begin() {
    _streamFrameCount = 0;
    _testImageActive = false;
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed in MediaManager");
        return;
    }
    if (LittleFS.exists(kSavedImagePath)) {
        _testImageActive = true;
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
        _testImageActive = false;
        _wasNavigationActive = true;
        return;
    }
    if (_testImageActive) return;
    if (static_cast<int32_t>(millis() - _externalStreamUntilMs) < 0) return;
    if (!_animationValid || _display == nullptr) return;

    if (_wasNavigationActive) {
        _wasNavigationActive = false;
        _nextFrameAtMs = 0;
    }
    const uint32_t now = millis();
    if (_nextFrameAtMs != 0 && static_cast<int32_t>(now - _nextFrameAtMs) < 0) return;

    if (_ramCacheActive && _cachedFrames[_playbackIndex].data != nullptr) {
        _display->drawJpeg(_cachedFrames[_playbackIndex].data, _cachedFrames[_playbackIndex].length);
    } else {
        if (!_display->drawJpegFile(framePath(_playbackIndex))) {
            Serial.printf("Failed to draw animation frame %u\n", _playbackIndex);
        }
    }
    _playbackIndex = (_playbackIndex + 1) % _animationFrames;

    const uint32_t interval = 1000UL / _animationFps;
    if (_nextFrameAtMs == 0) {
        _nextFrameAtMs = now + interval;
    } else {
        _nextFrameAtMs += interval;
        if (static_cast<int32_t>(now - _nextFrameAtMs) > static_cast<int32_t>(interval)) {
            _nextFrameAtMs = now + interval;
        }
    }
}

bool MediaManager::handleJpeg(const uint8_t* data, size_t length) {
    if (_display == nullptr) return false;
    const bool ok = _display->drawJpeg(data, length);
    if (ok) {
        _testImageActive = false;
        ++_streamFrameCount;
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
        ++_streamFrameCount;
        _externalStreamUntilMs = 0;
        _testImageActive = true;
        File file = LittleFS.open(kSavedImagePath, "w");
        if (file) {
            file.write(data, length);
            file.close();
            Serial.printf("Saved test image to %s (%u bytes)\n", kSavedImagePath, length);
        } else {
            Serial.println("Failed to open LittleFS file to save test image");
        }
    }
    return ok;
}

void MediaManager::clearTestImage() {
    _testImageActive = false;
    if (LittleFS.exists(kSavedImagePath)) {
        LittleFS.remove(kSavedImagePath);
        Serial.printf("Removed %s\n", kSavedImagePath);
    }
}

bool MediaManager::hasTestImage() const { return _testImageActive; }

bool MediaManager::hasSavedImage() const {
    return LittleFS.exists(kSavedImagePath);
}

bool MediaManager::showSavedImage() {
    if (_display == nullptr || !LittleFS.exists(kSavedImagePath)) {
        return false;
    }
    const bool ok = _display->drawJpegFile(kSavedImagePath);
    if (ok) {
        _testImageActive = true;
        _externalStreamUntilMs = 0;
    }
    return ok;
}

void MediaManager::setTestImageActive(bool active) {
    _testImageActive = active;
}

uint32_t MediaManager::frameCount() const { return _streamFrameCount; }
bool MediaManager::hasAnimation() const { return _animationValid; }
uint16_t MediaManager::animationFrameCount() const { return _animationFrames; }
uint8_t MediaManager::animationFps() const { return _animationFps; }
uint32_t MediaManager::animationBytes() const { return _animationTotalBytes; }

String MediaManager::framePath(uint16_t index) const {
    char path[32];
    snprintf(path, sizeof(path), "/animation/f%03u.jpg", index);
    return String(path);
}

bool MediaManager::removeAnimationFiles(String* error) {
    if (_uploadFile) _uploadFile.close();
    bool ok = true;
    for (uint16_t index = 0; index < kMaximumFrames; ++index) {
        const String path = framePath(index);
        if (LittleFS.exists(path) && !LittleFS.remove(path)) ok = false;
    }
    if (LittleFS.exists(kAnimationManifest) && !LittleFS.remove(kAnimationManifest)) ok = false;
    if (!ok && error != nullptr) *error = "animation_delete_failed";
    return ok;
}

bool MediaManager::clearAnimation(String* error) {
    clearRamCache();
    _animationValid = false;
    _animationFrames = 0;
    _animationFps = 0;
    _animationTotalBytes = 0;
    _playbackIndex = 0;
    _nextFrameAtMs = 0;
    _uploadId = "";
    _uploadMask = 0;
    _uploadTotalBytes = 0;
    return removeAnimationFiles(error);
}

bool MediaManager::beginAnimationUpload(uint16_t frameCount,
                                        uint8_t fps,
                                        String& uploadId,
                                        String& error) {
    if (frameCount == 0 || frameCount > kMaximumFrames || fps == 0 || fps > kMaximumFps) {
        error = "invalid_animation_settings";
        return false;
    }
    clearTestImage();
    if (!clearAnimation(&error)) return false;
    if (!LittleFS.exists(kAnimationDirectory) && !LittleFS.mkdir(kAnimationDirectory)) {
        error = "animation_directory_failed";
        return false;
    }

    _uploadExpectedFrames = frameCount;
    _uploadFps = fps;
    _uploadMask = 0;
    _uploadTotalBytes = 0;
    _uploadId = String(esp_random(), HEX) + String(millis(), HEX);
    uploadId = _uploadId;
    return true;
}

bool MediaManager::beginAnimationFrame(const String& uploadId,
                                       uint16_t index,
                                       String& error) {
    if (uploadId.length() == 0 || uploadId != _uploadId) {
        error = "invalid_upload_id";
        return false;
    }
    if (index >= _uploadExpectedFrames) {
        error = "invalid_frame_index";
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

    const uint64_t bit = 1ULL << _uploadFrameIndex;
    if ((_uploadMask & bit) == 0) {
        _uploadMask |= bit;
        _uploadTotalBytes += _uploadFrameBytes;
    }
    return true;
}

void MediaManager::abortAnimationFrame() {
    if (_uploadFile) _uploadFile.close();
    if (_uploadFramePath.length()) LittleFS.remove(_uploadFramePath);
}

bool MediaManager::commitAnimationUpload(const String& uploadId, String& error) {
    if (uploadId != _uploadId || _uploadExpectedFrames == 0) {
        error = "invalid_upload_id";
        return false;
    }
    const uint64_t expectedMask = _uploadExpectedFrames == 64
        ? UINT64_MAX
        : ((1ULL << _uploadExpectedFrames) - 1ULL);
    if ((_uploadMask & expectedMask) != expectedMask) {
        error = "animation_frames_missing";
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
    _uploadId = "";
    const bool ok = reloadAnimation(&error);
    if (ok) {
        cacheFramesInRam();
    }
    return ok;
}

bool MediaManager::reloadAnimation(String* error) {
    _animationValid = false;
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

    const uint16_t frames = document["frames"] | 0;
    const uint8_t fps = document["fps"] | 0;
    const uint16_t width = document["width"] | 0;
    const uint16_t height = document["height"] | 0;
    const uint32_t totalBytes = document["total_bytes"] | 0;
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

    _animationFrames = frames;
    _animationFps = fps;
    _animationTotalBytes = totalBytes;
    _animationValid = true;
    _playbackIndex = 0;
    _nextFrameAtMs = 0;
    Serial.printf("Animation ready: %u frames @ %u fps, %lu bytes\n",
                  _animationFrames,
                  _animationFps,
                  static_cast<unsigned long>(_animationTotalBytes));
    return true;
}

void MediaManager::cacheFramesInRam() {
    clearRamCache();
    if (!_animationValid || _animationFrames == 0) return;

    const size_t freeHeap = ESP.getFreeHeap();
    if (_animationTotalBytes > 175000 || freeHeap < (_animationTotalBytes + 40000)) {
        Serial.printf("Animation (%lu bytes) not cached in RAM (free heap: %u), playing from flash\n",
                      static_cast<unsigned long>(_animationTotalBytes), static_cast<unsigned int>(freeHeap));
        return;
    }

    bool allAllocated = true;
    for (uint16_t i = 0; i < _animationFrames; ++i) {
        const String path = framePath(i);
        File f = LittleFS.open(path, "r");
        if (!f) {
            allAllocated = false;
            break;
        }
        const size_t size = f.size();
        uint8_t* buf = static_cast<uint8_t*>(malloc(size));
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
        Serial.printf("Animation RAM cache READY: %u frames, %lu bytes preloaded. Ultra-smooth playback active!\n",
                      _animationFrames, static_cast<unsigned long>(_animationTotalBytes));
    } else {
        clearRamCache();
        Serial.println("RAM cache allocation incomplete, falling back to flash.");
    }
}

void MediaManager::clearRamCache() {
    _ramCacheActive = false;
    for (uint16_t i = 0; i < kMaximumFrames; ++i) {
        if (_cachedFrames[i].data != nullptr) {
            free(_cachedFrames[i].data);
            _cachedFrames[i].data = nullptr;
            _cachedFrames[i].length = 0;
        }
    }
}

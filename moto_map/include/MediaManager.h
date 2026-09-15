#pragma once

#include <Arduino.h>
#include <FS.h>

class DisplayManager;

class MediaManager {
public:
    explicit MediaManager(DisplayManager* display);

    void begin();
    void loop(bool navigationActive);

    bool handleJpeg(const uint8_t* data, size_t length);
    bool showTestImage(const uint8_t* data, size_t length);
    void clearTestImage();
    bool hasTestImage() const;
    bool hasSavedImage() const;
    bool showSavedImage();
    void setTestImageActive(bool active);
    uint32_t frameCount() const;

    bool hasAnimation() const;
    uint16_t animationFrameCount() const;
    uint8_t animationFps() const;
    uint32_t animationBytes() const;

    bool beginAnimationUpload(uint16_t frameCount,
                              uint8_t fps,
                              String& uploadId,
                              String& error);
    bool beginAnimationFrame(const String& uploadId, uint16_t index, String& error);
    bool writeAnimationFrameChunk(const uint8_t* data, size_t length, String& error);
    ~MediaManager();

    bool finishAnimationFrame(String& error);
    void abortAnimationFrame();
    bool commitAnimationUpload(const String& uploadId, String& error);
    bool clearAnimation(String* error = nullptr);
    bool reloadAnimation(String* error = nullptr);

private:
    static constexpr uint16_t kMaximumFrames = 60;
    static constexpr uint8_t kMaximumFps = 30;
    static constexpr size_t kMaximumFrameBytes = 32768;
    static constexpr size_t kMaximumTestImageBytes = 128U * 1024U;
    static constexpr size_t kMaximumAnimationBytes = 1450000;

    struct CachedFrame {
        uint8_t* data = nullptr;
        size_t length = 0;
    };

    DisplayManager* _display;
    uint32_t _streamFrameCount = 0;
    uint32_t _externalStreamUntilMs = 0;
    bool _testImageActive = false;
    bool _animationValid = false;
    uint16_t _animationFrames = 0;
    uint8_t _animationFps = 0;
    uint32_t _animationTotalBytes = 0;
    uint16_t _playbackIndex = 0;
    uint32_t _nextFrameAtMs = 0;
    bool _wasNavigationActive = false;

    CachedFrame _cachedFrames[kMaximumFrames]{};
    bool _ramCacheActive = false;

    String _uploadId;
    uint16_t _uploadExpectedFrames = 0;
    uint8_t _uploadFps = 0;
    uint64_t _uploadMask = 0;
    uint32_t _uploadTotalBytes = 0;
    File _uploadFile;
    String _uploadFramePath;
    uint16_t _uploadFrameIndex = 0;
    size_t _uploadFrameBytes = 0;
    uint8_t _uploadFirstBytes[2]{};
    uint8_t _uploadLastBytes[2]{};

    String framePath(uint16_t index) const;
    bool removeAnimationFiles(String* error);
    void cacheFramesInRam();
    void clearRamCache();
};

import AVFoundation
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

@MainActor
final class MediaStreamer: ObservableObject {
    @Published private(set) var status = "Media idle"
    @Published private(set) var progress: Double = 0

    private let baseURL = URL(string: "http://192.168.4.1")!
    private let session: URLSession
    private let width = 172
    private let height = 320

    init() {
        let configuration = URLSessionConfiguration.default
        configuration.timeoutIntervalForRequest = 3
        configuration.timeoutIntervalForResource = 8
        session = URLSession(configuration: configuration)
    }

    func startMediaMode() async {
        _ = await post(path: "/mode/media", body: nil)
        status = "Media mode active"
    }

    func stopMediaMode() async {
        _ = await post(path: "/mode/idle", body: nil)
        status = "Media stopped"
    }

    func streamImage(data: Data) async {
        guard let jpeg = normalizeImage(data: data) else {
            status = "Unsupported image"
            return
        }
        await startMediaMode()
        await sendFrame(jpeg)
        status = "Image sent"
    }

    func streamFile(url: URL) async {
        let didStart = url.startAccessingSecurityScopedResource()
        defer {
            if didStart { url.stopAccessingSecurityScopedResource() }
        }

        let type = UTType(filenameExtension: url.pathExtension.lowercased())
        if type?.conforms(to: .image) == true {
            do {
                await streamImage(data: Data(contentsOf: url))
            } catch {
                status = "Could not read image"
            }
        } else if type?.conforms(to: .movie) == true || type?.conforms(to: .video) == true {
            await streamVideo(url: url)
        } else {
            status = "Choose an image or video"
        }
    }

    func streamVideo(data: Data) async {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("motomap-\(UUID().uuidString).mp4")
        do {
            try data.write(to: url, options: .atomic)
            await streamVideo(url: url)
            try? FileManager.default.removeItem(at: url)
        } catch {
            status = "Could not open video"
        }
    }

    private func streamVideo(url: URL) async {
        await startMediaMode()
        let asset = AVAsset(url: url)
        guard let durationTime = try? await asset.load(.duration) else {
            status = "Invalid video"
            return
        }
        let duration = durationTime.seconds
        guard duration.isFinite, duration > 0 else {
            status = "Invalid video"
            return
        }

        let generator = AVAssetImageGenerator(asset: asset)
        generator.appliesPreferredTrackTransform = true
        generator.maximumSize = CGSize(width: width, height: height)
        let frameInterval = 0.1 // 10 fps target; ESP32 can adapt by dropping frames.
        var current = 0.0
        status = "Streaming video"

        while current < duration {
            if Task.isCancelled { break }
            do {
                let time = CMTime(seconds: current, preferredTimescale: 600)
                let image = try generator.copyCGImage(at: time, actualTime: nil)
                if let jpeg = encode(image: image) {
                    await sendFrame(jpeg)
                }
            } catch {
                // Skip a bad frame and continue playback.
            }
            progress = min(1, current / duration)
            current += frameInterval
            try? await Task.sleep(nanoseconds: 100_000_000)
        }
        progress = 1
        status = "Video finished"
    }

    private func sendFrame(_ jpeg: Data) async {
        guard let url = URL(string: "/media/frame", relativeTo: baseURL) else { return }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("image/jpeg", forHTTPHeaderField: "Content-Type")
        request.setValue(String(jpeg.count), forHTTPHeaderField: "Content-Length")

        do {
            let (_, response) = try await session.upload(for: request, from: jpeg)
            if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
                status = "ESP32 rejected frame (\(http.statusCode))"
            }
        } catch {
            status = "Media connection failed"
        }
    }

    private func post(path: String, body: Data?) async -> Bool {
        guard let url = URL(string: path, relativeTo: baseURL) else { return false }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.httpBody = body
        do {
            let (_, response) = try await session.data(for: request)
            return (response as? HTTPURLResponse).map { (200..<300).contains($0.statusCode) } ?? false
        } catch {
            status = "ESP32 Wi-Fi unavailable"
            return false
        }
    }

    private func normalizeImage(data: Data) -> Data? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
            return nil
        }
        return encode(image: image)
    }

    private func encode(image: CGImage) -> Data? {
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bytesPerRow = width * 4
        guard let context = CGContext(
            data: nil,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: bytesPerRow,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { return nil }

        context.setFillColor(CGColor(gray: 0, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        context.interpolationQuality = .high

        let scale = max(CGFloat(width) / CGFloat(image.width), CGFloat(height) / CGFloat(image.height))
        let drawWidth = CGFloat(image.width) * scale
        let drawHeight = CGFloat(image.height) * scale
        let drawRect = CGRect(
            x: (CGFloat(width) - drawWidth) / 2,
            y: (CGFloat(height) - drawHeight) / 2,
            width: drawWidth,
            height: drawHeight
        )
        context.draw(image, in: drawRect)

        guard let outputImage = context.makeImage() else { return nil }
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            output,
            UTType.jpeg.identifier as CFString,
            1,
            nil
        ) else { return nil }
        CGImageDestinationAddImage(destination, outputImage, [
            kCGImageDestinationLossyCompressionQuality: 0.88
        ] as CFDictionary)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return output as Data
    }
}

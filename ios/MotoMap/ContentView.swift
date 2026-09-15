import MapKit
import PhotosUI
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    var body: some View {
        TabView {
            ConnectionScreen()
                .tabItem { Label("Device", systemImage: "dot.radiowaves.left.and.right") }
            NavigationScreen()
                .tabItem { Label("Navigate", systemImage: "arrow.triangle.turn.up.right.diamond") }
            MediaScreen()
                .tabItem { Label("Media", systemImage: "photo.on.rectangle") }
        }
    }
}

struct ConnectionScreen: View {
    @EnvironmentObject private var app: AppModel

    var body: some View {
        NavigationStack {
            VStack(spacing: 20) {
                Image(systemName: app.ble.isConnected ? "checkmark.circle.fill" : "antenna.radiowaves.left.and.right")
                    .font(.system(size: 64))
                    .foregroundStyle(app.ble.isConnected ? .green : .blue)
                Text(app.ble.status)
                    .multilineTextAlignment(.center)
                Button("Scan / Connect ESP32") {
                    app.ble.start()
                }
                .buttonStyle(.borderedProminent)
                Text("For media, join Wi-Fi: MotoMap-ESP32 / motomap123")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
            .padding()
            .navigationTitle("MotoMap")
        }
    }
}

struct NavigationScreen: View {
    @EnvironmentObject private var app: AppModel
    @StateObject private var search = AddressSearchModel()
    @State private var searchError: String?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    TextField("Enter address or place", text: $search.query)
                        .textFieldStyle(.roundedBorder)

                    if !search.completions.isEmpty {
                        VStack(alignment: .leading, spacing: 0) {
                            ForEach(Array(search.completions.enumerated()), id: \.offset) { _, completion in
                                Button {
                                    Task {
                                        do {
                                            app.selectedDestination = try await search.resolve(completion)
                                            search.query = completion.title + ", " + completion.subtitle
                                            search.clearCompletions()
                                        } catch {
                                            searchError = error.localizedDescription
                                        }
                                    }
                                } label: {
                                    VStack(alignment: .leading) {
                                        Text(completion.title).font(.headline)
                                        Text(completion.subtitle).font(.caption).foregroundStyle(.secondary)
                                    }
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .padding(.vertical, 8)
                                }
                                Divider()
                            }
                        }
                    }

                    Map {
                        UserAnnotation()
                        if let route = app.navigation.route {
                            MapPolyline(route.polyline)
                                .stroke(.blue, lineWidth: 5)
                        }
                        if let destination = app.selectedDestination {
                            Marker(destination.name ?? "Destination", coordinate: destination.placemark.coordinate)
                        }
                    }
                    .frame(height: 280)
                    .clipShape(RoundedRectangle(cornerRadius: 12))

                    if let destination = app.selectedDestination {
                        Label(destination.name ?? "Selected destination", systemImage: "mappin.and.ellipse")
                            .font(.headline)
                    }

                    if let packet = app.navigation.currentPacket {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(packet.maneuver.rawValue)
                                .font(.title2.bold())
                            Text("\(packet.distanceToTurnM) m · \(packet.roadName)")
                            Text(app.navigation.status)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .padding()
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(.thinMaterial)
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    } else {
                        Text(app.navigation.status)
                            .foregroundStyle(.secondary)
                    }

                    HStack {
                        Button("Start navigation") {
                            app.startNavigation()
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(app.selectedDestination == nil || !app.ble.isConnected)

                        Button("Stop") {
                            app.stopNavigation()
                        }
                        .buttonStyle(.bordered)
                    }

                    if let searchError {
                        Text(searchError).foregroundStyle(.red)
                    }
                }
                .padding()
            }
            .navigationTitle("Navigation")
        }
    }
}

struct MediaScreen: View {
    @EnvironmentObject private var app: AppModel
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var isFileImporterPresented = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                Text(app.media.status)
                    .foregroundStyle(.secondary)

                ProgressView(value: app.media.progress)
                    .opacity(app.media.progress > 0 && app.media.progress < 1 ? 1 : 0)

                PhotosPicker(selection: $selectedPhoto, matching: .any(of: [.images, .videos])) {
                    Label("Choose from Photos", systemImage: "photo.on.rectangle")
                }
                .buttonStyle(.borderedProminent)
                .onChange(of: selectedPhoto) { _, item in
                    guard let item else { return }
                    Task {
                        let isVideo = item.supportedContentTypes.contains(where: { $0.conforms(to: .movie) })
                        if let data = try? await item.loadTransferable(type: Data.self) {
                            if isVideo {
                                await app.media.streamVideo(data: data)
                            } else {
                                await app.media.streamImage(data: data)
                            }
                        }
                    }
                }

                Button {
                    isFileImporterPresented = true
                } label: {
                    Label("Choose from Files", systemImage: "folder")
                }
                .buttonStyle(.bordered)

                Text("Join the MotoMap-ESP32 Wi-Fi network before sending media.")
                    .font(.footnote)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(.secondary)
            }
            .padding()
            .navigationTitle("Media")
            .fileImporter(
                isPresented: $isFileImporterPresented,
                allowedContentTypes: [.image, .movie, .video, .data],
                allowsMultipleSelection: false
            ) { result in
                guard case .success(let urls) = result, let url = urls.first else { return }
                Task { await app.media.streamFile(url: url) }
            }
        }
    }
}

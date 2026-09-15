import SwiftUI

@main
struct MotoMapApp: App {
    @StateObject private var app = AppModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(app)
        }
    }
}


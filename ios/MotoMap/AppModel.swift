import Foundation
import MapKit

@MainActor
final class AppModel: ObservableObject {
    let ble = BLEManager()
    let navigation = NavigationEngine()
    let media = MediaStreamer()

    @Published var selectedDestination: MKMapItem?

    func startNavigation() {
        guard let selectedDestination else { return }
        Task {
            await navigation.start(destination: selectedDestination, ble: ble)
        }
    }

    func stopNavigation() {
        navigation.stop()
        Task { await media.stopMediaMode() }
    }
}


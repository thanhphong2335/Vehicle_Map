import CoreLocation
import Foundation
import MapKit

@MainActor
final class NavigationEngine: NSObject, ObservableObject {
    @Published private(set) var route: MKRoute?
    @Published private(set) var destination: MKMapItem?
    @Published private(set) var currentPacket: NavigationPacket?
    @Published private(set) var isNavigating = false
    @Published private(set) var status = "Enter a destination"

    private let locationManager = CLLocationManager()
    private var ble: BLEManager?
    private var steps: [MKRouteStep] = []
    private var stepIndex = 0
    private var sequence = 0
    private var lastRerouteAt = Date.distantPast

    override init() {
        super.init()
        locationManager.delegate = self
        locationManager.activityType = .automotiveNavigation
        locationManager.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        locationManager.distanceFilter = 5
        locationManager.pausesLocationUpdatesAutomatically = false
    }

    func start(destination: MKMapItem, ble: BLEManager) async {
        self.destination = destination
        self.ble = ble
        locationManager.requestWhenInUseAuthorization()
        locationManager.allowsBackgroundLocationUpdates = true
        locationManager.startUpdatingLocation()
        status = "Calculating route"

        guard let current = locationManager.location else {
            status = "Waiting for GPS"
            return
        }

        do {
            try await calculateRoute(from: current)
            isNavigating = true
            status = "Navigation active"
            sendPacket(state: .start, location: current)
        } catch {
            status = "Route error: \(error.localizedDescription)"
        }
    }

    func stop() {
        isNavigating = false
        locationManager.stopUpdatingLocation()
        let packet = NavigationPacket(
            state: .stop,
            maneuver: .straight,
            distanceToTurnM: 0,
            remainingDistanceM: 0,
            etaSeconds: 0,
            roadName: "",
            sequence: nextSequence()
        )
        currentPacket = packet
        ble?.send(packet)
        status = "Navigation stopped"
    }

    private func calculateRoute(from location: CLLocation) async throws {
        guard let destination else { throw NavigationError.noDestination }
        let request = MKDirections.Request()
        request.source = MKMapItem(placemark: MKPlacemark(coordinate: location.coordinate))
        request.destination = destination
        request.transportType = .automobile
        request.requestsAlternateRoutes = false

        let response = try await MKDirections(request: request).calculate()
        guard let route = response.routes.first else { throw NavigationError.noRoute }
        self.route = route
        self.steps = route.steps.filter { !$0.instructions.isEmpty }
        self.stepIndex = 0
    }

    private func update(location: CLLocation) {
        guard isNavigating, let route else { return }

        let routeDistance = distance(from: location, to: route.polyline)
        if routeDistance > 100, Date().timeIntervalSince(lastRerouteAt) > 15 {
            lastRerouteAt = Date()
            sendPacket(state: .offRoute, location: location)
            Task {
                do {
                    try await calculateRoute(from: location)
                } catch {
                    status = "Reroute failed"
                }
            }
            return
        }

        advanceStepIfNeeded(location: location)
        sendPacket(state: .update, location: location)
    }

    private func advanceStepIfNeeded(location: CLLocation) {
        guard !steps.isEmpty else { return }
        let step = steps[min(stepIndex, steps.count - 1)]
        let end = step.polyline.points()[max(0, step.polyline.pointCount - 1)].coordinate
        if location.distance(from: CLLocation(latitude: end.latitude, longitude: end.longitude)) < 35,
           stepIndex < steps.count - 1 {
            stepIndex += 1
        }
    }

    private func sendPacket(state: NavigationState, location: CLLocation) {
        guard !steps.isEmpty, let route else { return }
        let step = steps[min(stepIndex, steps.count - 1)]
        let endPoint = step.polyline.points()[max(0, step.polyline.pointCount - 1)].coordinate
        let distanceToTurn = max(0, Int(location.distance(from: CLLocation(latitude: endPoint.latitude,
                                                                            longitude: endPoint.longitude))))
        let stepRemaining = steps.dropFirst(stepIndex).reduce(0.0) { partial, routeStep in
            partial + routeStep.distance
        }
        let remaining = max(0, Int(stepRemaining))
        let eta = route.distance > 0
            ? Int((Double(remaining) / route.distance) * route.expectedTravelTime)
            : 0
        let packet = NavigationPacket(
            state: state,
            maneuver: maneuver(for: step.instructions),
            distanceToTurnM: distanceToTurn,
            remainingDistanceM: remaining,
            etaSeconds: eta,
            roadName: step.instructions,
            sequence: nextSequence()
        )
        currentPacket = packet
        ble?.send(packet)

        if state == .update && stepIndex == steps.count - 1 && distanceToTurn < 25 {
            let arrival = NavigationPacket(
                state: .arrived,
                maneuver: .arrive,
                distanceToTurnM: 0,
                remainingDistanceM: 0,
                etaSeconds: 0,
                roadName: "Arrived",
                sequence: nextSequence()
            )
            currentPacket = arrival
            ble?.send(arrival)
            isNavigating = false
            locationManager.stopUpdatingLocation()
            status = "Arrived"
        }
    }

    private func nextSequence() -> Int {
        sequence += 1
        return sequence
    }

    private func maneuver(for instruction: String) -> Maneuver {
        let value = instruction.lowercased()
        if value.contains("u-turn") || value.contains("quay đầu") || value.contains("quay dau") {
            return .uTurn
        }
        if value.contains("roundabout") || value.contains("vòng xuyến") || value.contains("vong xuyen") {
            return .roundabout
        }
        if value.contains("left") || value.contains("trái") || value.contains("trai") {
            return .left
        }
        if value.contains("right") || value.contains("phải") || value.contains("phai") {
            return .right
        }
        return .straight
    }

    private func distance(from location: CLLocation, to polyline: MKPolyline) -> CLLocationDistance {
        guard polyline.pointCount > 0 else { return .greatestFiniteMagnitude }
        let points = polyline.points()
        var minimum = CLLocationDistance.greatestFiniteMagnitude
        for index in 0..<polyline.pointCount {
            let coordinate = points[index].coordinate
            minimum = min(minimum, location.distance(from: CLLocation(latitude: coordinate.latitude,
                                                                       longitude: coordinate.longitude)))
        }
        return minimum
    }

    enum NavigationError: LocalizedError {
        case noDestination
        case noRoute

        var errorDescription: String? {
            switch self {
            case .noDestination: return "No destination selected."
            case .noRoute: return "No route was returned."
            }
        }
    }
}

extension NavigationEngine: CLLocationManagerDelegate {
    nonisolated func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        Task { @MainActor in
            if manager.authorizationStatus == .denied || manager.authorizationStatus == .restricted {
                self.status = "Location permission denied"
            }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager,
                                    didUpdateLocations locations: [CLLocation]) {
        guard let location = locations.last else { return }
        Task { @MainActor in
            self.update(location: location)
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        Task { @MainActor in
            self.status = "GPS error: \(error.localizedDescription)"
        }
    }
}

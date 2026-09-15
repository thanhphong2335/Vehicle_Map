import Foundation

enum NavigationState: String, Codable {
    case start = "START"
    case update = "UPDATE"
    case stop = "STOP"
    case arrived = "ARRIVED"
    case offRoute = "OFF_ROUTE"
}

enum Maneuver: String, Codable {
    case straight = "STRAIGHT"
    case left = "LEFT"
    case right = "RIGHT"
    case uTurn = "UTURN"
    case roundabout = "ROUNDABOUT"
    case arrive = "ARRIVE"
}

struct NavigationPacket: Codable, Equatable {
    var state: NavigationState
    var maneuver: Maneuver
    var distanceToTurnM: Int
    var remainingDistanceM: Int
    var etaSeconds: Int
    var roadName: String
    var sequence: Int

    enum CodingKeys: String, CodingKey {
        case state
        case maneuver
        case distanceToTurnM = "distance_to_turn_m"
        case remainingDistanceM = "remaining_distance_m"
        case etaSeconds = "eta_seconds"
        case roadName = "road_name"
        case sequence
    }

    func encoded() throws -> Data {
        let encoder = JSONEncoder()
        return try encoder.encode(self)
    }
}


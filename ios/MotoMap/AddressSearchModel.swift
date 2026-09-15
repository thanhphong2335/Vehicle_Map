import MapKit
import Foundation

@MainActor
final class AddressSearchModel: NSObject, ObservableObject {
    @Published var query = "" {
        didSet {
            completer.queryFragment = query
        }
    }
    @Published private(set) var completions: [MKLocalSearchCompletion] = []

    private let completer = MKLocalSearchCompleter()

    override init() {
        super.init()
        completer.delegate = self
        completer.resultTypes = [.address, .pointOfInterest]
    }

    func resolve(_ completion: MKLocalSearchCompletion) async throws -> MKMapItem {
        let request = MKLocalSearch.Request(completion: completion)
        let response = try await MKLocalSearch(request: request).start()
        guard let item = response.mapItems.first else {
            throw SearchError.noResult
        }
        return item
    }

    func clearCompletions() {
        completions = []
    }

    enum SearchError: LocalizedError {
        case noResult

        var errorDescription: String? {
            "No matching address was found."
        }
    }
}

extension AddressSearchModel: MKLocalSearchCompleterDelegate {
    nonisolated func completerDidUpdateResults(_ completer: MKLocalSearchCompleter) {
        Task { @MainActor in
            self.completions = completer.results
        }
    }

    nonisolated func completer(_ completer: MKLocalSearchCompleter,
                               didFailWithError error: Error) {
        Task { @MainActor in
            self.completions = []
        }
    }
}

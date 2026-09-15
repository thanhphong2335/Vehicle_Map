import CoreBluetooth
import Foundation

@MainActor
final class BLEManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "4D4F544F-4D41-5000-0000-000000000001")
    static let navigationUUID = CBUUID(string: "4D4F544F-4D41-5000-0000-000000000002")
    static let statusUUID = CBUUID(string: "4D4F544F-4D41-5000-0000-000000000003")

    @Published private(set) var status = "Bluetooth is starting"
    @Published private(set) var isConnected = false

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var navigationCharacteristic: CBCharacteristic?

    override init() {
        super.init()
        central = CBCentralManager(
            delegate: self,
            queue: nil,
            options: [CBCentralManagerOptionRestoreIdentifierKey: "MotoMapBLECentral"]
        )
    }

    func start() {
        guard central.state == .poweredOn else {
            status = "Bluetooth is not available"
            return
        }
        status = "Searching for MotoMap-ESP32"
        central.scanForPeripherals(withServices: [Self.serviceUUID], options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: false
        ])
    }

    func disconnect() {
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
        }
        isConnected = false
        navigationCharacteristic = nil
        status = "Disconnected"
    }

    func send(_ packet: NavigationPacket) {
        guard let peripheral, let characteristic = navigationCharacteristic else {
            status = "ESP32 is not connected"
            return
        }
        do {
            let payload = try packet.encoded()
            let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.writeWithoutResponse)
                ? .withoutResponse
                : .withResponse
            peripheral.writeValue(payload, for: characteristic, type: writeType)
        } catch {
            status = "Navigation packet error: \(error.localizedDescription)"
        }
    }
}

extension BLEManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            switch central.state {
            case .poweredOn:
                self.status = "Bluetooth ready"
            case .poweredOff:
                self.status = "Turn on Bluetooth"
                self.isConnected = false
            case .unauthorized:
                self.status = "Bluetooth permission denied"
            case .unsupported:
                self.status = "Bluetooth unsupported"
            default:
                self.status = "Bluetooth unavailable"
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    willRestoreState dict: [String: Any]) {
        Task { @MainActor in
            if let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
               let peripheral = restored.first {
                self.peripheral = peripheral
                peripheral.delegate = self
                central.connect(peripheral, options: nil)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any],
                                    rssi RSSI: NSNumber) {
        Task { @MainActor in
            self.peripheral = peripheral
            peripheral.delegate = self
            self.status = "Connecting to ESP32"
            central.stopScan()
            central.connect(peripheral, options: nil)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.status = "Discovering ESP32 services"
            peripheral.delegate = self
            peripheral.discoverServices([Self.serviceUUID])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didFailToConnect peripheral: CBPeripheral,
                                    error: Error?) {
        Task { @MainActor in
            self.isConnected = false
            self.status = "ESP32 connection failed"
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didDisconnectPeripheral peripheral: CBPeripheral,
                                    error: Error?) {
        Task { @MainActor in
            self.isConnected = false
            self.navigationCharacteristic = nil
            self.status = "ESP32 disconnected; reconnecting"
            self.start()
        }
    }
}

extension BLEManager: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverServices error: Error?) {
        Task { @MainActor in
            guard error == nil,
                  let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
                self.status = "MotoMap service not found"
                return
            }
            peripheral.discoverCharacteristics([Self.navigationUUID, Self.statusUUID], for: service)
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverCharacteristicsFor service: CBService,
                                error: Error?) {
        Task { @MainActor in
            guard error == nil else {
                self.status = "Characteristic discovery failed"
                return
            }
            self.navigationCharacteristic = service.characteristics?.first(where: {
                $0.uuid == Self.navigationUUID
            })
            if let statusCharacteristic = service.characteristics?.first(where: { $0.uuid == Self.statusUUID }) {
                peripheral.setNotifyValue(true, for: statusCharacteristic)
            }
            self.isConnected = self.navigationCharacteristic != nil
            self.status = self.isConnected ? "ESP32 connected" : "Navigation characteristic missing"
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        guard characteristic.uuid == Self.statusUUID,
              let data = characteristic.value,
              let message = String(data: data, encoding: .utf8) else { return }
        Task { @MainActor in
            self.status = "ESP32: \(message)"
        }
    }
}


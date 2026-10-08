import CoreBluetooth
import Foundation

/// iPhone is BLE central, Passport is connectable GATT peripheral.
final class BLETransport: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let serviceUUID = CBUUID(string: "8A2FA760-11F0-4FD9-9B94-6E092E60E21A")
    static let writeUUID = CBUUID(string: "8A2FA760-11F0-4FD9-9B94-6E092E60E21B")

    @Published private(set) var state = "蓝牙初始化"
    @Published private(set) var ready = false

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var writer: CBCharacteristic?
    private var waitingForAck = false
    private var queued: Data?
    private var sequence: UInt16 = 0
    private var wantsConnection = true

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil,
            options: [CBCentralManagerOptionRestoreIdentifierKey: "PassportNav.central"])
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { scan() }
        else {
            ready = false
            state = "请开启 iPhone 蓝牙"
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        if let restored = (dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral])?.first {
            peripheral = restored
            restored.delegate = self
            state = "正在恢复连接"
            if restored.state == .connected { restored.discoverServices([Self.serviceUUID]) }
            else if wantsConnection { central.connect(restored) }
        }
    }

    func scan() {
        wantsConnection = true
        guard central.state == .poweredOn else { return }
        if let p = peripheral, p.state == .connected { return }
        central.scanForPeripherals(withServices: [Self.serviceUUID],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        state = "搜索 Passport Nav"
    }

    func disconnect() {
        wantsConnection = false
        central.stopScan()
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        ready = false
        writer = nil
        queued = nil
        state = "已断开"
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        central.stopScan()
        peripheral = p
        p.delegate = self
        state = "正在连接"
        central.connect(p)
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        state = "查找导航服务"
        p.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        ready = false
        state = "连接失败"
        if wantsConnection { scan() }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral,
                        error: Error?) {
        ready = false
        writer = nil
        waitingForAck = false
        queued = nil
        state = "连接中断"
        if wantsConnection { scan() }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = p.services else {
            state = "服务发现失败"
            return
        }
        services.filter { $0.uuid == Self.serviceUUID }
            .forEach { p.discoverCharacteristics([Self.writeUUID], for: $0) }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService,
                    error: Error?) {
        guard error == nil,
              let value = s.characteristics?.first(where: {
                  $0.uuid == Self.writeUUID && $0.properties.contains(.write)
              }) else {
            state = "未找到可写导航特征"
            return
        }
        writer = value
        ready = true
        waitingForAck = false
        state = "Passport 已连接"
        flush()
    }

    func send(_ packet: NavSnapshot) {
        sequence &+= 1
        queued = packet.encode(sequence: sequence)
        flush()
    }

    private func flush() {
        guard ready, !waitingForAck, let p = peripheral, let c = writer,
              let data = queued,
              p.maximumWriteValueLength(for: .withResponse) >= data.count else { return }
        queued = nil
        waitingForAck = true
        p.writeValue(data, for: c, type: .withResponse)
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        waitingForAck = false
        if error != nil {
            state = "导航数据发送失败"
            return
        }
        flush()
    }
}

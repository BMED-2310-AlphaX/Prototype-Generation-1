//
//  BLEManager.swift
//  AlphaX
//
//  All the Bluetooth. This is the only file that imports CoreBluetooth, so the
//  UI never has to know how the radio works — it just reads @Published values.
//
//  IMPORTANT: Bluetooth does not exist in the iOS Simulator. Any BLE testing
//  has to happen on a physical iPhone connected to your Mac.
//
//  The UUIDs below must match the firmware byte for byte. Apple writes CBUUIDs
//  uppercase by convention; the comparison is case-insensitive, so the
//  lowercase versions in the .ino files are the same UUIDs.
//

import Foundation
import Combine          // ObservableObject, @Published
import CoreBluetooth

enum AlphaXBLE {
    static let service = CBUUID(string: "6E5A0001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let command = CBUUID(string: "6E5A0002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let status  = CBUUID(string: "6E5A0003-B5A3-F393-E0A9-E50E24DCCA9E")
}

/// A board seen during a scan. Keeps the RSSI so you can tell which one is on
/// your bench versus someone else's across the room.
struct DiscoveredDevice: Identifiable, Equatable {
    let id: UUID                  // peripheral.identifier
    let peripheral: CBPeripheral
    var name: String
    var rssi: Int
    var advertisesAlphaX: Bool

    static func == (a: DiscoveredDevice, b: DiscoveredDevice) -> Bool {
        a.id == b.id && a.rssi == b.rssi
    }

    /// Rough distance cue. -40 is on the bench, -90 is down the hall.
    var signalBars: String {
        switch rssi {
        case (-55)...:    return "\u{25cf}\u{25cf}\u{25cf}"
        case (-70)..<(-55): return "\u{25cf}\u{25cf}\u{25cb}"
        default:          return "\u{25cf}\u{25cb}\u{25cb}"
        }
    }
}

final class BLEManager: NSObject, ObservableObject {

    // MARK: - Published state the UI watches

    enum Link {          // implicitly Equatable: no associated values
        case bluetoothOff
        case unauthorized
        case idle
        case scanning
        case connecting
        case connected

        var label: String {
            switch self {
            case .bluetoothOff:  return "Bluetooth is off"
            case .unauthorized:  return "Bluetooth not permitted"
            case .idle:          return "Not connected"
            case .scanning:      return "Searching…"
            case .connecting:    return "Connecting…"
            case .connected:     return "Connected"
            }
        }
    }

    @Published private(set) var link: Link = .idle
    @Published private(set) var deviceName: String?
    @Published private(set) var status: DeviceStatus = .placeholder
    @Published private(set) var hrv: [HRVSample] = []
    @Published private(set) var log: [LogLine] = []

    /// Called when a device session ends, so history can be recorded.
    var onSessionEnd: ((SessionRecord) -> Void)?

    /// Every board seen in the current scan. Populated in browse mode.
    @Published private(set) var discovered: [DiscoveredDevice] = []

    /// True while browse() is running, so the UI can show the picker sheet.
    @Published var isBrowsing = false

    /// Logs every raw packet the board sends, before it's parsed. Turn this on
    /// when the firmware is behaving oddly — you see exactly what arrived.
    @Published var verboseLogging = false

    /// Synthesizes a plausible session with no hardware attached. Use it to
    /// rehearse the demo, or to build the UI before the circuit is finished.
    @Published var demoMode = false {
        didSet {
            if demoMode { startDemo() } else { stopDemo() }
        }
    }

    /// True when commands will actually go somewhere.
    var isLive: Bool { link == .connected || demoMode }

    // MARK: - Private

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var commandChar: CBCharacteristic?
    private var scanTimeout: Timer?
    private var autoConnectOnDiscover = true

    private let maxHRVPoints = 240      // ~2 min at 2 Hz
    private let maxLogLines  = 60

    // Demo-mode simulation
    // Accumulated while a session runs, flushed into a SessionRecord at the end.
    private var sessionStartedAt: Date?
    private var sessionRMSSD: [Double] = []
    private var sessionBPM: [Double] = []
    private var sessionIntensity = 0

    private var demoTimer: Timer?
    private var demoStarted: Date?
    private var demoMinutes = 20
    private var demoTargetIntensity = 0
    private var demoLiveIntensity = 0
    private var demoRMSSD: Double = 32

    override init() {
        super.init()
        // queue: .main means every delegate callback arrives on the main
        // thread, so it is safe to touch @Published properties directly.
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: - Connection

    /// Normal path: look only for boards advertising the AlphaX service and
    /// connect to the first one. This is what you use for a demo.
    func connect() {
        guard radioReady() else { return }

        autoConnectOnDiscover = true
        discovered.removeAll()
        link = .scanning
        note("Scanning for AlphaX…")

        central.scanForPeripherals(withServices: [AlphaXBLE.service])
        armScanTimeout(seconds: 12, message:
            "No AlphaX found. Board powered? Try Browse to see every nearby device.")
    }

    /// Dev path: scan for everything and list it. Use this when a board is not
    /// showing up — if it appears here but not in the filtered scan, its
    /// advertising packet is missing the service UUID, which is a firmware
    /// problem rather than a wiring one.
    func browse() {
        guard radioReady() else { return }

        autoConnectOnDiscover = false
        discovered.removeAll()
        isBrowsing = true
        link = .scanning
        note("Browsing all BLE devices…")

        central.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        armScanTimeout(seconds: 20, message: "Scan finished.")
    }

    /// Connect to a specific board picked from the browse list.
    func connect(to device: DiscoveredDevice) {
        central.stopScan()
        scanTimeout?.invalidate()
        isBrowsing = false

        peripheral = device.peripheral
        device.peripheral.delegate = self
        deviceName = device.name
        link = .connecting
        note("Connecting to \(device.name)…")

        central.connect(device.peripheral, options: nil)
    }

    func stopBrowsing() {
        central.stopScan()
        scanTimeout?.invalidate()
        isBrowsing = false
        if link == .scanning { link = .idle }
    }

    private func radioReady() -> Bool {
        guard !demoMode else { return false }

        switch central.state {
        case .poweredOn:
            return true
        case .poweredOff:
            link = .bluetoothOff
            note("Turn on Bluetooth in Settings.")
        case .unauthorized:
            link = .unauthorized
            note("Allow Bluetooth for AlphaX in Settings.")
        case .unsupported:
            // Almost always the Simulator, which has no radio to simulate.
            note("No Bluetooth radio here. On the Simulator, use Demo mode — "
               + "BLE needs a real iPhone.")
        default:
            note("Bluetooth not ready yet.")
        }
        return false
    }

    private func armScanTimeout(seconds: TimeInterval, message: String) {
        scanTimeout?.invalidate()
        scanTimeout = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { [weak self] _ in
            guard let self, self.link == .scanning else { return }
            self.central.stopScan()
            self.link = .idle
            self.note(message)
        }
    }

    func disconnect() {
        if demoMode { demoMode = false; return }
        scanTimeout?.invalidate()
        isBrowsing = false
        if central.state == .poweredOn { central.stopScan() }
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        teardown()
    }

    private func teardown() {
        peripheral  = nil
        commandChar = nil
        deviceName  = nil
        link        = .idle
        status      = .placeholder
    }

    // MARK: - Commands

    func startSession(intensity: Int, minutes: Int) {
        send("START:\(intensity):\(minutes)")
    }

    func stopSession() { send("STOP") }

    /// Adjusts output level mid-session. The firmware clamps to MAX_INTENSITY,
    /// so an out-of-range value here is safe rather than merely unlikely.
    func setIntensity(_ value: Int) {
        send("SET:\(value)")
    }

    /// Sends whatever you type, unvalidated. The serial-monitor equivalent,
    /// for poking at firmware without rebuilding the app.
    func sendRaw(_ command: String) {
        let trimmed = command.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        send(trimmed)
    }

    private func send(_ command: String) {
        if demoMode { handleDemoCommand(command); return }

        guard let p = peripheral, let c = commandChar else {
            note("Not connected.")
            return
        }
        p.writeValue(Data(command.utf8), for: c, type: .withResponse)
        note("→ \(command)")
    }

    // MARK: - Incoming status

    private func ingest(_ data: Data) {
        if verboseLogging {
            let raw = String(data: data, encoding: .utf8) ?? data.map {
                String(format: "%02x", $0)
            }.joined()
            note("← \(raw)")
        }

        guard let decoded = try? JSONDecoder().decode(DeviceStatus.self, from: data) else {
            // Most common cause: the BLE packet was truncated because the MTU
            // was never raised. Call BLEDevice::setMTU(247) in the firmware.
            note("Unreadable status packet (\(data.count) bytes).")
            return
        }
        apply(decoded)
    }

    private func apply(_ new: DeviceStatus) {
        let wasActive = status.state.isActive
        status = new

        if new.rmssd > 0 {
            hrv.append(HRVSample(time: Date(), rmssd: new.rmssd))
            if hrv.count > maxHRVPoints { hrv.removeFirst(hrv.count - maxHRVPoints) }
        }

        if new.state.isActive {
            if new.rmssd > 0 { sessionRMSSD.append(new.rmssd) }
            if new.bpm   > 0 { sessionBPM.append(new.bpm) }
            sessionIntensity = max(sessionIntensity, new.intensity)
        }

        if !wasActive && new.state.isActive {
            hrv.removeAll()                       // fresh chart per session
            sessionStartedAt = Date()
            sessionRMSSD.removeAll()
            sessionBPM.removeAll()
            sessionIntensity = new.target
            note("Session started at \(new.target)%.")
        }

        if wasActive && !new.state.isActive {
            finishSessionRecord()
            note("Session ended.")
        }
    }

    /// RMSSD change is computed as the mean of the last third of the session
    /// minus the mean of the first third, which is steadier than comparing two
    /// single samples from a noisy 12-beat window.
    private func finishSessionRecord() {
        guard let started = sessionStartedAt else { return }
        sessionStartedAt = nil

        let duration = Int(Date().timeIntervalSince(started))
        guard duration >= 10 else { return }

        var meanRMSSD: Double?
        var delta: Double?

        if sessionRMSSD.count >= 6 {
            let third = max(1, sessionRMSSD.count / 3)
            let head  = sessionRMSSD.prefix(third)
            let tail  = sessionRMSSD.suffix(third)
            let headMean = head.reduce(0, +) / Double(head.count)
            let tailMean = tail.reduce(0, +) / Double(tail.count)
            delta = tailMean - headMean
        }
        if sessionRMSSD.isEmpty == false {
            meanRMSSD = sessionRMSSD.reduce(0, +) / Double(sessionRMSSD.count)
        }

        let meanBPM: Double? = sessionBPM.isEmpty
            ? nil
            : sessionBPM.reduce(0, +) / Double(sessionBPM.count)

        onSessionEnd?(SessionRecord(
            date: Date(),
            kind: .stimulation,
            durationSec: duration,
            meanRMSSD: meanRMSSD,
            rmssdDelta: delta,
            meanBPM: meanBPM,
            intensity: sessionIntensity))
    }

    private func note(_ text: String) {
        log.insert(LogLine(time: Date(), text: text), at: 0)
        if log.count > maxLogLines { log.removeLast(log.count - maxLogLines) }
    }

    // MARK: - Demo mode

    private func startDemo() {
        teardownForDemo()
        note("Demo mode on — no hardware connected.")
        demoRMSSD = 32
        hrv.removeAll()
        status = DeviceStatus(state: .idle, intensity: 0, target: 40,
                              remaining: 0, bpm: 68, rmssd: 0, batt: 255)

        demoTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            self?.tickDemo()
        }
    }

    private func teardownForDemo() {
        scanTimeout?.invalidate()
        if central.state == .poweredOn { central.stopScan() }
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil; commandChar = nil
        deviceName = "Demo device"
        link = .idle
    }

    private func stopDemo() {
        demoTimer?.invalidate(); demoTimer = nil
        demoStarted = nil
        demoLiveIntensity = 0; demoTargetIntensity = 0
        status = .placeholder
        deviceName = nil
        note("Demo mode off.")
    }

    private func handleDemoCommand(_ command: String) {
        note("→ \(command)")
        let parts = command.split(separator: ":").map(String.init)

        switch parts.first {
        case "START":
            demoTargetIntensity = min(Int(parts.count > 1 ? parts[1] : "") ?? 40, 80)
            demoMinutes = min(Int(parts.count > 2 ? parts[2] : "") ?? 20, 60)
            demoStarted = Date()
        case "STOP":
            demoStarted = nil
            demoTargetIntensity = 0
        case "SET":
            demoTargetIntensity = min(Int(parts.count > 1 ? parts[1] : "") ?? 0, 80)
        default:
            break
        }
    }

    /// Fakes the firmware: ramps intensity, counts down, and nudges RMSSD
    /// upward the way a real parasympathetic response would look.
    private func tickDemo() {
        var phase: DeviceStatus.Phase = .idle
        var remaining = 0

        if let started = demoStarted {
            let elapsed = Date().timeIntervalSince(started)
            let total   = Double(demoMinutes * 60)
            remaining   = max(0, Int(total - elapsed))

            if remaining == 0 {
                demoStarted = nil
                demoTargetIntensity = 0
                phase = .rampingDown
            } else if elapsed < 1.5 {
                phase = .rampingUp
            } else {
                phase = .running
            }
        } else if demoLiveIntensity > 0 {
            phase = .rampingDown
        }

        // Approach the target in steps, like the firmware's ramp.
        if demoLiveIntensity < demoTargetIntensity {
            demoLiveIntensity = min(demoTargetIntensity, demoLiveIntensity + 7)
        } else if demoLiveIntensity > demoTargetIntensity {
            demoLiveIntensity = max(demoTargetIntensity, demoLiveIntensity - 7)
        }

        if phase.isActive {
            // Drifts up with a little noise — not a straight line.
            demoRMSSD += Double.random(in: -0.8...1.9)
            demoRMSSD = min(max(demoRMSSD, 22), 78)
        } else {
            demoRMSSD += Double.random(in: -1.0...0.6)
            demoRMSSD = min(max(demoRMSSD, 20), 60)
        }

        let bpm = 74 - (demoRMSSD - 32) * 0.22 + Double.random(in: -1.1...1.1)

        apply(DeviceStatus(state: phase,
                           intensity: demoLiveIntensity,
                           target: demoTargetIntensity,
                           remaining: remaining,
                           bpm: bpm,
                           rmssd: phase.isActive ? demoRMSSD : 0,
                           batt: 255))
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if link == .bluetoothOff || link == .unauthorized { link = .idle }
            note("Bluetooth ready.")
        case .poweredOff:
            link = .bluetoothOff
            teardown()
        case .unauthorized:
            link = .unauthorized
        case .unsupported:
            note("No Bluetooth radio. If this is the Simulator, that's expected "
               + "— turn on Demo mode to exercise the UI.")
        default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {

        let advertised = (advertisementData[CBAdvertisementDataServiceUUIDsKey]
                          as? [CBUUID]) ?? []
        let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String

        let device = DiscoveredDevice(
            id: peripheral.identifier,
            peripheral: peripheral,
            name: localName ?? peripheral.name ?? "Unnamed device",
            rssi: RSSI.intValue,
            advertisesAlphaX: advertised.contains(AlphaXBLE.service))

        if let i = discovered.firstIndex(where: { $0.id == device.id }) {
            discovered[i] = device
        } else {
            discovered.append(device)
            if !autoConnectOnDiscover {
                note("Saw \(device.name) (\(device.rssi) dBm)")
            }
        }
        // Strongest signal first — your bench board should be at the top.
        discovered.sort { $0.rssi > $1.rssi }

        guard autoConnectOnDiscover else { return }   // browse mode: just list

        note("Found \(device.name) — connecting.")
        connect(to: device)
    }

    func centralManager(_ central: CBCentralManager,
                        didConnect peripheral: CBPeripheral) {
        note("Connected. Discovering services…")
        peripheral.discoverServices([AlphaXBLE.service])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        note("Connection failed: \(error?.localizedDescription ?? "unknown").")
        teardown()
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        note("Disconnected — the device stops output on its own.")
        teardown()
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let service = peripheral.services?.first(where: {
            $0.uuid == AlphaXBLE.service
        }) else {
            note("AlphaX service not found on this device.")
            disconnect()
            return
        }
        peripheral.discoverCharacteristics(
            [AlphaXBLE.command, AlphaXBLE.status], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == AlphaXBLE.command {
                commandChar = c
            } else if c.uuid == AlphaXBLE.status {
                peripheral.setNotifyValue(true, for: c)
            }
        }

        guard commandChar != nil else {
            note("Command characteristic missing — check the firmware UUIDs.")
            disconnect()
            return
        }

        link = .connected
        note("Ready.")
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == AlphaXBLE.status,
              let data = characteristic.value, !data.isEmpty else { return }
        ingest(data)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error { note("Write failed: \(error.localizedDescription)") }
    }
}

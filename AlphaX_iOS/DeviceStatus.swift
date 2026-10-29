//
//  DeviceStatus.swift
//  AlphaX
//
//  The data model. Mirrors exactly what the firmware sends on the status
//  characteristic, twice a second:
//
//    {"state":"running","intensity":40,"target":40,"remaining":1183,
//     "bpm":72.4,"rmssd":41.2,"batt":255}
//
//  If you ever add a field to sendStatus() in the firmware, add it here too.
//  Decoding is lenient — a missing key won't crash the app.
//

import Foundation

struct DeviceStatus: Codable, Equatable {

    enum Phase: String, Codable {
        case idle
        case rampingUp    = "ramping_up"
        case running
        case rampingDown  = "ramping_down"
        case unknown

        var display: String {
            switch self {
            case .idle:        return "Idle"
            case .rampingUp:   return "Starting"
            case .running:     return "Running"
            case .rampingDown: return "Stopping"
            case .unknown:     return "—"
            }
        }

        /// True while the output stage is actually being driven.
        var isActive: Bool { self != .idle && self != .unknown }
    }

    var state: Phase
    var intensity: Int
    var target: Int
    var remaining: Int      // seconds
    var bpm: Double
    var rmssd: Double
    var batt: Int

    private enum CodingKeys: String, CodingKey {
        case state, intensity, target, remaining, bpm, rmssd, batt
    }

    static let placeholder = DeviceStatus(
        state: .idle, intensity: 0, target: 40,
        remaining: 0, bpm: 0, rmssd: 0, batt: 255
    )

    /// mm:ss for the big timer. Shows dashes when nothing is running.
    var timerText: String {
        guard remaining > 0 else { return "--:--" }
        return String(format: "%02d:%02d", remaining / 60, remaining % 60)
    }

    // Lenient decoding: tolerate missing or unexpected values rather than
    // throwing away the whole packet.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let raw = (try? c.decode(String.self, forKey: .state)) ?? "unknown"
        state     = Phase(rawValue: raw) ?? .unknown
        intensity = (try? c.decode(Int.self,    forKey: .intensity)) ?? 0
        target    = (try? c.decode(Int.self,    forKey: .target))    ?? 0
        remaining = (try? c.decode(Int.self,    forKey: .remaining)) ?? 0
        bpm       = (try? c.decode(Double.self, forKey: .bpm))       ?? 0
        rmssd     = (try? c.decode(Double.self, forKey: .rmssd))     ?? 0
        batt      = (try? c.decode(Int.self,    forKey: .batt))      ?? 255
    }

    init(state: Phase, intensity: Int, target: Int,
         remaining: Int, bpm: Double, rmssd: Double, batt: Int) {
        self.state = state; self.intensity = intensity; self.target = target
        self.remaining = remaining; self.bpm = bpm
        self.rmssd = rmssd; self.batt = batt
    }
}

/// One point on the RMSSD trend chart.
struct HRVSample: Identifiable, Equatable {
    let id = UUID()
    let time: Date
    let rmssd: Double
}

/// One line in the activity log.
struct LogLine: Identifiable, Equatable {
    let id = UUID()
    let time: Date
    let text: String

    var stamp: String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f.string(from: time)
    }
}

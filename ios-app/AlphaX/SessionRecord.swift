//
//  SessionRecord.swift
//  AlphaX
//
//  One completed session, and the store that keeps them.
//
//  Trends are the point: RMSSD's value as an outcome measure is in
//  within-subject change across sessions, not in any single absolute number.
//  So each record keeps the change across the session (end minus start)
//  rather than just a mean.
//
//  Persisted to UserDefaults as JSON. Fine for a prototype; a real study
//  would export to CSV or a proper store.
//

import Foundation
import Combine

struct SessionRecord: Identifiable, Codable, Equatable {

    enum Kind: String, Codable {
        case breathing
        case stimulation

        var label: String {
            self == .breathing ? "Breathing" : "Stimulation"
        }
    }

    var id = UUID()
    var date: Date
    var kind: Kind
    var durationSec: Int
    var meanRMSSD: Double?
    var rmssdDelta: Double?        // end minus start, the outcome of interest
    var meanBPM: Double?
    var intensity: Int

    var durationText: String {
        String(format: "%d:%02d", durationSec / 60, durationSec % 60)
    }

    var dateText: String {
        let f = DateFormatter()
        f.dateFormat = "MMM d, h:mm a"
        return f.string(from: date)
    }
}

final class SessionStore: ObservableObject {

    @Published private(set) var records: [SessionRecord] = []

    private let key = "alphax.sessionHistory"
    private let maxRecords = 200

    init() { load() }

    func add(_ record: SessionRecord) {
        records.append(record)
        if records.count > maxRecords {
            records.removeFirst(records.count - maxRecords)
        }
        save()
    }

    func clear() {
        records.removeAll()
        save()
    }

    // MARK: - Summary statistics

    var totalSessions: Int { records.count }

    var totalMinutes: Int {
        records.reduce(0) { $0 + $1.durationSec } / 60
    }

    /// Only sessions that actually captured HRV — breathing sessions with no
    /// sensor attached have nothing to show.
    var withHRV: [SessionRecord] {
        records.filter { $0.rmssdDelta != nil }
    }

    /// Mean within-session RMSSD change. Positive means RMSSD rose during
    /// sessions on average, which is the direction the device claims.
    var meanDelta: Double? {
        let deltas = withHRV.compactMap { $0.rmssdDelta }
        guard deltas.isEmpty == false else { return nil }
        return deltas.reduce(0, +) / Double(deltas.count)
    }

    var mostRecent: [SessionRecord] {
        Array(records.suffix(12).reversed())
    }

    // MARK: - Persistence

    private func save() {
        guard let data = try? JSONEncoder().encode(records) else { return }
        UserDefaults.standard.set(data, forKey: key)
    }

    private func load() {
        guard let data = UserDefaults.standard.data(forKey: key),
              let decoded = try? JSONDecoder().decode([SessionRecord].self, from: data)
        else { return }
        records = decoded
    }
}

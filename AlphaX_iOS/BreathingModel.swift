//
//  BreathingModel.swift
//  AlphaX
//
//  The breathing exercise is its own thing. It needs no device, no radio and
//  no session from the firmware — just a clock. Keeping it separate from
//  BLEManager means it can never be blocked by a connection problem.
//
//  Open-ended by design: it counts up and stops when the person decides to
//  stop, rather than imposing a duration they have to choose before they
//  begin.
//

import Foundation
import Combine

final class BreathingModel: ObservableObject {

    @Published private(set) var isRunning = false
    @Published private(set) var elapsed = 0          // seconds

    /// Called when a session ends, so history can be recorded without this
    /// model needing to know the store exists.
    var onSessionEnd: ((SessionRecord) -> Void)?

    private var timer: Timer?
    private var started: Date?

    var elapsedText: String {
        String(format: "%02d:%02d", elapsed / 60, elapsed % 60)
    }

    /// Rough count of completed 10 s breath cycles — a gentler sense of
    /// progress than a stopwatch, and the only "score" the patient sees.
    var breathCount: Int { elapsed / 10 }

    func toggle() { isRunning ? stop() : start() }

    func start() {
        started   = Date()
        elapsed   = 0
        isRunning = true

        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self, let started = self.started else { return }
            self.elapsed = Int(Date().timeIntervalSince(started))
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil

        let completed = elapsed
        started   = nil
        isRunning = false

        // Ignore accidental taps — under one full breath cycle isn't a session.
        guard completed >= 10 else { return }

        onSessionEnd?(SessionRecord(
            date: Date(),
            kind: .breathing,
            durationSec: completed,
            meanRMSSD: nil,
            rmssdDelta: nil,
            meanBPM: nil,
            intensity: 0))
    }
}

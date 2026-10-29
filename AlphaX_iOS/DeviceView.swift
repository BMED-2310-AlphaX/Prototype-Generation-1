//
//  DeviceView.swift
//  AlphaX
//
//  Connection and stimulation controls on one page: pair the device, then
//  run a session, without switching tabs mid-task.
//
//  Everything below the connection card is inert until a device is connected
//  — visible, so the person knows the capability exists, but not misleading.
//  The breathing exercise on the first tab never depends on any of this.
//
//  This page also hides the way into the clinician view: five taps on the
//  version line at the bottom. Deliberately undiscoverable — a patient who
//  wanders in there finds raw physiological numbers, which is exactly what
//  the two-view split exists to prevent.
//
//  STYLE NOTE
//    Small sub-views with explicit types, ternaries hoisted into computed
//    properties. SwiftUI's type-checker gives up on large single expressions
//    and then reports misleading errors elsewhere.
//

import SwiftUI

struct DeviceView: View {
    @EnvironmentObject private var ble: BLEManager
    @AppStorage("userRole") private var roleRaw = UserRole.patient.rawValue

    @State private var intensity: Double = 40
    @State private var minutes: Int = 0          // 0 == open-ended

    @State private var taps = 0
    @State private var lastTap = Date.distantPast
    @State private var justUnlocked = false

    private struct DurationOption: Identifiable {
        let minutes: Int
        let label: String
        var id: Int { minutes }
    }

    private let durations: [DurationOption] = [
        DurationOption(minutes:  0, label: "Open"),
        DurationOption(minutes:  5, label: "5 min"),
        DurationOption(minutes: 20, label: "20 min"),
        DurationOption(minutes: 40, label: "40 min"),
    ]

    private let openEndedCap = 60                 // firmware maximum

    // MARK: - Derived values

    private var connected: Bool { ble.isLive }
    private var running: Bool { ble.status.state.isActive }

    private var intensityLabel: String { "\(Int(intensity))%" }

    private var stateLabel: String {
        if running { return ble.status.state.display.uppercased() }
        return connected ? (minutes == 0 ? "OPEN-ENDED" : "READY") : "NOT CONNECTED"
    }

    private var durationNote: String {
        minutes == 0
            ? "Runs until you stop it."
            : "Stops automatically after \(minutes) minutes."
    }

    /// Fixed durations count down. Open-ended counts up, because a countdown
    /// from the 60-minute cap would imply a deadline nobody chose.
    private var displayTime: String {
        guard running else { return "--:--" }
        let seconds: Int = minutes == 0
            ? (openEndedCap * 60) - ble.status.remaining
            : ble.status.remaining
        let safe = max(0, seconds)
        return String(format: "%02d:%02d", safe / 60, safe % 60)
    }

    private var startMinutes: Int { minutes == 0 ? openEndedCap : minutes }
    private var timerColor: Color { connected ? Color.axPurple : Color.axMuted.opacity(0.4) }

    // MARK: - Body

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    connectionPanel
                    timerPanel
                    controlsPanel
                    actionButtons

                    Spacer(minLength: 28)
                    versionLine
                }
                .padding(16)
            }
            .background(Color.white)
            .navigationTitle("Device")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    // MARK: - Connection

    private var connectionPanel: some View {
        Panel {
            HStack {
                Circle()
                    .fill(connected ? Color.axGreen : Color.gray.opacity(0.4))
                    .frame(width: 10, height: 10)

                VStack(alignment: .leading, spacing: 2) {
                    Text(ble.deviceName ?? "No device")
                        .font(.system(size: 16, weight: .medium))
                    Text(ble.link.label)
                        .font(.system(size: 12))
                        .foregroundStyle(Color.axMuted)
                }
                Spacer()
            }

            connectButton

            if !connected {
                Button("Browse all devices") { ble.browse() }
                    .buttonStyle(OutlineButtonStyle())

                Text("Breathing works without a device. Connect to use the "
                   + "stimulation controls below.")
                    .font(.system(size: 12))
                    .foregroundStyle(Color.axMuted)
            }
        }
    }

    private var connectButton: some View {
        let title = connected ? "Disconnect" : "Connect"
        let fill: Color = connected ? .axRed : .axPurple
        return Button(title) {
            if connected { ble.disconnect() } else { ble.connect() }
        }
        .buttonStyle(FilledButtonStyle(fill: fill))
    }

    // MARK: - Session

    private var timerPanel: some View {
        Panel {
            VStack(spacing: 2) {
                Text(displayTime)
                    .font(.system(size: 46, weight: .bold, design: .rounded))
                    .foregroundStyle(timerColor)
                    .monospacedDigit()

                Text(stateLabel)
                    .font(.system(size: 12, weight: .medium))
                    .tracking(1.2)
                    .foregroundStyle(Color.axMuted)
            }
            .frame(maxWidth: .infinity)
        }
    }

    private var controlsPanel: some View {
        Panel {
            intensityRow
            intensitySlider
            durationHeader
            durationButtons
            Text(durationNote)
                .font(.system(size: 12))
                .foregroundStyle(Color.axMuted)
        }
        .disabled(!connected)
        .opacity(connected ? 1 : 0.45)
    }

    private var intensityRow: some View {
        HStack {
            Text("Intensity")
                .font(.system(size: 15, weight: .semibold))
            Spacer()
            Text(intensityLabel)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(Color.axMuted)
                .monospacedDigit()
        }
    }

    private var intensitySlider: some View {
        // Named method rather than an inline closure: a manager call buried in
        // a ViewBuilder closure confuses the compiler's property-wrapper
        // resolution and produces errors that point at the wrong line.
        Slider(value: $intensity,
               in: 0...80,
               step: 5,
               onEditingChanged: commitIntensity)
            .tint(Color.axPurple)
    }

    private func commitIntensity(_ editing: Bool) {
        guard editing == false, connected else { return }
        ble.setIntensity(Int(intensity))
    }

    private var durationHeader: some View {
        Text("Duration")
            .font(.system(size: 15, weight: .semibold))
            .padding(.top, 8)
    }

    private var durationButtons: some View {
        HStack(spacing: 8) {
            ForEach(durations) { option in
                Button(option.label) {
                    minutes = option.minutes
                }
                .buttonStyle(OutlineButtonStyle(selected: minutes == option.minutes))
            }
        }
    }

    private var actionButtons: some View {
        VStack(spacing: 10) {
            startButton
            stopButton
        }
    }

    private var startButton: some View {
        let disabled = !connected || running
        return Button("Start session") {
            ble.startSession(intensity: Int(intensity), minutes: startMinutes)
        }
        .buttonStyle(FilledButtonStyle())
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1)
    }

    private var stopButton: some View {
        let disabled = !connected || !running
        return Button("Stop") {
            ble.stopSession()
        }
        .buttonStyle(FilledButtonStyle(fill: Color.axRed))
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1)
    }

    // MARK: - Hidden clinician unlock

    private var versionLine: some View {
        VStack(spacing: 4) {
            Text("AlphaX  ·  v1.0")
                .font(.system(size: 11))
                .foregroundStyle(Color.axMuted.opacity(0.75))
                .onTapGesture { registerTap() }

            if justUnlocked {
                Text("Clinician view enabled")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(Color.axPurple)
                    .transition(.opacity)
            }
        }
        .padding(.bottom, 8)
    }

    /// TAPS_TO_UNLOCK taps inside a rolling window. Tapping slowly resets, so
    /// it won't trigger by accident. Change these two constants to make the
    /// gesture easier or harder.
    private func registerTap() {
        let tapsToUnlock = 5
        let window: TimeInterval = 1.5

        let now = Date()
        taps = now.timeIntervalSince(lastTap) < window ? taps + 1 : 1
        lastTap = now

        guard taps >= tapsToUnlock else { return }
        taps = 0

        withAnimation {
            roleRaw = UserRole.clinician.rawValue
            justUnlocked = true
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) {
            withAnimation { justUnlocked = false }
        }
    }
}

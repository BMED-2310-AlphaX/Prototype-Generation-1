//
//  ClinicianView.swift
//  AlphaX
//
//  The clinician's page: who's running the session, what's happening right
//  now, and what has happened across sessions.
//
//  Trends matter more than any single reading here. RMSSD's usefulness as an
//  outcome measure is in within-subject change — a 2023 meta-analysis of
//  slow-paced breathing found a large increase during practice (SMD 0.90),
//  while the same metric does NOT reliably separate GAD patients from
//  controls. So this page leads with change-per-session, not absolutes.
//

import SwiftUI
import Charts

struct ClinicianView: View {
    @EnvironmentObject private var ble: BLEManager
    @EnvironmentObject private var store: SessionStore

    @AppStorage("userRole") private var roleRaw = UserRole.clinician.rawValue
    @AppStorage("clinicianName") private var clinicianName = ""
    @AppStorage("clinicianID")   private var clinicianID = ""

    @State private var rawCommand = ""
    @State private var showDevTools = false
    @State private var confirmClear = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    clinicianCard
                    liveCard
                    trendCard
                    historyCard
                    demoCard
                    devCard
                    switchRoleButton
                }
                .padding(16)
            }
            .background(Color.white)
            .navigationTitle("Clinician")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    // MARK: - Who is running this

    private var clinicianCard: some View {
        Panel {
            Text("Clinician")
                .font(.system(size: 13, weight: .semibold))

            TextField("Name", text: $clinicianName)
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 14))

            TextField("ID or credential", text: $clinicianID)
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 14))
                .autocorrectionDisabled()

            Text(sessionAttribution)
                .font(.system(size: 11.5))
                .foregroundStyle(Color.axMuted)
        }
    }

    private var sessionAttribution: String {
        let who = clinicianName.isEmpty ? "Unattributed" : clinicianName
        let id  = clinicianID.isEmpty ? "" : " · \(clinicianID)"
        return "Sessions recorded under: \(who)\(id)"
    }

    // MARK: - Right now

    private var liveCard: some View {
        Panel {
            Text("Live")
                .font(.system(size: 13, weight: .semibold))

            HStack(spacing: 10) {
                metric(bpmText, "BPM")
                metric(rmssdText, "RMSSD ms")
                metric("\(ble.status.intensity)%", "Intensity")
            }

            liveChart

            Text("RMSSD, 12-beat rolling window. Responsive but noisy — read "
               + "the direction, not the value. Validation needs ≥60 s epochs "
               + "from a validated sensor.")
                .font(.system(size: 11.5))
                .foregroundStyle(Color.axMuted)
        }
    }

    private var bpmText: String {
        ble.status.bpm > 0 ? "\(Int(ble.status.bpm.rounded()))" : "--"
    }

    private var rmssdText: String {
        ble.status.rmssd > 0 ? "\(Int(ble.status.rmssd.rounded()))" : "--"
    }

    private func metric(_ value: String, _ label: String) -> some View {
        VStack(spacing: 3) {
            Text(value)
                .font(.system(size: 22, weight: .bold))
                .foregroundStyle(Color.axPurple)
                .monospacedDigit()
            Text(label.uppercased())
                .font(.system(size: 10, weight: .medium))
                .tracking(0.5)
                .foregroundStyle(Color.axMuted)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 11)
        .background(.white)
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.axBorder, lineWidth: 1))
    }

    @ViewBuilder
    private var liveChart: some View {
        ZStack {
            if ble.hrv.count < 2 {
                Text("Fills in once a session is running.")
                    .font(.system(size: 12))
                    .foregroundStyle(Color.axMuted)
            } else {
                Chart(ble.hrv) { sample in
                    LineMark(x: .value("Time", sample.time),
                             y: .value("RMSSD", sample.rmssd))
                        .interpolationMethod(.monotone)
                        .foregroundStyle(Color.axPurple)
                        .lineStyle(StrokeStyle(lineWidth: 2.5))
                }
                .chartXAxis(.hidden)
                .chartYAxis { axisMarks }
                .padding(8)
            }
        }
        .frame(height: 120)
        .frame(maxWidth: .infinity)
        .background(.white)
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.axBorder, lineWidth: 1))
    }

    private var axisMarks: some AxisContent {
        AxisMarks(position: .leading) { value in
            AxisGridLine().foregroundStyle(Color.axBorder)
            AxisValueLabel {
                if let v = value.as(Double.self) {
                    Text("\(Int(v))")
                        .font(.system(size: 10))
                        .foregroundStyle(Color.axMuted)
                }
            }
        }
    }

    // MARK: - Trends over time

    private var trendCard: some View {
        Panel {
            Text("Trends")
                .font(.system(size: 13, weight: .semibold))

            HStack(spacing: 10) {
                metric("\(store.totalSessions)", "Sessions")
                metric("\(store.totalMinutes)", "Minutes")
                metric(meanDeltaText, "Mean Δ ms")
            }

            trendChart

            Text(trendCaption)
                .font(.system(size: 11.5))
                .foregroundStyle(Color.axMuted)
        }
    }

    private var meanDeltaText: String {
        guard let mean = store.meanDelta else { return "--" }
        let sign = mean >= 0 ? "+" : ""
        return "\(sign)\(Int(mean.rounded()))"
    }

    private var trendCaption: String {
        if store.withHRV.isEmpty {
            return "Within-session RMSSD change per session. Needs a pulse "
                 + "sensor — breathing-only sessions record duration only."
        }
        return "Within-session RMSSD change (last third minus first third). "
             + "Bars above zero mean RMSSD rose during the session."
    }

    @ViewBuilder
    private var trendChart: some View {
        ZStack {
            if store.withHRV.count < 2 {
                Text("Two sessions with HRV data will draw a trend.")
                    .font(.system(size: 12))
                    .foregroundStyle(Color.axMuted)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 12)
            } else {
                Chart(store.withHRV) { record in
                    BarMark(x: .value("Session", record.date),
                            y: .value("Δ RMSSD", record.rmssdDelta ?? 0))
                        .foregroundStyle(barColor(for: record))
                }
                .chartXAxis(.hidden)
                .chartYAxis { axisMarks }
                .padding(8)
            }
        }
        .frame(height: 130)
        .frame(maxWidth: .infinity)
        .background(.white)
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.axBorder, lineWidth: 1))
    }

    private func barColor(for record: SessionRecord) -> Color {
        let delta = record.rmssdDelta ?? 0
        return delta >= 0 ? Color.axPurple : Color.axRed
    }

    // MARK: - History

    private var historyCard: some View {
        Panel {
            HStack {
                Text("Recent sessions")
                    .font(.system(size: 13, weight: .semibold))
                Spacer()
                if store.totalSessions > 0 {
                    Button(confirmClear ? "Tap again to erase" : "Clear") {
                        handleClear()
                    }
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(Color.axRed)
                }
            }

            if store.records.isEmpty {
                Text("No sessions recorded yet.")
                    .font(.system(size: 12))
                    .foregroundStyle(Color.axMuted)
            } else {
                VStack(spacing: 0) {
                    ForEach(store.mostRecent) { record in
                        historyRow(record)
                        Divider().overlay(Color.axBorder)
                    }
                }
            }
        }
    }

    private func historyRow(_ record: SessionRecord) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(record.dateText)
                    .font(.system(size: 13, weight: .medium))
                Text("\(record.kind.label) · \(record.durationText)")
                    .font(.system(size: 11.5))
                    .foregroundStyle(Color.axMuted)
            }
            Spacer()
            Text(deltaText(record))
                .font(.system(size: 14, weight: .semibold))
                .monospacedDigit()
                .foregroundStyle(deltaColor(record))
        }
        .padding(.vertical, 8)
    }

    private func deltaText(_ record: SessionRecord) -> String {
        guard let delta = record.rmssdDelta else { return "—" }
        let sign = delta >= 0 ? "+" : ""
        return "\(sign)\(Int(delta.rounded())) ms"
    }

    private func deltaColor(_ record: SessionRecord) -> Color {
        guard let delta = record.rmssdDelta else { return Color.axMuted }
        return delta >= 0 ? Color.axGreen : Color.axRed
    }

    /// Two taps to erase — history is the only thing here that can't be
    /// regenerated by running the device again.
    private func handleClear() {
        if confirmClear {
            store.clear()
            confirmClear = false
        } else {
            confirmClear = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
                confirmClear = false
            }
        }
    }

    // MARK: - Demo and developer tools

    private var demoCard: some View {
        Panel {
            Toggle(isOn: $ble.demoMode) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Demo mode")
                        .font(.system(size: 14, weight: .semibold))
                    Text("Synthesizes biometrics with no hardware. Never "
                       + "present this as measured data.")
                        .font(.system(size: 11.5))
                        .foregroundStyle(Color.axMuted)
                }
            }
            .tint(Color.axPurple)
        }
    }

    private var devCard: some View {
        Panel {
            devDisclosure
            if showDevTools {
                Toggle(isOn: $ble.verboseLogging) {
                    Text("Log raw packets")
                        .font(.system(size: 13))
                        .foregroundStyle(Color.axMuted)
                }
                .tint(Color.axPurple)

                commandRow

                Text("START:<%>:<min>, STOP, SET:<%>. The firmware clamps "
                   + "anything out of range.")
                    .font(.system(size: 11.5))
                    .foregroundStyle(Color.axMuted)

                logList
            }
        }
    }

    private var devDisclosure: some View {
        Button {
            withAnimation(.easeInOut(duration: 0.15)) { showDevTools.toggle() }
        } label: {
            HStack {
                Text("Developer tools")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Color.axPurple)
                Spacer()
                Image(systemName: showDevTools ? "chevron.up" : "chevron.down")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(Color.axPurple)
            }
        }
    }

    private var commandRow: some View {
        HStack(spacing: 8) {
            TextField("START:40:20", text: $rawCommand)
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 14, design: .monospaced))
                .autocorrectionDisabled()
                .textInputAutocapitalization(.characters)
                .onSubmit(sendRaw)

            Button("Send", action: sendRaw)
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(ble.isLive ? Color.axPurple : Color.axMuted)
                .disabled(ble.isLive == false || rawCommand.isEmpty)
        }
    }

    private func sendRaw() {
        guard ble.isLive else { return }
        ble.sendRaw(rawCommand)
        rawCommand = ""
    }

    private var logList: some View {
        VStack(alignment: .leading, spacing: 4) {
            ForEach(ble.log.prefix(10)) { line in
                Text("[\(line.stamp)] \(line.text)")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Color.axMuted)
                    .lineLimit(2)
            }
        }
    }

    // MARK: - Role

    private var switchRoleButton: some View {
        Button("Switch to patient view") {
            ble.demoMode = false          // don't leave fake data running
            withAnimation { roleRaw = UserRole.patient.rawValue }
        }
        .buttonStyle(OutlineButtonStyle())
        .padding(.top, 4)
    }
}

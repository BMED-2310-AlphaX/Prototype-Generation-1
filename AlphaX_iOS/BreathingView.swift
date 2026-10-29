//
//  BreathingView.swift
//  AlphaX
//
//  The patient's home screen. One thing on it: a circle to breathe with.
//
//  No device required, no duration to choose, no numbers to watch. Begin,
//  breathe, stop when you've had enough.
//

import SwiftUI

struct BreathingView: View {
    @EnvironmentObject private var breath: BreathingModel

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                Spacer(minLength: 12)

                BreathingPacer(isActive: breath.isRunning)

                Spacer(minLength: 8)

                // Elapsed time is small and secondary on purpose. The circle
                // is what you follow; this is just there if you want it.
                VStack(spacing: 6) {
                    Text(breath.isRunning ? breath.elapsedText : "Take as long as you like")
                        .font(.system(size: breath.isRunning ? 28 : 15,
                                      weight: breath.isRunning ? .semibold : .regular,
                                      design: breath.isRunning ? .rounded : .default))
                        .foregroundStyle(breath.isRunning ? Color.axPurple : Color.axMuted)
                        .monospacedDigit()

                    if breath.isRunning && breath.breathCount > 0 {
                        Text("\(breath.breathCount) breath\(breath.breathCount == 1 ? "" : "s")")
                            .font(.system(size: 13))
                            .foregroundStyle(Color.axMuted)
                    }
                }
                .frame(height: 56)

                Spacer(minLength: 16)

                Button(breath.isRunning ? "Stop" : "Begin breathing") {
                    withAnimation(.easeInOut(duration: 0.25)) { breath.toggle() }
                }
                .buttonStyle(FilledButtonStyle(fill: breath.isRunning ? .axRed : .axPurple))
                .padding(.horizontal, 24)

                Text("Six breaths a minute — in for four, out for six.")
                    .font(.system(size: 12))
                    .foregroundStyle(Color.axMuted)
                    .padding(.top, 14)

                Spacer(minLength: 20)
            }
            .frame(maxWidth: .infinity)
            .background(Color.white)
            .navigationTitle("Breathe")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

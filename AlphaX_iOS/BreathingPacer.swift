//
//  BreathingPacer.swift
//  AlphaX
//
//  The patient-facing element: a circle that expands on the inhale and
//  contracts on the exhale.
//
//  TIMING
//    4 s in, 6 s out — a 10 s cycle, so 6 breaths per minute. That rate is
//    what the slow-paced-breathing literature uses, and it sits inside the
//    4.5–6.5 breaths/min band where vagal outflow and blood pressure
//    oscillations synchronise. The longer exhale matters: vagal activity
//    rises during expiration, so an exhale-weighted ratio does more than an
//    even one.
//
//  WHY NO NUMBERS HERE
//    This gives the patient something to DO. A heart rate gives them
//    something to WATCH. For an anxious population those are not equivalent
//    — see the clinician view for the measurements.
//

import SwiftUI

struct BreathingPacer: View {
    /// Animates while a session runs; rests quietly otherwise.
    let isActive: Bool

    private let inhale: Double = 4
    private let exhale: Double = 6
    private var cycle: Double { inhale + exhale }

    private let restScale: CGFloat = 0.52

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: !isActive)) { context in
            let phase = breathPhase(at: context.date)
            circle(scale: isActive ? phase.scale : restScale,
                   label: isActive ? phase.label : "Ready when you are")
        }
        .frame(height: 260)
        .frame(maxWidth: .infinity)
    }

    /// Where we are in the breath cycle right now.
    /// Derived from wall-clock time rather than accumulated state, so it stays
    /// smooth across view updates and can't drift.
    private func breathPhase(at date: Date) -> (scale: CGFloat, label: String) {
        let t = date.timeIntervalSinceReferenceDate
            .truncatingRemainder(dividingBy: cycle)

        let isInhaling = t < inhale
        let progress   = isInhaling ? t / inhale : (t - inhale) / exhale

        // Cosine easing: slowest at the turns, which is where a real breath
        // pauses. Linear scaling feels mechanical.
        let eased = CGFloat(0.5 - cos(progress * .pi) / 2)

        let scale = isInhaling
            ? restScale + (1 - restScale) * eased
            : 1 - (1 - restScale) * eased

        return (scale, isInhaling ? "Breathe in" : "Breathe out")
    }

    private func circle(scale: CGFloat, label: String) -> some View {
        VStack(spacing: 22) {
            ZStack {
                // Stationary guide marking the top of the inhale, so there's
                // a target to breathe toward.
                Circle()
                    .stroke(Color.axBorder, lineWidth: 1.5)
                    .frame(width: 190, height: 190)

                Circle()
                    .fill(
                        RadialGradient(
                            colors: [Color.axPurpleLight.opacity(0.34),
                                     Color.axPurple.opacity(0.17)],
                            center: .center, startRadius: 4, endRadius: 96)
                    )
                    .frame(width: 190, height: 190)
                    .scaleEffect(scale)

                Circle()
                    .stroke(Color.axPurple.opacity(0.55), lineWidth: 2.5)
                    .frame(width: 190, height: 190)
                    .scaleEffect(scale)
            }
            .frame(height: 200)

            Text(label)
                .font(.system(size: 16, weight: .medium))
                .foregroundStyle(Color.axPurple)
                .animation(.easeInOut(duration: 0.4), value: label)
        }
    }
}

#Preview {
    VStack {
        BreathingPacer(isActive: true)
        BreathingPacer(isActive: false)
    }
}

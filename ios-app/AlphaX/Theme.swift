//
//  Theme.swift
//  AlphaX
//
//  One place for the colors so the app matches the deck. Change these and the
//  whole UI follows.
//

import SwiftUI

extension Color {
    static let axPurple      = Color(red: 0.298, green: 0.114, blue: 0.420) // #4C1D6B
    static let axPurpleLight = Color(red: 0.541, green: 0.310, blue: 0.702) // #8A4FB3
    static let axPurplePale  = Color(red: 0.957, green: 0.945, blue: 0.969) // #F4F1F7
    static let axBorder      = Color(red: 0.890, green: 0.855, blue: 0.918) // #E3DAEA
    static let axMuted       = Color(red: 0.353, green: 0.353, blue: 0.353)
    static let axRed         = Color(red: 0.698, green: 0.227, blue: 0.330) // #B23A54
    static let axGreen       = Color(red: 0.180, green: 0.490, blue: 0.357) // #2E7D5B
}

/// The rounded pale-purple panel used throughout the app.
struct Panel<Content: View>: View {
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) { content }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.axPurplePale)
            .clipShape(RoundedRectangle(cornerRadius: 14))
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(Color.axBorder, lineWidth: 1)
            )
    }
}

/// Filled button — Start, and the destructive Stop.
struct FilledButtonStyle: ButtonStyle {
    var fill: Color = .axPurple

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 17, weight: .semibold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 15)
            .background(fill)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .opacity(configuration.isPressed ? 0.75 : 1)
    }
}

/// Outlined button — Connect, duration presets.
struct OutlineButtonStyle: ButtonStyle {
    var selected: Bool = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 15, weight: .semibold))
            .foregroundStyle(selected ? .white : Color.axPurple)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 11)
            .background(selected ? Color.axPurple : .white)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(Color.axPurple, lineWidth: 1.5)
            )
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}

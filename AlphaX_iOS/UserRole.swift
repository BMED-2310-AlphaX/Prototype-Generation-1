//
//  UserRole.swift
//  AlphaX
//
//  Which interface someone sees. Chosen once on first launch, stored after.
//
//  This is a usability boundary, not a security one — a determined patient
//  can switch roles. The point is that physiological numbers are never
//  encountered incidentally during a session, which is what the evidence on
//  wearables and health anxiety argues for.
//

import SwiftUI

enum UserRole: String {
    case unset
    case patient
    case clinician
}

struct RolePickerView: View {
    @AppStorage("userRole") private var roleRaw = UserRole.unset.rawValue

    var body: some View {
        VStack(spacing: 0) {
            Spacer()

            VStack(spacing: 8) {
                Text("AlphaX")
                    .font(.system(size: 34, weight: .bold, design: .rounded))
                    .foregroundStyle(Color.axPurple)
                Text("Who's using this device?")
                    .font(.system(size: 15))
                    .foregroundStyle(Color.axMuted)
            }
            .padding(.bottom, 36)

            VStack(spacing: 14) {
                roleCard(
                    role: .patient,
                    title: "I'm using it myself",
                    detail: "Breathing exercise and session controls.",
                    icon: "wind")

                roleCard(
                    role: .clinician,
                    title: "Clinician or researcher",
                    detail: "Adds live measurements and trends across sessions.",
                    icon: "stethoscope")
            }
            .padding(.horizontal, 24)

            Spacer()

            Text("You can change this later.")
                .font(.system(size: 12))
                .foregroundStyle(Color.axMuted)
                .padding(.bottom, 28)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.white)
    }

    private func roleCard(role: UserRole,
                          title: String,
                          detail: String,
                          icon: String) -> some View {
        Button {
            withAnimation { roleRaw = role.rawValue }
        } label: {
            HStack(spacing: 14) {
                Image(systemName: icon)
                    .font(.system(size: 22))
                    .foregroundStyle(Color.axPurple)
                    .frame(width: 34)

                VStack(alignment: .leading, spacing: 3) {
                    Text(title)
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundStyle(.primary)
                    Text(detail)
                        .font(.system(size: 12.5))
                        .foregroundStyle(Color.axMuted)
                        .multilineTextAlignment(.leading)
                }
                Spacer(minLength: 0)
            }
            .padding(18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.axPurplePale)
            .clipShape(RoundedRectangle(cornerRadius: 14))
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(Color.axBorder, lineWidth: 1)
            )
        }
    }
}

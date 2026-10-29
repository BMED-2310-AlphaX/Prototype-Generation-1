//
//  ContentView.swift
//  AlphaX
//
//  Role gate, then tabs.
//
//  First launch asks who's using the device. Patients get two tabs — the
//  breathing exercise and the device. Clinicians get those plus measurements
//  and trends.
//
//  WHY THE SPLIT
//    Our users are anxious by definition, and the evidence on wearables and
//    health anxiety says showing an anxious person a number that can rise
//    invites exactly the symptom-monitoring loop this device is meant to
//    interrupt. The measurements are research instruments, so they live
//    where researchers work — not one tab away from a patient mid-session.
//
//    A patient can still reach the clinician view (five taps on the version
//    line), so this is a usability boundary, not a security one. The point is
//    that nobody encounters raw physiological numbers by accident.
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var ble: BLEManager
    @AppStorage("userRole") private var roleRaw = UserRole.unset.rawValue

    private var role: UserRole {
        UserRole(rawValue: roleRaw) ?? .unset
    }

    var body: some View {
        Group {
            if role == .unset {
                RolePickerView()
            } else {
                tabs
            }
        }
        // onDismiss matters: swiping the sheet away must stop the scan, or
        // the radio keeps running in the background.
        .sheet(isPresented: $ble.isBrowsing, onDismiss: { ble.stopBrowsing() }) {
            DeviceBrowserView()
        }
    }

    private var tabs: some View {
        TabView {
            BreathingView()
                .tabItem { Label("Breathe", systemImage: "wind") }

            DeviceView()
                .tabItem { Label("Device", systemImage: "dot.radiowaves.left.and.right") }

            if role == .clinician {
                ClinicianView()
                    .tabItem { Label("Clinician", systemImage: "stethoscope") }
            }
        }
        .tint(Color.axPurple)
    }
}

//
//  AlphaXApp.swift
//  AlphaX — Team AlphaX, BMED 2310
//
//  Three shared objects: the radio, the breathing clock, and session history.
//  They're kept apart on purpose — a Bluetooth problem can never stop someone
//  breathing, and neither model needs to know the history store exists. They
//  just hand it a finished session through a callback, wired up here.
//

import SwiftUI

@main
struct AlphaXApp: App {
    @StateObject private var ble    = BLEManager()
    @StateObject private var breath = BreathingModel()
    @StateObject private var store  = SessionStore()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
                .environmentObject(breath)
                .environmentObject(store)
                .preferredColorScheme(.light)
                .onAppear(perform: wireHistory)
        }
    }

    private func wireHistory() {
        ble.onSessionEnd    = { [store] record in store.add(record) }
        breath.onSessionEnd = { [store] record in store.add(record) }
    }
}

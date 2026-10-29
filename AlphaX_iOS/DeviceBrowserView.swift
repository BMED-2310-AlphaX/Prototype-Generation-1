//
//  DeviceBrowserView.swift
//  AlphaX
//
//  Dev tool. Lists every BLE device in range so you can see whether your board
//  is advertising at all.
//
//  Reading the result:
//    • Board appears here with the AlphaX badge → tap it, you're done.
//    • Board appears WITHOUT the badge → it's advertising, but not the AlphaX
//      service UUID. Check addServiceUUID() in the firmware.
//    • Board doesn't appear at all → it isn't advertising. Check power and the
//      serial monitor, not the app.
//
//  That three-way split tells you which layer is broken, which is usually the
//  hard part.
//

import SwiftUI

struct DeviceBrowserView: View {
    @EnvironmentObject private var ble: BLEManager
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    if ble.discovered.isEmpty {
                        HStack(spacing: 10) {
                            ProgressView()
                            Text("Scanning…")
                                .foregroundStyle(Color.axMuted)
                        }
                        .padding(.vertical, 6)
                    } else {
                        ForEach(ble.discovered) { device in
                            Button {
                                ble.connect(to: device)
                                dismiss()
                            } label: {
                                row(for: device)
                            }
                        }
                    }
                } footer: {
                    Text("Sorted by signal strength. Your bench board should be "
                       + "near the top. A device listed without the AlphaX badge "
                       + "is advertising, but not our service UUID.")
                }
            }
            .navigationTitle("Nearby devices")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") {
                        ble.stopBrowsing()
                        dismiss()
                    }
                }
            }
        }
    }

    private func row(for device: DiscoveredDevice) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 3) {
                Text(device.name)
                    .font(.system(size: 15, weight: .medium))
                    .foregroundStyle(.primary)

                HStack(spacing: 6) {
                    Text("\(device.rssi) dBm")
                    if device.advertisesAlphaX {
                        Text("AlphaX")
                            .font(.system(size: 10, weight: .bold))
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(Color.axPurple)
                            .foregroundStyle(.white)
                            .clipShape(Capsule())
                    }
                }
                .font(.system(size: 12))
                .foregroundStyle(Color.axMuted)
            }

            Spacer()

            Text(device.signalBars)
                .font(.system(size: 13))
                .foregroundStyle(device.advertisesAlphaX ? Color.axPurple : Color.axMuted)
        }
        .padding(.vertical, 2)
    }
}

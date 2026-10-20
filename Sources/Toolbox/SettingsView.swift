import SwiftUI
import ServiceManagement

struct SettingsView: View {
    // SMAppService.mainApp 仅在 App 是正规 .app bundle 时可用；
    // 裸跑二进制（swift run）时 Bundle.main.bundleIdentifier 为 nil，会抛异常。
    private static let supportsLoginItem = Bundle.main.bundleIdentifier != nil

    @State private var isEnabled = SettingsView.supportsLoginItem && (SMAppService.mainApp.status == .enabled)
    @State private var errorMsg: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("通用")
                .font(.system(size: 25, weight: .bold))
                .padding(.horizontal, 20)
                .padding(.top, 16)
                .padding(.bottom, 12)

            Form {
                Section {
                    if SettingsView.supportsLoginItem {
                        Toggle(isOn: Binding(
                            get: { isEnabled },
                            set: { setLaunchAtLogin($0) }
                        )) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("开机自启")
                                Text("登录 macOS 时自动启动 Toolbox")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        .toggleStyle(.switch)
                    }
                } footer: {
                    if !SettingsView.supportsLoginItem {
                        Text("当前以未打包方式运行，开机自启不可用")
                    }
                }

                if let errorMsg {
                    Section {
                        Text(errorMsg)
                            .foregroundStyle(.red)
                    }
                }
            }
            .formStyle(.grouped)
        }
    }

    private func setLaunchAtLogin(_ on: Bool) {
        guard SettingsView.supportsLoginItem else { return }
        do {
            if on {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
            isEnabled = (SMAppService.mainApp.status == .enabled)
            errorMsg = nil
        } catch {
            errorMsg = "设置失败：\(error.localizedDescription)"
            isEnabled = (SMAppService.mainApp.status == .enabled)
        }
    }
}

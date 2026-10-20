import SwiftUI
import ServiceManagement

struct SettingsView: View {
    // SMAppService.mainApp 仅在 App 是正规 .app bundle 时可用；
    // 裸跑二进制（swift run）时 Bundle.main.bundleIdentifier 为 nil，会抛异常。
    private static let supportsLoginItem = Bundle.main.bundleIdentifier != nil

    @State private var isEnabled = SettingsView.supportsLoginItem && (SMAppService.mainApp.status == .enabled)
    @State private var errorMsg: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("设置")
                .font(.title2.bold())

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
            } else {
                Text("当前以未打包方式运行，开机自启不可用")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if let errorMsg {
                Text(errorMsg)
                    .font(.caption)
                    .foregroundStyle(.red)
            }

            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
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

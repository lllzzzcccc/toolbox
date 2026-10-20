import SwiftUI
import ServiceManagement

struct SettingsView: View {
    @State private var isEnabled = (SMAppService.mainApp.status == .enabled)
    @State private var errorMsg: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("设置")
                .font(.title2.bold())

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

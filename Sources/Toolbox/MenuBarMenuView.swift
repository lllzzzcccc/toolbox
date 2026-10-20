import SwiftUI
import AppKit

/// 菜单栏下拉：打开主界面 + 各工具开关 + 退出。
struct MenuBarMenuView: View {
    @Environment(ToolRegistry.self) private var registry
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Button {
                openWindow(id: "main")
            } label: {
                Label("打开主界面", systemImage: "sidebar.left")
            }

            Divider()

            ForEach(registry.tools) { tool in
                Toggle(isOn: Binding(
                    get: { tool.isEnabled },
                    set: { tool.setEnabled($0) }
                )) {
                    Label(tool.name, systemImage: tool.icon)
                }
            }

            Divider()

            Button("退出") {
                NSApplication.shared.terminate(nil)
            }
        }
        .padding(12)
        .frame(width: 200)
    }
}

import SwiftUI

/// 平台主界面：左侧边栏 = 工具列表（带开关）+ 设置，右侧 = 选中项的内容。
struct MainView: View {
    @Environment(ToolRegistry.self) private var registry
    @State private var selectedID: String?

    var body: some View {
        NavigationSplitView {
            List(selection: $selectedID) {
                Section("工具") {
                    ForEach(registry.tools) { tool in
                        HStack {
                            Label(tool.name, systemImage: tool.icon)
                            Spacer()
                            Toggle("", isOn: Binding(
                                get: { tool.isEnabled },
                                set: { tool.setEnabled($0) }
                            ))
                            .labelsHidden()
                            .toggleStyle(.switch)
                        }
                        .tag(tool.id)
                    }
                }
                Section {
                    Label("设置", systemImage: "gear")
                        .tag("settings")
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 150, ideal: 170)
            .navigationTitle("工具")
        } detail: {
            if selectedID == "settings" {
                SettingsView()
            } else if let tool = registry.tools.first(where: { $0.id == selectedID }) {
                tool.makeMainView()
            } else {
                ContentUnavailableView(
                    "选择一个工具",
                    systemImage: "square.grid.2x2",
                    description: Text("从左侧选择一个工具查看详情，或用开关启停它。")
                )
            }
        }
        .frame(minWidth: 560, minHeight: 360)
    }
}

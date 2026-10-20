import SwiftUI
import AppKit

@main
struct ToolboxApp: App {
    @State private var registry = ToolRegistry(tools: [
        SystemMonitorTool(),
        CalendarSyncTool(),
    ])

    var body: some Scene {
        // 平台主窗口（侧边栏 + 主页面）
        Window("Toolbox", id: "main") {
            MainView()
                .environment(registry)
        }

        // 菜单栏项：系统监控开启时显示 CPU/内存摘要，否则显示图标
        MenuBarExtra {
            MenuBarMenuView()
                .environment(registry)
        } label: {
            if let monitor = registry.tools.first(where: { $0 is SystemMonitorTool }) as? SystemMonitorTool,
               monitor.isEnabled {
                Text(monitor.statusText)
            } else {
                Image(systemName: "wrench.and.screwdriver")
            }
        }
        .menuBarExtraStyle(.window)
    }
}

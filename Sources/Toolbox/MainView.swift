import SwiftUI

/// 平台主界面：左侧通顶侧边栏（仿 macOS 系统设置样式，固定展开）+ 右侧选中项内容。
/// 无窗口顶栏；详情页自带大号标题。
struct MainView: View {
    @Environment(ToolRegistry.self) private var registry
    @State private var selectedID: String?

    private let sidebarWidth: CGFloat = 150

    var body: some View {
        HStack(spacing: 0) {
            sidebar
                .frame(width: sidebarWidth)

            detail
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        // 红绿灯悬浮在内容上方，不为它预留安全区；侧边栏顶部留白已自行避让。
        .ignoresSafeArea(.container, edges: .top)
        .frame(minWidth: 560, minHeight: 400)
        .onAppear {
            if selectedID == nil {
                selectedID = registry.tools.first?.id
            }
        }
    }

    // MARK: - 侧边栏

    private var sidebar: some View {
        VStack(spacing: 3) {
            // 顶部留白：避让窗口左上角的红绿灯按钮
            Spacer()
                .frame(height: 36)

            VStack(spacing: 3) {
                ForEach(registry.tools) { tool in
                    sidebarRow(icon: tool.icon, name: tool.name, id: tool.id, tint: tool.tint,
                               enabled: tool.isEnabled)
                }
            }

            Spacer()

            // 最底部：设置入口
            sidebarRow(icon: "gear", name: "通用", id: "settings", tint: .secondary)
                .padding(.bottom, 12)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    /// 单个边栏项：彩色图标 + 名称；选中时蓝色圆角高亮（仿系统设置）。
    /// enabled 非 nil 时在行右侧显示状态圆点：绿 = 启用，红 = 停用。
    private func sidebarRow(icon: String, name: String, id: String, tint: Color,
                            enabled: Bool? = nil) -> some View {
        let selected = selectedID == id
        return Button {
            selectedID = id
        } label: {
            HStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.system(size: 15))
                    .foregroundStyle(selected ? Color.white : tint)
                    .frame(width: 22, height: 22)
                Text(name)
                    .lineLimit(1)
                Spacer(minLength: 0)
                if let enabled {
                    Circle()
                        .fill(enabled ? Color.green : Color.red)
                        .frame(width: 7, height: 7)
                }
            }
            .foregroundStyle(selected ? Color.white : Color.primary)
            .frame(maxWidth: .infinity, minHeight: 30, alignment: .leading)
            .padding(.horizontal, 8)
            .background(
                RoundedRectangle(cornerRadius: 8)
                    .fill(selected ? Color.accentColor : Color.primary.opacity(0.06))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.primary.opacity(selected ? 0 : 0.09), lineWidth: 0.5)
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 10)
    }

    // MARK: - 内容区

    @ViewBuilder
    private var detail: some View {
        if selectedID == "settings" {
            SettingsView()
        } else if let tool = registry.tools.first(where: { $0.id == selectedID }) {
            tool.makeMainView()
        } else {
            ContentUnavailableView(
                "选择一个工具",
                systemImage: "square.grid.2x2",
                description: Text("从左侧选择一个工具查看详情。")
            )
        }
    }
}

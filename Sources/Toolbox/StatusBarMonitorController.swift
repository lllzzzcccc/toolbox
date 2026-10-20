import AppKit
import Foundation
import SwiftUI

extension Notification.Name {
    /// 状态项菜单「打开主界面」通过通知让 SwiftUI 侧 openWindow。
    static let openMainWindow = Notification.Name("com.toolbox.app.openMainWindow")
}

/// 菜单栏独立状态项：每个启用的监控项一个（上标识、下百分比，仿 Stats 样式）。
/// 停用即隐藏对应状态项，启用即恢复。读数由 SwiftUI 观察 tool 自动刷新。
@MainActor
final class StatusBarMonitorController: NSObject {
    private static let infoItemTag = 99

    private let monitors: [(tool: MonitorTool, item: NSStatusItem)]

    init(registry: ToolRegistry) {
        var pairs: [(MonitorTool, NSStatusItem)] = []
        for tool in registry.tools.compactMap({ $0 as? MonitorTool }) {
            let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
            pairs.append((tool, item))
        }
        self.monitors = pairs
        super.init()

        for (tool, item) in monitors {
            setupButton(item, tool: tool)
            let menu = buildMenu()
            menu.delegate = self
            item.menu = menu
        }
        tick()
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
    }

    /// 按钮内容换成自定义 SwiftUI 视图：整块 22pt（菜单栏标准高度）垂直居中，
    /// 宽度贴合内容（左对齐效果）。
    private func setupButton(_ item: NSStatusItem, tool: MonitorTool) {
        guard let button = item.button else { return }
        let hosting = NSHostingView(rootView: MenuBarMonitorLabel(tool: tool))
        hosting.frame = NSRect(origin: .zero, size: hosting.fittingSize)
        button.addSubview(hosting)
        button.title = ""
        button.image = nil
        button.imagePosition = .noImage
        button.frame = NSRect(origin: .zero, size: hosting.frame.size)
    }

    /// 每秒同步可见性与标签宽度；读数由视图观察 tool 自动更新。
    /// 宽度必须随内容重设——位数从「0%」涨到「100%」后 frame 不变会被裁剪。
    private func tick() {
        for (tool, item) in monitors {
            item.isVisible = tool.isEnabled
            guard let button = item.button, let hosting = button.subviews.first else { continue }
            let size = hosting.fittingSize
            if hosting.frame.size != size {
                hosting.frame = NSRect(origin: .zero, size: size)
                button.frame = NSRect(origin: .zero, size: size)
            }
        }
    }

    // MARK: - 菜单

    private func buildMenu() -> NSMenu {
        let menu = NSMenu()

        let openItem = NSMenuItem(title: "打开主界面", action: #selector(openMainWindow),
                                  keyEquivalent: "")
        openItem.target = self

        let quitItem = NSMenuItem(title: "退出 Toolbox", action: #selector(quit),
                                  keyEquivalent: "q")
        quitItem.target = self

        menu.addItem(openItem)
        let activityItem = NSMenuItem(title: "打开活动监视器", action: #selector(openActivityMonitor),
                                      keyEquivalent: "")
        activityItem.target = self
        menu.addItem(activityItem)
        menu.addItem(.separator())
        menu.addItem(quitItem)
        return menu
    }

    @objc private func openMainWindow() {
        // 接收方在 AppDelegate（常驻对象）：窗口还在则置前，已关闭则重建
        NotificationCenter.default.post(name: .openMainWindow, object: nil)
    }

    @objc private func openActivityMonitor() {
        NSWorkspace.shared.open(URL(fileURLWithPath: "/System/Applications/Utilities/Activity Monitor.app"))
    }

    @objc private func quit() {
        NSApplication.shared.terminate(nil)
    }
}

extension StatusBarMonitorController: NSMenuDelegate {
    /// 菜单弹出前重建顶部详情行（每次弹出取最新数据）。
    func menuNeedsUpdate(_ menu: NSMenu) {
        guard let tool = monitors.first(where: { $0.item.menu === menu })?.tool else { return }

        menu.items.filter { $0.tag == Self.infoItemTag }.forEach { menu.removeItem($0) }
        let lines = tool.details(tool)
        for (index, line) in lines.enumerated() {
            let item = NSMenuItem(title: line, action: nil, keyEquivalent: "")
            item.isEnabled = false
            item.tag = Self.infoItemTag
            menu.insertItem(item, at: index)
        }
        if !lines.isEmpty {
            let separator = NSMenuItem.separator()
            separator.tag = Self.infoItemTag
            menu.insertItem(separator, at: lines.count)
        }
    }
}

/// 菜单栏监控项标签：上行小字标识（CPU/RAM/SSD），下行大字号百分比。
/// 整块 22pt 高、贴合内容宽 → 垂直居中 + 左对齐。
private struct MenuBarMonitorLabel: View {
    let tool: MonitorTool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(tool.shortName)
                .font(.system(size: 7, weight: .medium))
            Text("\(tool.percent, specifier: "%.0f")%")
                .font(.system(size: 11, weight: .semibold, design: .monospaced))
        }
        .foregroundStyle(.primary)
        .fixedSize()
        .padding(.horizontal, 6)
        .frame(height: 22)
    }
}

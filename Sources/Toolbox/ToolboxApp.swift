import SwiftUI
import AppKit
import Foundation
import CSysInfo

@main
@MainActor
struct ToolboxApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @State private var registry: ToolRegistry
    @State private var statusBarMonitors: StatusBarMonitorController

    init() {
        // 内存按二进制显示（对齐活动监视器），磁盘按十进制显示（对齐磁盘工具/关于本机）
        let fmtMem: (UInt64) -> String = {
            ByteCountFormatter.string(fromByteCount: Int64($0), countStyle: .binary)
        }
        let fmtDisk: (UInt64) -> String = {
            ByteCountFormatter.string(fromByteCount: Int64($0), countStyle: .decimal)
        }
        // 磁盘容量实时读取（设置页口径：容器总容量 / 重要用途可用）
        let diskCapacityNow: () -> (total: UInt64, avail: UInt64) = {
            diskCapacity() ?? (sysinfo_disk_total_bytes(), sysinfo_disk_avail_bytes())
        }
        let registry = ToolRegistry(tools: [
            MonitorTool(id: "cpu", name: "CPU监控", shortName: "CPU",
                        icon: "cpu", tint: .blue,
                        sampler: { sysinfo_cpu_usage() },
                        details: { tool in
                            [String(format: "当前使用率 %.1f%%", tool.percent),
                             String(format: "系统 %.1f%%", sysinfo_cpu_system_percent()),
                             String(format: "用户 %.1f%%", sysinfo_cpu_user_percent()),
                             String(format: "闲置 %.1f%%", sysinfo_cpu_idle_percent())]
                        }),
            MonitorTool(id: "memory", name: "内存监控", shortName: "RAM",
                        icon: "memorychip", tint: .green,
                        sampler: { sysinfo_mem_usage() },
                        details: { _ in
                            ["总容量 \(fmtMem(sysinfo_mem_total_bytes()))",
                             "已使用 \(fmtMem(sysinfo_mem_used_bytes()))",
                             "已缓存 \(fmtMem(sysinfo_mem_cached_bytes()))",
                             "空闲 \(fmtMem(sysinfo_mem_free_bytes()))"]
                        }),
            MonitorTool(id: "disk", name: "磁盘监控", shortName: "SSD",
                        icon: "internaldrive", tint: .purple,
                        sampler: {
                            let (total, avail) = diskCapacityNow()
                            guard total > avail else { return 0 }
                            return Double(total - avail) / Double(total) * 100
                        },
                        details: { _ in
                            let (total, avail) = diskCapacityNow()
                            return ["总容量 \(fmtDisk(total))",
                                    "已占用 \(fmtDisk(total > avail ? total - avail : 0))",
                                    "可用 \(fmtDisk(avail))"]
                        }),
            CalendarSyncTool(),
        ])
        _registry = State(initialValue: registry)
        _statusBarMonitors = State(initialValue: StatusBarMonitorController(registry: registry))
        appDelegate.registry = registry
    }

    var body: some Scene {
        // 平台主窗口（无窗口标题/顶栏：红绿灯悬浮在内容上方，仿 macOS 系统设置）
        Window(Text(""), id: "main") {
            MainView()
                .environment(registry)
                .background {
                    // 标题栏彻底透明化（否则顶部会残留一条标题栏材质）；
                    // 无顶栏后开启背景拖动移动窗口。
                    // SwiftUI 会在窗口状态变化时重置这些属性，故通过 WindowAccessor 持续重设。
                    WindowAccessor { window in
                        if window.titleVisibility != .hidden {
                            window.titleVisibility = .hidden
                        }
                        if !window.titlebarAppearsTransparent {
                            window.titlebarAppearsTransparent = true
                        }
                        if !window.isMovableByWindowBackground {
                            window.isMovableByWindowBackground = true
                        }
                    }
                }
        }
        .defaultSize(width: 780, height: 500)
    }
}

/// 访问视图所属 NSWindow 的辅助视图（SwiftUI 未公开窗口样式等 AppKit 设置）。
/// 通过 NSWindow.didUpdateNotification 持续重设配置，防止 SwiftUI 重置窗口属性。
struct WindowAccessor: NSViewRepresentable {
    let configure: (NSWindow) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(configure: configure)
    }

    /// viewDidMoveToWindow 是获取 window 的可靠时机（DispatchQueue.async 不保证已挂载）。
    func makeNSView(context: Context) -> AccessorView {
        let view = AccessorView()
        view.onWindow = { window in
            configure(window)
            context.coordinator.observe(window)
        }
        return view
    }

    func updateNSView(_ nsView: AccessorView, context: Context) {
        if let window = nsView.window {
            configure(window)
        }
    }

    final class AccessorView: NSView {
        var onWindow: ((NSWindow) -> Void)?

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let window {
                onWindow?(window)
            }
        }
    }

    @MainActor
    final class Coordinator: NSObject {
        private let configure: (NSWindow) -> Void
        private weak var observedWindow: NSWindow?

        init(configure: @escaping (NSWindow) -> Void) {
            self.configure = configure
        }

        /// 窗口可能被重建，允许换绑到新窗口。
        func observe(_ window: NSWindow) {
            guard observedWindow !== window else { return }
            if let old = observedWindow {
                NotificationCenter.default.removeObserver(
                    self, name: NSWindow.didUpdateNotification, object: old)
            }
            observedWindow = window
            NotificationCenter.default.addObserver(
                self, selector: #selector(windowDidUpdate),
                name: NSWindow.didUpdateNotification, object: window)
        }

        @objc private func windowDidUpdate() {
            guard let window = observedWindow else { return }
            configure(window)
        }

        deinit {
            NotificationCenter.default.removeObserver(self)
        }
    }
}

/// 应用代理：管理激活策略、主窗口唤回。
/// 默认是 LSUIElement 菜单栏应用（.accessory）——该策略下窗口不参与前台调度和 App 切换器；
/// 主窗口可见（或最小化）时切换为 .regular，关闭后切回 .accessory。
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    /// 主界面重建需要 registry：窗口被关闭后 SwiftUI Window 场景已销毁，只能编程方式重建。
    var registry: ToolRegistry?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // 显式设置运行中应用的图标：前台调度/Dock 取的是 applicationIconImage，
        // 不依赖 LaunchServices 缓存（曾因旧缓存显示空白图标）。
        if let url = Bundle.main.url(forResource: "AppIcon", withExtension: "icns"),
           let image = NSImage(contentsOf: url) {
            NSApp.applicationIconImage = image
        }
        let nc = NotificationCenter.default
        for name in [NSWindow.didBecomeKeyNotification, NSWindow.willCloseNotification,
                     NSWindow.didChangeOcclusionStateNotification,
                     NSWindow.didMiniaturizeNotification, NSWindow.didDeminiaturizeNotification] {
            nc.addObserver(self, selector: #selector(windowVisibilityChanged),
                           name: name, object: nil)
        }
        nc.addObserver(self, selector: #selector(handleOpenMainWindow),
                       name: .openMainWindow, object: nil)
    }

    /// 「打开主界面」：窗口还在就置前（含最小化恢复）；已被关闭则编程方式重建。
    /// 接收方必须挂在常驻对象上——挂在 MainView 上会随关窗销毁，通知无人接收。
    @objc private func handleOpenMainWindow() {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate()
        if let window = NSApp.windows.first(where: { $0.styleMask.contains(.titled) }) {
            if window.isMiniaturized { window.deminiaturize(nil) }
            window.makeKeyAndOrderFront(nil)
            return
        }
        guard let registry else { return }
        let hosting = NSHostingController(rootView: MainView().environment(registry))
        let window = NSWindow(contentViewController: hosting)
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.isMovableByWindowBackground = true
        window.setContentSize(NSSize(width: 780, height: 500))
        window.center()
        window.makeKeyAndOrderFront(nil)
    }

    /// 关闭主窗口不等于退出：菜单栏读数、日历同步等后台功能继续运行。
    /// 之前 App 有常驻 MenuBarExtra 场景兜底；移除后只剩 Window 场景，
    /// 这里必须显式返回 false，否则关窗口会退出进程。
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    @objc private func windowVisibilityChanged() {
        // willClose 触发时窗口仍是可见状态，延迟一拍再判断
        DispatchQueue.main.async { [weak self] in
            self?.updateActivationPolicy()
        }
    }

    private func updateActivationPolicy() {
        // 最小化的窗口 isVisible 为 false，但仍然存在——否则切回 .accessory 后
        // Dock 图标消失，用户无法通过 Dock/Cmd-Tab 找回窗口
        let hasWindow = NSApp.windows.contains {
            $0.styleMask.contains(.titled) && ($0.isVisible || $0.isMiniaturized)
        }
        // 全部监控项停用且窗口已关闭时，保持 .regular 留 Dock 图标作兜底入口，
        // 否则进程无任何 UI 入口（无 Dock、无菜单栏、无 Cmd-Tab）
        let anyMonitorEnabled = registry?.tools.contains {
            ($0 as? MonitorTool)?.isEnabled ?? false
        } ?? false
        NSApp.setActivationPolicy((hasWindow || !anyMonitorEnabled) ? .regular : .accessory)
    }
}

import SwiftUI
import Foundation

/// 小工具的基类：每个工具都有名字、图标、图标颜色、开关状态，以及一个「主页面」视图。
@Observable
@MainActor
class Tool: Identifiable {
    let id: String
    let name: String
    let icon: String
    let tint: Color
    var isEnabled: Bool
    /// 轮询间隔（秒），仅轮询型工具使用；持久化到 UserDefaults。
    private(set) var pollingInterval: TimeInterval

    init(id: String, name: String, icon: String, tint: Color = .accentColor, defaultEnabled: Bool) {
        self.id = id
        self.name = name
        self.icon = icon
        self.tint = tint
        self.isEnabled = UserDefaults.standard.object(forKey: "tool.\(id).enabled") as? Bool ?? defaultEnabled
        let stored = UserDefaults.standard.object(forKey: "tool.\(id).interval") as? TimeInterval ?? 2.0
        // 持久化值可能被 defaults write 写入 0/负值，夹取下限防止 Timer 空转
        self.pollingInterval = max(1.0, stored)
    }

    /// 开关状态变更（持久化到 UserDefaults）
    func setEnabled(_ value: Bool) {
        isEnabled = value
        UserDefaults.standard.set(value, forKey: "tool.\(id).enabled")
    }

    /// 修改轮询间隔（持久化）；轮询型工具立即按新间隔重启定时器。
    func setPollingInterval(_ value: TimeInterval) {
        pollingInterval = max(1.0, value)
        UserDefaults.standard.set(pollingInterval, forKey: "tool.\(id).interval")
        restartPolling()
    }

    /// 子类覆盖：以当前 pollingInterval 重启轮询定时器。
    func restartPolling() {}

    /// 主页面内容（子类覆盖）
    func makeMainView() -> AnyView { AnyView(EmptyView()) }
}

/// 工具注册表：持有所有工具，平台从这里读取侧边栏和主页面。
@Observable
@MainActor
final class ToolRegistry {
    var tools: [Tool]

    init(tools: [Tool]) {
        self.tools = tools
    }
}

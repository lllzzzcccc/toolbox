import SwiftUI
import Foundation

/// 小工具的基类：每个工具都有名字、图标、开关状态，以及一个「主页面」视图。
@Observable
@MainActor
class Tool: Identifiable {
    let id: String
    let name: String
    let icon: String
    var isEnabled: Bool

    init(id: String, name: String, icon: String, defaultEnabled: Bool) {
        self.id = id
        self.name = name
        self.icon = icon
        self.isEnabled = UserDefaults.standard.object(forKey: "tool.\(id).enabled") as? Bool ?? defaultEnabled
    }

    /// 开关状态变更（持久化到 UserDefaults）
    func setEnabled(_ value: Bool) {
        isEnabled = value
        UserDefaults.standard.set(value, forKey: "tool.\(id).enabled")
    }

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

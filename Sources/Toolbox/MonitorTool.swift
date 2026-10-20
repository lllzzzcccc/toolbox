import SwiftUI
import Foundation
import CSysInfo

/// 单项负载监控（CPU / 内存 / 磁盘各一个实例）：每 2 秒采样，各自独立启停。
@Observable
@MainActor
final class MonitorTool: Tool {
    /// 菜单栏摘要用的短名（如 "CPU"）。
    let shortName: String
    /// 菜单栏菜单顶部显示的详情行（点击状态项弹出菜单时刷新）。
    let details: (MonitorTool) -> [String]
    private(set) var percent: Double = 0
    private(set) var lastSampledAt: Date?

    private let sampler: () -> Double
    private var timer: Timer?

    init(id: String, name: String, shortName: String, icon: String,
         tint: Color, sampler: @escaping () -> Double,
         details: @escaping (MonitorTool) -> [String] = { _ in [] }) {
        self.shortName = shortName
        self.details = details
        self.sampler = sampler
        super.init(id: id, name: name, icon: icon, tint: tint, defaultEnabled: true)
        if isEnabled { start() }
    }

    func start() {
        guard timer == nil else { return }
        sample()
        timer = Timer.scheduledTimer(withTimeInterval: pollingInterval, repeats: true) { _ in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.sample()
            }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    override func restartPolling() {
        guard timer != nil else { return }
        stop()
        start()
    }

    override func setEnabled(_ value: Bool) {
        super.setEnabled(value)
        value ? start() : stop()
    }

    private func sample() {
        percent = sampler()
        lastSampledAt = Date()
    }

    override func makeMainView() -> AnyView {
        AnyView(MonitorToolView(tool: self))
    }
}

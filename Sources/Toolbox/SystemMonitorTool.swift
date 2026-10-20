import SwiftUI
import Foundation
import CSysInfo

/// 「系统监控」工具：每 2 秒采样一次 CPU 与内存，菜单栏显示摘要。
@Observable
@MainActor
final class SystemMonitorTool: Tool {
    var cpuPercent: Double = 0
    var memPercent: Double = 0

    private var timer: Timer?

    override init(id: String = "monitor", name: String = "系统监控",
                  icon: String = "gauge.medium", defaultEnabled: Bool = true) {
        super.init(id: id, name: name, icon: icon, defaultEnabled: defaultEnabled)
        if isEnabled { start() }
    }

    var statusText: String {
        String(format: "CPU %.0f%% · 内存 %.0f%%", cpuPercent, memPercent)
    }

    func start() {
        guard timer == nil else { return }
        sample()
        timer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.sample()
            }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    override func setEnabled(_ value: Bool) {
        super.setEnabled(value)
        value ? start() : stop()
    }

    private func sample() {
        cpuPercent = sysinfo_cpu_usage()
        memPercent = sysinfo_mem_usage()
    }

    override func makeMainView() -> AnyView {
        AnyView(MonitorToolView(monitor: self))
    }
}

import SwiftUI

/// 「系统监控」工具的主页面：CPU / 内存的进度条详情。
struct MonitorToolView: View {
    let monitor: SystemMonitorTool

    var body: some View {
        @Bindable var monitor = monitor
        VStack(alignment: .leading, spacing: 24) {
            Text("系统监控")
                .font(.title2.bold())

            GaugeRow(title: "CPU", icon: "cpu", value: monitor.cpuPercent, color: .blue)
            GaugeRow(title: "内存", icon: "memorychip", value: monitor.memPercent, color: .green)

            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

struct GaugeRow: View {
    let title: String
    let icon: String
    let value: Double
    let color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label(title, systemImage: icon)
                    .font(.headline)
                Spacer()
                Text("\(value, specifier: "%.1f")%")
                    .font(.title3.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            ProgressView(value: value, total: 100)
                .tint(color)
        }
    }
}

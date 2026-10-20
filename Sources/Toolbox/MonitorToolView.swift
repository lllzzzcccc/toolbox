import SwiftUI

/// 「系统监控」工具的主页面：CPU / 内存的进度条详情。
struct MonitorToolView: View {
    let monitor: SystemMonitorTool

    var body: some View {
        Form {
            Section {
                VStack(spacing: 16) {
                    GaugeRow(title: "CPU", icon: "cpu", value: monitor.cpuPercent)
                    GaugeRow(title: "内存", icon: "memorychip", value: monitor.memPercent)
                }
                .padding(.vertical, 4)
            } header: {
                HStack {
                    Text("实时负载")
                    Spacer()
                    if let updated = monitor.lastSampledAt {
                        Text("更新于 \(updated, style: .time)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle("系统监控")
    }
}

struct GaugeRow: View {
    let title: String
    let icon: String
    let value: Double

    /// 负载着色：绿 → 黄 → 红。
    private var tint: Color {
        switch value {
        case ..<60: .green
        case ..<85: .yellow
        default: .red
        }
    }

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
            ProgressView(value: min(max(value, 0), 100), total: 100)
                .tint(tint)
        }
        .accessibilityElement(children: .combine)
    }
}

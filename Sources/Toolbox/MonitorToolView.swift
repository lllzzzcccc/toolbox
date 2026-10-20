import SwiftUI

/// 单个监控项（CPU / 内存 / 磁盘）的主页面：大标题 + 启停开关 + 实时负载。
struct MonitorToolView: View {
    let tool: MonitorTool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(tool.name)
                .font(.system(size: 25, weight: .bold))
                .padding(.horizontal, 20)
                .padding(.top, 16)
                .padding(.bottom, 12)

            Form {
                Section {
                    Toggle(isOn: Binding(
                        get: { tool.isEnabled },
                        set: { tool.setEnabled($0) }
                    )) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("启用")
                            Text("每 \(Int(tool.pollingInterval)) 秒自动采样，菜单栏同步显示")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .toggleStyle(.switch)

                    Picker("采样间隔", selection: Binding(
                        get: { tool.pollingInterval },
                        set: { tool.setPollingInterval($0) }
                    )) {
                        Text("1 秒").tag(1.0)
                        Text("2 秒").tag(2.0)
                        Text("5 秒").tag(5.0)
                        Text("10 秒").tag(10.0)
                        Text("30 秒").tag(30.0)
                        Text("60 秒").tag(60.0)
                    }
                    .pickerStyle(.menu)
                }

                if tool.isEnabled {
                    Section {
                        GaugeRow(title: tool.name, icon: tool.icon, value: tool.percent, tint: tool.tint)
                            .padding(.vertical, 4)
                    } header: {
                        HStack {
                            Text("实时负载")
                            Spacer()
                            if let updated = tool.lastSampledAt {
                                Text("更新于 \(updated, style: .time)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                } else {
                    Section {
                        ContentUnavailableView(
                            "监控已停用",
                            systemImage: "pause.circle",
                            description: Text("开启上方开关以查看实时负载。")
                        )
                    }
                }
            }
            .formStyle(.grouped)
        }
    }
}

struct GaugeRow: View {
    let title: String
    let icon: String
    let value: Double
    let tint: Color

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

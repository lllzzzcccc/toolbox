import SwiftUI
import CSyncCal

/// 「日历同步」工具：监控日历 DB 修改时间，变更后（去抖）调用 C 库同步。
@Observable
@MainActor
final class CalendarSyncTool: Tool {
    private var timer: Timer?
    private(set) var isSyncing = false
    private(set) var lastSyncAt: Date?
    private var lastMtime: Date?
    private var pendingSince: Date?

    private let dbPath = ("~" as NSString).expandingTildeInPath
        + "/Library/Containers/com.xdiarys.www/Data/desktopcal.sqlite"

    override init(id: String = "calendarsync", name: String = "日历同步",
                  icon: String = "arrow.triangle.2.circlepath",
                  tint: Color = .orange,
                  defaultEnabled: Bool = true) {
        super.init(id: id, name: name, icon: icon, tint: tint, defaultEnabled: defaultEnabled)
        sync_cal_init()
        if isEnabled { start() }
    }

    func start() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: pollingInterval, repeats: true) { _ in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.tick()
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

    /// 按 pollingInterval 检查 DB 修改时间；变更后去抖 3 秒再同步。
    private func tick() {
        let attrs = try? FileManager.default.attributesOfItem(atPath: dbPath)
        guard let mtime = attrs?[.modificationDate] as? Date else { return }

        if lastMtime == nil {
            lastMtime = mtime
            return
        }
        if mtime != lastMtime {
            lastMtime = mtime
            pendingSince = Date()
        }
        if let p = pendingSince, Date().timeIntervalSince(p) >= 3.0 {
            pendingSince = nil
            syncNow()
        }
    }

    /// 立即执行一次同步（后台线程，带防重入）。
    func syncNow() {
        guard !isSyncing else { return }
        isSyncing = true
        DispatchQueue.global(qos: .utility).async {
            sync_cal_do_sync()
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.isSyncing = false
                self.lastSyncAt = Date()
            }
        }
    }

    override func makeMainView() -> AnyView {
        AnyView(CalendarSyncView(tool: self))
    }
}

struct CalendarSyncView: View {
    let tool: CalendarSyncTool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("日历同步")
                .font(.system(size: 25, weight: .bold))
                .padding(.horizontal, 20)
                .padding(.top, 16)
                .padding(.bottom, 12)

            Form {
            Section("状态") {
                Toggle(isOn: Binding(
                    get: { tool.isEnabled },
                    set: { tool.setEnabled($0) }
                )) {
                    Text("自动同步")
                }
                .toggleStyle(.switch)

                Picker("检测间隔", selection: Binding(
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

                LabeledContent("运行状态") {
                    HStack(spacing: 6) {
                        Circle()
                            .fill(tool.isEnabled ? Color.green : Color.secondary)
                            .frame(width: 8, height: 8)
                        Text(tool.isEnabled ? "运行中（每 \(Int(tool.pollingInterval)) 秒检测变更）" : "已停止")
                    }
                }
                if let last = tool.lastSyncAt {
                    LabeledContent("上次同步") {
                        Text(last.formatted(date: .omitted, time: .standard))
                    }
                }
            }

            Section("说明") {
                Text("开启后，自动把「日历清单」（CalendarTask）里的改动上传到服务器。\n下载方向由 App 自身处理，这里只负责「本地一改 → 自动推送」。")
                    .foregroundStyle(.secondary)
                    .lineSpacing(3)
                    .padding(.vertical, 2)
            }

            Section {
                Button {
                    tool.syncNow()
                } label: {
                    if tool.isSyncing {
                        HStack(spacing: 6) {
                            ProgressView()
                                .controlSize(.small)
                            Text("同步中…")
                        }
                    } else {
                        Label("立即同步一次", systemImage: "arrow.triangle.2.circlepath")
                    }
                }
                .disabled(tool.isSyncing)
            }
        }
        .formStyle(.grouped)
        }
    }
}

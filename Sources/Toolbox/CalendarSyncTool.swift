import SwiftUI
import CSyncCal

/// 「日历同步」工具：监控日历 DB 修改时间，变更后（去抖）调用 C 库同步。
@Observable
@MainActor
final class CalendarSyncTool: Tool {
    private var timer: Timer?
    private var syncing = false
    private var lastMtime: Date?
    private var pendingSince: Date?

    private let dbPath = ("~" as NSString).expandingTildeInPath
        + "/Library/Containers/com.xdiarys.www/Data/desktopcal.sqlite"

    override init(id: String = "calendarsync", name: String = "日历同步",
                  icon: String = "arrow.triangle.2.circlepath",
                  defaultEnabled: Bool = true) {
        super.init(id: id, name: name, icon: icon, defaultEnabled: defaultEnabled)
        sync_cal_init()
        if isEnabled { start() }
    }

    func start() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
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

    /// 每 2 秒检查一次 DB 修改时间；变更后去抖 3 秒再同步。
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
        guard !syncing else { return }
        syncing = true
        DispatchQueue.global(qos: .utility).async { [weak self] in
            sync_cal_do_sync()
            Task { @MainActor in self?.syncing = false }
        }
    }

    override func makeMainView() -> AnyView {
        AnyView(CalendarSyncView(tool: self))
    }
}

struct CalendarSyncView: View {
    let tool: CalendarSyncTool

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("日历同步")
                .font(.title2.bold())

            Text("开启后，自动把「日历清单」（CalendarTask）里的改动上传到服务器。\n下载方向由 App 自身处理，这里只负责「本地一改 → 自动推送」。")
                .foregroundStyle(.secondary)
                .lineSpacing(3)

            Button {
                tool.syncNow()
            } label: {
                Label("立即同步一次", systemImage: "arrow.triangle.2.circlepath")
            }

            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

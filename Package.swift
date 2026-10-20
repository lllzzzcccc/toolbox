// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Toolbox",
    platforms: [.macOS(.v14)],
    targets: [
        // C 辅助库：CPU/内存采样（Mach API）
        .target(name: "CSysInfo", path: "Sources/CSysInfo"),
        // C 库：日历同步逻辑（复用 sync_cal 的已验证代码）
        .target(
            name: "CSyncCal",
            path: "Sources/CSyncCal",
            linkerSettings: [
                .linkedLibrary("sqlite3"),
                .linkedLibrary("curl"),
                .linkedFramework("IOKit"),
                .linkedFramework("CoreFoundation"),
            ]
        ),
        // SwiftUI 菜单栏应用
        .executableTarget(
            name: "Toolbox",
            dependencies: ["CSysInfo", "CSyncCal"],
            path: "Sources/Toolbox"
        ),
    ],
    // 锁定 Swift 5 语言模式（宽松并发）。本代码含 @MainActor + Timer 闭包，
    // 在 Swift 6 严格并发下会报错；锁定 v5 后这些只是警告，构建可通过。
    swiftLanguageModes: [.v5]
)

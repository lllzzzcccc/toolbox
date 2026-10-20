#!/usr/bin/env swift
// 生成 Toolbox 应用图标：圆角方形蓝色渐变底 + 白色工具符号（macOS 11+ 风格）。
// 用法：swift Scripts/generate_icon.swift   （在仓库根目录运行）
// 产物：.build/AppIcon.iconset/，之后用 iconutil 转成 Assets/AppIcon.icns
import AppKit

let iconsetURL = URL(fileURLWithPath: ".build/AppIcon.iconset")
try? FileManager.default.removeItem(at: iconsetURL)
try! FileManager.default.createDirectory(at: iconsetURL, withIntermediateDirectories: true)

let entries: [(name: String, px: Int)] = [
    ("icon_16x16", 16), ("icon_16x16@2x", 32),
    ("icon_32x32", 32), ("icon_32x32@2x", 64),
    ("icon_128x128", 128), ("icon_128x128@2x", 256),
    ("icon_256x256", 256), ("icon_256x256@2x", 512),
    ("icon_512x512", 512), ("icon_512x512@2x", 1024),
]

let topColor = NSColor(calibratedRed: 0.22, green: 0.51, blue: 0.99, alpha: 1)
let bottomColor = NSColor(calibratedRed: 0.05, green: 0.27, blue: 0.83, alpha: 1)

func render(_ px: Int) -> NSImage {
    let size = NSSize(width: px, height: px)
    let image = NSImage(size: size)
    image.lockFocus()
    defer { image.unlockFocus() }

    let rect = NSRect(origin: .zero, size: size)
    let corner = CGFloat(px) * 0.2247
    let path = NSBezierPath(roundedRect: rect, xRadius: corner, yRadius: corner)

    // 纵向渐变底
    NSGradient(starting: topColor, ending: bottomColor)!.draw(in: path, angle: 90)

    // 内描边高光
    let lineWidth = max(1, CGFloat(px) * 0.006)
    let strokePath = NSBezierPath(roundedRect: rect.insetBy(dx: lineWidth / 2, dy: lineWidth / 2),
                                  xRadius: corner, yRadius: corner)
    strokePath.lineWidth = lineWidth
    NSColor.white.withAlphaComponent(0.22).setStroke()
    strokePath.stroke()

    // 中央工具符号（白色 + 柔和投影）
    if let symbol = NSImage(systemSymbolName: "wrench.and.screwdriver.fill",
                            accessibilityDescription: nil)?
        .withSymbolConfiguration(.init(paletteColors: [.white])) {
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
        shadow.shadowBlurRadius = CGFloat(px) * 0.022
        shadow.shadowOffset = NSSize(width: 0, height: -CGFloat(px) * 0.01)
        shadow.set()

        let side = CGFloat(px) * 0.56
        let symbolRect = NSRect(x: (CGFloat(px) - side) / 2,
                                y: (CGFloat(px) - side) / 2,
                                width: side, height: side)
        symbol.draw(in: symbolRect)
    }
    return image
}

func savePNG(_ image: NSImage, to url: URL) {
    guard let tiff = image.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff),
          let data = rep.representation(using: .png, properties: [:]) else {
        FileHandle.standardError.write("failed to encode \(url.lastPathComponent)\n".data(using: .utf8)!)
        exit(1)
    }
    try! data.write(to: url)
}

for entry in entries {
    savePNG(render(entry.px), to: iconsetURL.appendingPathComponent("\(entry.name).png"))
}
print("iconset written to \(iconsetURL.path)")

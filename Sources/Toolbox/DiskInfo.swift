import Foundation

/// 磁盘容量口径对齐「设置 > 通用 > 储存空间」：
/// 总容量 = APFS 容器容量；可用 = 重要用途可用容量（含系统可清除的缓存空间）。
/// 返回 nil 表示读取失败。
func diskCapacity() -> (total: UInt64, avail: UInt64)? {
    guard let values = try? URL(fileURLWithPath: "/")
        .resourceValues(forKeys: [.volumeTotalCapacityKey,
                                  .volumeAvailableCapacityForImportantUsageKey]),
          let total = values.volumeTotalCapacity,
          let avail = values.volumeAvailableCapacityForImportantUsage else {
        return nil
    }
    return (UInt64(max(total, 0)), UInt64(max(avail, 0)))
}

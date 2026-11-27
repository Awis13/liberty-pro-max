import CryptoKit
import Foundation

enum FileDigest {
  static func hasSize(_ url: URL, _ size: Int) -> Bool {
    let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
    return values?.isRegularFile == true && values?.fileSize == size
  }

  /// Read at most 1 GiB plus one byte to confirm the expected end of the file.
  static func sha256(_ url: URL, size: Int) throws -> String? {
    guard hasSize(url, size), (1...(1 << 30)).contains(size) else { return nil }
    let file = try FileHandle(forReadingFrom: url)
    defer { try? file.close() }
    let chunkSize = 4 * 1024 * 1024
    var remaining = size
    var hash = SHA256()
    for _ in 0..<256 where remaining > 0 {
      try Task.checkCancellation()
      guard let bytes = try file.read(upToCount: min(remaining, chunkSize)), !bytes.isEmpty else {
        return nil
      }
      hash.update(data: bytes)
      remaining -= bytes.count
    }
    guard remaining == 0, (try file.read(upToCount: 1) ?? Data()).isEmpty else { return nil }
    return hash.finalize().map { String(format: "%02x", $0) }.joined()
  }
}

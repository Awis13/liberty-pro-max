import Foundation

/// Read VS_FIXEDFILEINFO from the PE version resource of a 32-bit x86 image.
/// https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
/// https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo
struct WindowsFileVersion: CustomStringConvertible {
  let major: Int
  let minor: Int
  let patch: Int
  let revision: Int

  var description: String { "\(major).\(minor).\(patch).\(revision)" }
  var isCompleteEdition: Bool { major == 1 && minor == 2 && patch == 0 }

  static func read(_ url: URL) throws -> WindowsFileVersion? {
    let maxSize = 64 * 1024 * 1024
    let handle = try FileHandle(forReadingFrom: url)
    defer { try? handle.close() }
    guard let image = try handle.read(upToCount: maxSize + 1),
          image.count <= maxSize else { return nil }
    return PortableExecutable(bytes: image).fileVersion()
  }

  fileprivate init?(resource: Data, at offset: Int, size: Int) {
    let key: [UInt8] = "VS_VERSION_INFO\u{0}".utf8.flatMap { [$0, 0] }
    let keyEnd = 6 + key.count
    let fixedInfoOffset = (keyEnd + 3) & ~3
    let fixedInfo = offset + fixedInfoOffset
    let fixedInfoSize = 52
    guard size >= fixedInfoOffset + fixedInfoSize,
          let length = resource.uint16(at: offset), length <= size,
          length >= fixedInfoOffset + fixedInfoSize,
          resource.uint16(at: offset + 2) == fixedInfoSize,
          resource.uint16(at: offset + 4) == 0,
          resource[(offset + 6)..<(offset + keyEnd)].elementsEqual(key),
          resource.uint32(at: fixedInfo) == 0xfeef04bd,
          let high = resource.uint32(at: fixedInfo + 8),
          let low = resource.uint32(at: fixedInfo + 12) else { return nil }
    major = high >> 16
    minor = high & 0xffff
    patch = low >> 16
    revision = low & 0xffff
  }
}

private struct PortableExecutable {
  let bytes: Data

  func fileVersion() -> WindowsFileVersion? {
    guard bytes.uint16(at: 0) == 0x5a4d,
          let header = bytes.uint32(at: 0x3c), header >= 64,
          bytes.uint32(at: header) == 0x00004550,
          bytes.uint16(at: header + 4) == 0x014c,
          let sectionCount = bytes.uint16(at: header + 6),
          (1...96).contains(sectionCount),
          let optionalSize = bytes.uint16(at: header + 20), optionalSize >= 120 else {
      return nil
    }
    let optional = header + 24
    let sections = optional + optionalSize
    guard sections + sectionCount * 40 <= bytes.count,
          bytes.uint16(at: optional) == 0x010b,
          let directoryCount = bytes.uint32(at: optional + 92), directoryCount >= 3,
          let resourceRVA = bytes.uint32(at: optional + 112), resourceRVA > 0,
          let resourceSize = bytes.uint32(at: optional + 116), resourceSize >= 16,
          let resources = sectionData(rva: resourceRVA, size: resourceSize,
                                      sections: sections, count: sectionCount) else { return nil }

    // RT_VERSION is type 16. Its resource identifier is 1.
    guard let type = subdirectory(in: resources, at: 0, id: 16),
          let name = subdirectory(in: resources, at: type, id: 1),
          let languages = entries(in: resources, at: name) else { return nil }
    return versionIn(languages, resources: resources, baseRVA: resourceRVA)
  }

  private func subdirectory(in resources: Data, at directory: Int, id: Int) -> Int? {
    let directoryFlag = 0x80000000
    guard let entry = entries(in: resources, at: directory)?.first(where: {
      resources.uint32(at: $0) == id
    }), let target = resources.uint32(at: entry + 4), target >= directoryFlag else {
      return nil
    }
    return target - directoryFlag
  }

  private func versionIn(_ languages: StrideTo<Int>, resources: Data,
                         baseRVA: Int) -> WindowsFileVersion? {
    for language in languages {
      guard let entry = resources.uint32(at: language + 4), entry <= resources.count - 16,
            let rva = resources.uint32(at: entry), rva >= baseRVA,
            let size = resources.uint32(at: entry + 4) else { continue }
      let offset = rva - baseRVA
      guard offset + size <= resources.count else { continue }
      if let version = WindowsFileVersion(resource: resources, at: offset, size: size) {
        return version
      }
    }
    return nil
  }

  private func entries(in resources: Data, at directory: Int) -> StrideTo<Int>? {
    guard let named = resources.uint16(at: directory + 12),
          let numbered = resources.uint16(at: directory + 14) else { return nil }
    let start = directory + 16
    let end = start + (named + numbered) * 8
    guard end <= resources.count else { return nil }
    return stride(from: start, to: end, by: 8)
  }

  private func sectionData(rva: Int, size: Int, sections: Int, count: Int) -> Data? {
    for section in stride(from: sections, to: sections + count * 40, by: 40) {
      guard let address = bytes.uint32(at: section + 12), rva >= address,
            let rawSize = bytes.uint32(at: section + 16),
            let rawOffset = bytes.uint32(at: section + 20), rawOffset > 0 else { continue }
      let relative = rva - address
      let start = rawOffset + relative
      guard relative + size <= rawSize, start + size <= bytes.count else { continue }
      return bytes.subdata(in: start..<(start + size))
    }
    return nil
  }
}

// Read unaligned little-endian fields after checking the entire field is in bounds.
private extension Data {
  func uint16(at offset: Int) -> Int? {
    guard offset >= 0, offset <= count - 2 else { return nil }
    return Int(self[offset]) | Int(self[offset + 1]) << 8
  }

  func uint32(at offset: Int) -> Int? {
    guard let low = uint16(at: offset), let high = uint16(at: offset + 2) else { return nil }
    return low | high << 16
  }
}

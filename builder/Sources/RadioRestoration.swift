// Verify the original Radio Restoration files and install a playlist as a mod.

import Darwin
import Foundation

struct RadioOptions: Sendable {
  var postCut = false
  var beta = false
  var split = true

  var presets: [String] {
    let playlist: String
    if beta {
      playlist = postCut ? "opALL.dat" : "opVANILLABETA.dat"
    } else {
      playlist = postCut ? "opCLASSIC.dat" : "opVANILLA.dat"
    }
    guard split else { return [playlist] }
    return [playlist, "opSPLITbase.dat", beta ? "opSPLITBETA.dat" : "opSPLITVANILLA.dat"]
  }
}

struct RadioInput: Sendable {
  static let archiveName = "data1.dat"
  static let rpfPath = "update/pc/audio/sfx/RADIO_RESTORATION.rpf"
  static let releaseURL = URL(string: "https://github.com/Tomasak/GTA-Downgraders/releases/tag/iv-latest")!

  let source: URL
  let directory: URL
  let isUnpacked: Bool

  init(_ url: URL) throws {
    source = url.standardizedFileURL
    if source.lastPathComponent.caseInsensitiveCompare(Self.archiveName) == .orderedSame {
      directory = source.deletingLastPathComponent()
      isUnpacked = false
      return
    }
    let parts = Self.rpfPath.split(separator: "/").map(String.init)
    let suffix = source.pathComponents.suffix(parts.count)
    guard suffix.map({ $0.lowercased() }) == parts.map({ $0.lowercased() }) else {
      throw Failure("Choose data1.dat or the extracted RADIO_RESTORATION.rpf under update/pc/audio/sfx.")
    }
    directory = parts.reduce(source) { current, _ in current.deletingLastPathComponent() }
    isUnpacked = true
  }
}

struct RadioRequest: Sendable {
  let input: RadioInput
  let options: RadioOptions
}

struct PreparedRadio: Sendable {
  private static let name = "Radio Restoration"
  private let work: URL
  private let destination: URL

  static var modsFolder: URL {
    let environment = ProcessInfo.processInfo.environment
    if let root = environment["XDG_DATA_HOME"], !root.isEmpty {
      return URL(fileURLWithPath: root).appendingPathComponent("gtaiv/drive_c/mods")
    }
    return FileManager.default.homeDirectoryForCurrentUser
      .appendingPathComponent("Library/Application Support/gtaiv/drive_c/mods")
  }

  /// Extract into a hidden folder on the same volume as the final mod.
  static func prepare(_ request: RadioRequest, in mods: URL,
                      catalog: URL) async throws -> PreparedRadio {
    let fm = FileManager.default
    let destination = mods.appendingPathComponent(name, isDirectory: true)
    guard !fm.fileExists(atPath: destination.path) else {
      throw Failure("Move the existing Radio Restoration folder out of Mods before building.")
    }
    try fm.createDirectory(at: mods, withIntermediateDirectories: true)
    let work = mods.appendingPathComponent(".radio-" + UUID().uuidString, isDirectory: true)
    try fm.createDirectory(at: work, withIntermediateDirectories: false,
                           attributes: [.posixPermissions: 0o700])
    let stage = PreparedRadio(work: work, destination: destination)
    do {
      let package = try RadioPackage(catalog: catalog)
      try await package.stage(request, in: work)
      return stage
    } catch {
      stage.discard()
      throw error
    }
  }

  /// Publish the complete directory and refuse to overwrite another mod.
  func install() throws -> URL {
    let source = work.appendingPathComponent("mod")
    guard renamex_np(source.path, destination.path, UInt32(RENAME_EXCL)) == 0 else {
      let reason = String(cString: strerror(errno))
      throw Failure("Could not install Radio Restoration. \(reason)")
    }
    return destination
  }

  func discard() { try? FileManager.default.removeItem(at: work) }
}

private struct RadioPackage {
  private struct Archive: Decodable {
    struct Member: Decodable {
      let path: String
      let size: Int
      let sha256: String?
    }
    let sha256: String
    let size: Int
    let files: [Member]
  }

  private let catalog: [String: Archive]

  init(catalog url: URL) throws {
    catalog = try JSONDecoder().decode([String: Archive].self, from: Data(contentsOf: url))
  }

  func stage(_ request: RadioRequest, in work: URL) async throws {
    let names = [RadioInput.archiveName] + request.options.presets
    for name in names {
      try Task.checkCancellation()
      guard let archive = catalog[name] else {
        throw Failure("The Builder is missing the file list for \(name).")
      }
      let isBase = name == RadioInput.archiveName
      let unpacked = isBase && request.input.isUnpacked
      let directory: URL
      if unpacked {
        directory = request.input.directory
      } else {
        let source = isBase ? request.input.source : request.input.directory.appendingPathComponent(name)
        directory = try await extract(archive, from: source, named: name, in: work)
      }
      let selectedRPF = unpacked ? request.input.source : nil
      try stageFiles(archive.files, from: directory, in: work, selectedRPF: selectedRPF)
      if !unpacked { try FileManager.default.removeItem(at: directory) }
    }
    if request.options.split {
      let basePlaylist = work.appendingPathComponent("mod/" + RadioStationList.basePath)
      var metadata = try Data(contentsOf: basePlaylist)
      try RadioStationList.removeEpisodeStations(from: &metadata)
      try metadata.write(to: basePlaylist, options: .atomic)
    }
  }

  private func extract(_ archive: Archive, from source: URL,
                       named name: String, in work: URL) async throws -> URL {
    let fm = FileManager.default
    let copy = work.appendingPathComponent(name)
    try Self.copyFile(source, to: copy, size: archive.size, digest: archive.sha256)
    let directory = work.appendingPathComponent(name + "-files", isDirectory: true)
    let result = await Run.run("/usr/bin/unzip", ["-qq", "-n", copy.path, "-d", directory.path],
                               env: ["LC_ALL": "C", "UNZIP": "", "UNZIPOPT": ""])
    try Task.checkCancellation()
    guard result.ok else { throw Failure("Could not extract \(name).\n" + result.output.suffix(600)) }
    try fm.removeItem(at: copy)
    return directory
  }

  private func stageFiles(_ files: [Archive.Member], from directory: URL,
                          in work: URL, selectedRPF: URL?) throws {
    let fm = FileManager.default
    let overlay = work.appendingPathComponent("mod", isDirectory: true)
    for file in files {
      try Task.checkCancellation()
      let source = (file.path == RadioInput.rpfPath ? selectedRPF : nil)
        ?? directory.appendingPathComponent(file.path)
      let target = overlay.appendingPathComponent(Self.normalizeEpisode(file.path))
      try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
      if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
      if selectedRPF != nil {
        guard let digest = file.sha256 else { throw Failure("Missing checksum for \(file.path).") }
        try Self.copyFile(source, to: target, size: file.size, digest: digest)
      } else {
        try Self.checkFile(source, size: file.size)
        try fm.moveItem(at: source, to: target)
      }
    }
  }

  private static func normalizeEpisode(_ path: String) -> String {
    var parts = path.split(separator: "/").map(String.init)
    if parts.count >= 2 {
      switch parts[1].lowercased() {
      case "tbogt": parts[1] = "TBoGT"
      case "tlad": parts[1] = "TLAD"
      default: break
      }
    }
    return parts.joined(separator: "/")
  }

  private static func checkFile(_ url: URL, size: Int) throws {
    guard FileDigest.hasSize(url, size) else { throw invalidFile(url) }
  }

  private static func copyFile(_ source: URL, to target: URL, size: Int, digest: String) throws {
    try checkFile(source, size: size)
    try FileManager.default.copyItem(at: source, to: target)
    guard try FileDigest.sha256(target, size: size) == digest else {
      throw invalidFile(source)
    }
  }

  private static func invalidFile(_ url: URL) -> Failure {
    Failure("\(url.lastPathComponent) is missing or differs from the 23 May 2025 Radio Restoration release. Keep the extracted files together.")
  }
}

// Identify the game data and supply the executable used by both translators.

import Foundation

struct GameInput: Sendable {
  let folder: URL
  let needsPatch: Bool

  private static let executableName = "GTAIV.exe"
  static let patchURL = URL(string: "https://updates.rockstargames.com/patches/gtaiv/GTA_IV_EFIGS_PATCH_8.exe")!
  private static let patchSize = 132_317_817
  private static let patchDigest =
    "0e12b0cc45b9716fabd9b48305cf93329044dc977684d358a1e4b8fbf7356219"

  static var extractor: String? {
    ["/opt/homebrew/bin/7zz", "/usr/local/bin/7zz"].first {
      FileManager.default.isExecutableFile(atPath: $0)
    }
  }

  static func inspect(_ selection: URL) throws -> GameInput {
    let fm = FileManager.default
    let selected = selection.lastPathComponent == executableName
      ? selection.deletingLastPathComponent() : selection
    let nested = selected.appendingPathComponent("GTAIV", isDirectory: true)
    let folder = [selected, nested].first {
      (try? $0.appendingPathComponent(executableName)
        .resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
    }
    guard let folder else {
      throw Failure("Choose the GTA IV folder that contains GTAIV.exe.")
    }
    for name in ["pc", "common"] {
      let directory = folder.appendingPathComponent(name)
      var isDirectory: ObjCBool = false
      guard fm.fileExists(atPath: directory.path, isDirectory: &isDirectory),
            isDirectory.boolValue else {
        throw Failure("The game folder is missing the \(name) directory.")
      }
    }

    let executable = folder.appendingPathComponent(executableName)
    if try FileDigest.sha256(executable, size: Kit.exeSize) == Kit.exeSHA256 {
      return GameInput(folder: folder, needsPatch: false)
    }
    if try WindowsFileVersion.read(executable)?.isCompleteEdition == true {
      for name in ["TLAD", "TBoGT"] {
        let episode = folder.appendingPathComponent(name)
        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: episode.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
          throw Failure("The Complete Edition folder is missing \(name).")
        }
      }
      return GameInput(folder: folder, needsPatch: true)
    }
    throw Failure("Choose a Complete Edition install or Rockstar's GTAIV.exe 1.0.8.0.")
  }

  /// The staging folder belongs to the caller and is removed after the build.
  func executable(patch: URL?, staging: URL) async throws -> URL {
    if !needsPatch {
      let executable = folder.appendingPathComponent(Self.executableName)
      guard try FileDigest.sha256(executable, size: Kit.exeSize) == Kit.exeSHA256 else {
        throw Failure("GTAIV.exe changed. Choose the game folder again.")
      }
      return executable
    }
    guard let patch else { throw Failure("Choose Rockstar's Patch 8 installer.") }
    guard let extractor = Self.extractor else {
      throw Failure("Install 7-Zip with brew install sevenzip, then retry.")
    }
    return try await Self.extractPatch(patch, using: extractor, staging: staging)
  }

  private static func extractPatch(_ patch: URL, using extractor: String,
                                   staging: URL) async throws -> URL {
    let invalidPatch = Failure(
      "Choose the GTA IV Patch 8 installer for English, French, Italian, German and Spanish.")
    let source = patch.resolvingSymlinksInPath()
    guard FileDigest.hasSize(source, patchSize) else { throw invalidPatch }
    let installer = staging.appendingPathComponent("Patch8.exe")
    let executable = staging.appendingPathComponent(executableName)
    try FileManager.default.copyItem(at: source, to: installer)
    guard try FileDigest.sha256(installer, size: patchSize) == patchDigest else {
      throw invalidPatch
    }
    let result = await Run.run(extractor,
      ["x", "-so", "-bsp0", "--", installer.path, "$0/GTAIV.exe"],
      stdoutFile: executable.path)
    try Task.checkCancellation()
    guard result.ok else {
      throw Failure("Could not extract GTAIV.exe from Patch 8.\n" + result.output.suffix(600))
    }
    guard try FileDigest.sha256(executable, size: Kit.exeSize) == Kit.exeSHA256 else {
      throw Failure("Patch 8 did not yield the expected GTAIV.exe 1.0.8.0.")
    }
    return executable
  }
}

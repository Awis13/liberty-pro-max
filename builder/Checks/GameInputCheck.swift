// Exercise both inputs without publishing an app or editing a game install.

import Foundation

@main
struct GameInputCheck {
  static func main() async throws {
    let paths = CommandLine.arguments.dropFirst().map { URL(fileURLWithPath: $0) }
    guard paths.count <= 2 else {
      throw Failure("Pass the Patch 8 installer and, if available, a Complete Edition folder.")
    }
    let fm = FileManager.default
    let root = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try fm.createDirectory(at: root, withIntermediateDirectories: false)
    defer { try? fm.removeItem(at: root) }
    let game = root.appendingPathComponent("GTAIV")
    let staged = root.appendingPathComponent("staged")
    try fm.createDirectory(at: staged, withIntermediateDirectories: false)
    for name in ["pc", "common", "TLAD", "TBoGT"] {
      try fm.createDirectory(at: game.appendingPathComponent(name),
                             withIntermediateDirectories: true)
    }
    let executable = game.appendingPathComponent("GTAIV.exe")
    try checkVersionResource(executable)
    let input = try GameInput.inspect(root)
    try require(input.needsPatch, "Complete Edition requires Patch 8")
    print("PASS: malformed executables rejected and Complete Edition version read from PE resources")
    guard let patch = paths.first else { return }
    if paths.count == 2 {
      let selected = try GameInput.inspect(paths[1])
      let selectedExe = selected.folder.appendingPathComponent("GTAIV.exe")
      guard selected.needsPatch, let version = try WindowsFileVersion.read(selectedExe) else {
        throw Failure("Choose a Complete Edition folder with GTAIV.exe 1.2.0.x.")
      }
      print("Selected GTAIV.exe file version: \(version)")
    }
    let completeEdition = try Data(contentsOf: executable)
    try await rejectPatch(nil, input: input, staging: staged,
                          message: "Choose Rockstar's Patch 8 installer.")
    try await rejectPatch(executable, input: input, staging: staged,
                          message: "Choose the GTA IV Patch 8 installer")
    let extracted = try await input.executable(patch: patch, staging: staged)
    try require(extracted == staged.appendingPathComponent("GTAIV.exe"),
                "The build uses a staged copy")
    try require(Data(contentsOf: executable) == completeEdition,
                "The Complete Edition executable stays intact")

    try fm.removeItem(at: executable)
    try fm.copyItem(at: extracted, to: executable)
    let stock = try GameInput.inspect(executable)
    try require(!stock.needsPatch, "Stock 1.0.8.0 needs no installer")
    let original = try await stock.executable(patch: nil, staging: staged)
    try require(original.path == executable.path, "The stock path is used")
    try await rejectChangedInstaller(patch, input: input, root: root)
    print("PASS: Complete Edition, verified Patch 8, and stock 1.0.8.0")
  }

  private static func checkVersionResource(_ executable: URL) throws {
    // The former byte search accepted this file even though it has no PE header.
    let signatureOnly = Data([0x4d, 0x5a, 0xbd, 4, 0xef, 0xfe, 0, 0, 1, 0,
                              2, 0, 1, 0, 59, 0, 0, 0])
    let valid = versionFixture()
    var badDirectory = valid
    badDirectory[0x214] = 0xff
    var badRVA = valid
    badRVA[0x24b] = 0xff
    var unsupportedVersion = valid
    unsupportedVersion[0x290] = 3
    for invalid in [signatureOnly, Data(valid.prefix(0x2b0)), badDirectory, badRVA,
                    unsupportedVersion] {
      try invalid.write(to: executable)
      do {
        _ = try GameInput.inspect(executable)
      } catch let failure as Failure {
        try require(failure.localizedDescription.hasPrefix("Choose a Complete Edition install"),
                    "Rejected an invalid or unsupported executable")
        continue
      }
      throw Failure("The Builder accepted an invalid or unsupported executable.")
    }
    try valid.write(to: executable)
    try require(try WindowsFileVersion.read(executable)?.description == "1.2.0.59",
                "Use the file version, even when the product version differs")
  }

  private static func versionFixture() -> Data {
    var image = Data(count: 0x400)
    func write(_ offset: Int, _ value: UInt32) {
      for byte in 0..<4 {
        image[offset + byte] = UInt8(truncatingIfNeeded: value >> (byte * 8))
      }
    }

    // DOS header, x86 COFF header, and PE32 optional header.
    write(0, 0x5a4d)
    write(0x3c, 0x80)
    write(0x80, 0x00004550)
    write(0x84, 0x0001014c)  // One section, x86.
    write(0x94, 0xe0)
    write(0x98, 0x010b)
    write(0xf4, 16)
    write(0x108, 0x2000)
    write(0x10c, 0x200)

    // The resource section maps RVA 0x2000 to file offset 0x200.
    write(0x180, 0x200)
    write(0x184, 0x2000)
    write(0x188, 0x200)
    write(0x18c, 0x200)

    // Type, resource ID, language, and the resource data entry.
    write(0x20c, 0x00010000)
    write(0x210, 16)
    write(0x214, 0x80000018)
    write(0x224, 0x00010000)
    write(0x228, 1)
    write(0x22c, 0x80000030)
    write(0x23c, 0x00010000)
    write(0x240, 0x0409)
    write(0x244, 0x48)
    write(0x248, 0x2060)
    write(0x24c, 92)

    // VS_VERSION_INFO with file version 1.2.0.59 and product version 1.0.8.0.
    write(0x260, 0x0034005c)
    let key: [UInt8] = "VS_VERSION_INFO".utf8.flatMap { [$0, 0] }
    image.replaceSubrange(0x266..<(0x266 + key.count), with: key)
    write(0x288, 0xfeef04bd)
    write(0x28c, 0x00010000)
    write(0x290, 0x00010002)
    write(0x294, 59)
    write(0x298, 0x00010000)
    write(0x29c, 0x00080000)
    return image
  }

  private static func rejectChangedInstaller(_ patch: URL, input: GameInput,
                                             root: URL) async throws {
    let fm = FileManager.default
    let changed = root.appendingPathComponent("changed.exe")
    try fm.copyItem(at: patch, to: changed)
    let handle = try FileHandle(forWritingTo: changed)
    defer { try? handle.close() }
    try handle.write(contentsOf: Data([0]))
    let staged = root.appendingPathComponent("changed-staging")
    try fm.createDirectory(at: staged, withIntermediateDirectories: false)
    try await rejectPatch(changed, input: input, staging: staged,
                          message: "Choose the GTA IV Patch 8 installer")
    try require(!fm.fileExists(atPath: staged.appendingPathComponent("GTAIV.exe").path),
                "The Builder rejected the installer before extraction")
  }

  private static func rejectPatch(_ patch: URL?, input: GameInput,
                                  staging: URL, message: String) async throws {
    do {
      _ = try await input.executable(patch: patch, staging: staging)
    } catch {
      try require(error.localizedDescription.hasPrefix(message), message)
      return
    }
    throw Failure("The Builder accepted a missing or invalid patch.")
  }

  private static func require(_ condition: @autoclosure () throws -> Bool,
                              _ message: String) throws {
    guard try condition() else { throw Failure(message) }
  }
}

// Verify both radio inputs using the original archive and its extracted tree.

import Foundation

@main
struct RadioRestorationCheck {
  @MainActor
  static func main() async throws {
    try checkOptionalSelection()
    try checkPresets()
    print("PASS: build ready with radio disabled and all eight radio preset mappings")
    if CommandLine.arguments.count == 1 { return }
    guard CommandLine.arguments.count == 3 else {
      throw Failure("Pass data1.dat and the extracted RADIO_RESTORATION.rpf.")
    }
    let args = CommandLine.arguments.dropFirst().map { URL(fileURLWithPath: $0) }
    let catalog = URL(fileURLWithPath: "builder/RadioResources/Catalog.json")
    let fm = FileManager.default
    let root = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try fm.createDirectory(at: root, withIntermediateDirectories: false)
    defer { try? fm.removeItem(at: root) }

    for (index, source) in args.enumerated() {
      let mods = root.appendingPathComponent("mods-\(index)")
      let input = try RadioInput(source)
      try require(input.isUnpacked == (index == 1), "Recognized archive type")
      try await checkInstall(input, mods: mods, catalog: catalog)
    }
    let sharedMod = root.appendingPathComponent("shared-mod")
    try await checkSharedInstall(try RadioInput(args[1]), mods: sharedMod, catalog: catalog)
    try await rejectMissingCompanions(root, catalog: catalog)
    print("PASS: DAT and RPF installs, and safe failures")
  }

  @MainActor
  private static func checkOptionalSelection() throws {
    let url = URL(fileURLWithPath: "/check").appendingPathComponent(RadioInput.rpfPath)
    let engine = Engine(loadSavedGame: false)
    engine.game = .ok("GTA IV")
    engine.tools = .ok("Command Line Tools")
    try require(!engine.restoreRadio, "Radio starts disabled")
    try require(engine.canBuild, "Radio is optional")
    engine.restoreRadio = true
    try require(!engine.canBuild, "Radio needs a selection when enabled")
    engine.setRadio(url.deletingLastPathComponent().appendingPathComponent("unrelated.rpf"))
    try require(!engine.canBuild, "The Builder rejects an unrecognized RPF")
    engine.restoreRadio = false
    try require(engine.canBuild, "Radio errors do not block a build with radio disabled")
    engine.restoreRadio = true
    engine.setRadio(url)
    try require(engine.canBuild, "The selected RPF enables the build")
  }

  private static func checkPresets() throws {
    let variants = [(false, false, "opVANILLA.dat"), (true, false, "opCLASSIC.dat"),
                    (false, true, "opVANILLABETA.dat"), (true, true, "opALL.dat")]
    for (postCut, beta, base) in variants {
      var options = RadioOptions(postCut: postCut, beta: beta, split: false)
      try require(options.presets == [base], "Shared playlist order")
      options.split = true
      let split = beta ? "opSPLITBETA.dat" : "opSPLITVANILLA.dat"
      try require(options.presets == [base, "opSPLITbase.dat", split], "Split playlist order")
    }
  }

  private static func checkInstall(_ input: RadioInput, mods: URL, catalog: URL) async throws {
    let request = RadioRequest(input: input, options: RadioOptions())
    let prepared = try await PreparedRadio.prepare(request, in: mods, catalog: catalog)
    defer { prepared.discard() }
    let visibleName = "Radio Restoration"
    let fm = FileManager.default
    let items = try fm.contentsOfDirectory(atPath: mods.path)
    try require(!items.contains(visibleName), "Preparation keeps the incomplete mod hidden")
    let installed = try prepared.install()
    let sizes = [RadioInput.rpfPath: 676_575_232, "update/pc/audio/config/GAME.DAT16": 268_136,
                 "update/TBoGT/e2_radio.xml": 195, "update/TLAD/pc/audio/sfx/RR_EP1_SFX.rpf": 221_806_592]
    for (path, size) in sizes {
      try require(FileDigest.hasSize(installed.appendingPathComponent(path), size), "Installed \(path)")
    }
    let base = try stations(installed.appendingPathComponent(RadioStationList.basePath))
    let episodes = try stations(installed.appendingPathComponent("update/pc/audio/config/eflc_GAME.DAT16"))
    try require(episodes.count == 23 && RadioStationList.episodeStations.isSubset(of: episodes),
                "The episodes keep all their stations")
    try require(base == episodes.filter { !RadioStationList.episodeStations.contains($0) },
                "GTA IV keeps the original station order and leaves out the three EFLC stations")
    do {
      let second = try await PreparedRadio.prepare(request, in: mods, catalog: catalog)
      second.discard()
    } catch {
      try require(fm.fileExists(atPath: installed.path), "The existing mod stays in place")
      return
    }
    throw Failure("The Builder accepted a second install over an existing mod.")
  }

  private static func checkSharedInstall(_ input: RadioInput, mods: URL, catalog: URL) async throws {
    let options = RadioOptions(postCut: true, split: false)
    let request = RadioRequest(input: input, options: options)
    let prepared = try await PreparedRadio.prepare(request, in: mods, catalog: catalog)
    defer { prepared.discard() }
    let installed = try prepared.install()
    let file = installed.appendingPathComponent(RadioStationList.basePath)
    try require(FileDigest.hasSize(file, 268_320), "The mixed playlist uses opCLASSIC.dat")
    let base = try stations(file)
    try require(base.count == 23 && RadioStationList.episodeStations.isSubset(of: base),
                "The shared playlist keeps all stations")
  }

  private static func stations(_ file: URL) throws -> [UInt32] {
    try RadioStationList(Data(contentsOf: file)).stationIDs
  }

  private static func rejectMissingCompanions(_ root: URL, catalog: URL) async throws {
    let mods = root.appendingPathComponent("incomplete-mods")
    let source = root.appendingPathComponent(RadioInput.rpfPath)
    let fm = FileManager.default
    try fm.createDirectory(at: source.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data([0]).write(to: source)
    let request = RadioRequest(input: try RadioInput(source), options: RadioOptions())
    do {
      let staged = try await PreparedRadio.prepare(request, in: mods, catalog: catalog)
      staged.discard()
    } catch {
      try require(fm.contentsOfDirectory(atPath: mods.path).isEmpty, "Failed preparation cleans up")
      return
    }
    throw Failure("The Builder accepted an RPF without companion files.")
  }

  private static func require(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
    guard try condition() else { throw Failure(message) }
  }
}

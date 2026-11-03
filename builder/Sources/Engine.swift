// What the Builder does: checks the game and the developer tools, then
// translates the player's GTAIV.exe, compiles and links it with the kit and
// puts Liberty Pro Max.app in Applications. The radio import is apart.

import AppKit
import CryptoKit
import Foundation

enum Check: Equatable {
  case none, busy(String), ok(String), bad(String)
  var isBusy: Bool { if case .busy = self { return true } else { return false } }
}

struct Failure: LocalizedError {
  let errorDescription: String?
  init(_ s: String) { errorDescription = s }
}

@MainActor
final class Engine: ObservableObject {
  // The game.
  @Published var gameFolder: URL?
  @Published var game: Check = .none
  // The Command Line Tools (clang, the macOS SDK, objdump, python3).
  @Published var tools: Check = .none
  @Published var toolsOutdated = false  // installed but too old: the installer cannot help
  // The build.
  @Published var building = false
  @Published var step = ""
  @Published var progress: Double = 0  // 0...1; negative: a step without a count
  @Published var built: URL?
  @Published var failure: String?
  @Published var log: [String] = []
  // The radio.
  @Published var radio: Check = .none

  // BUILDER_WORK, BUILDER_DEST: another work folder and destination (tests).
  nonisolated static let work: URL = ProcessInfo.processInfo.environment["BUILDER_WORK"].map { URL(fileURLWithPath: $0) }
    ?? FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("Liberty Pro Max Builder")
  nonisolated static let destination: String = ProcessInfo.processInfo.environment["BUILDER_DEST"] ?? "/Applications"
  private var toolsTimer: Timer?
  private var toolsSince: Date?
  private var buildTask: Task<Void, Never>?

  var canBuild: Bool {
    if building { return false }
    if case .ok = game, case .ok = tools { return true }
    return false
  }

  init() {
    if let saved = UserDefaults.standard.string(forKey: "GameFolder") {
      Task { await self.setGame(URL(fileURLWithPath: saved)) }
    }
    Task { await checkTools() }
    // Quitting stops the compilers too.
    NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil,
                                           queue: .main) { [weak self] _ in
      MainActor.assumeIsolated { self?.buildTask?.cancel() }
    }
  }

  private var echo = false

  /// The build without the window: checks, builds, prints the log.
  static func headless(game: URL) async -> Int32 {
    let e = Engine()
    e.echo = true
    await e.setGame(game)
    await e.checkTools()
    guard e.canBuild else {
      print("not ready: game \(e.game), tools \(e.tools)")
      return 2
    }
    var last = ""
    e.build()
    while e.building {
      if e.step != last { last = e.step; print("step: \(last)") }
      try? await Task.sleep(nanoseconds: 2_000_000_000)
    }
    if let f = e.failure { print("FAILED: \(f)"); return 1 }
    print("installed: \(e.built?.path ?? "?")")
    return 0
  }

  /// The radio import without the window.
  static func headlessRadio(_ src: URL) async -> Int32 {
    do {
      try await runRadioImport(src)
      print("radio imported into \(radioDestination.path)")
      return 0
    } catch {
      print("FAILED: \(error.localizedDescription)")
      return 1
    }
  }

  /// Where the game reads the restored radio (BUILDER_DATA: another data
  /// folder, for tests).
  nonisolated static var radioDestination: URL {
    (ProcessInfo.processInfo.environment["BUILDER_DATA"].map { URL(fileURLWithPath: $0) }
      ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("gtaiv"))
      .appendingPathComponent("drive_c/radio-restored")
  }

  func say(_ line: String) {
    if echo { print(line) }
    log.append(line)
    if log.count > 400 { log.removeFirst(log.count - 400) }
  }

  // MARK: The game

  /// A dropped or chosen folder, or GTAIV.exe itself.
  func setGame(_ url: URL) async {
    var folder = url
    var isDir: ObjCBool = false
    if FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir), !isDir.boolValue {
      folder = url.deletingLastPathComponent()
    }
    let exe = folder.appendingPathComponent("GTAIV.exe")
    guard FileManager.default.fileExists(atPath: exe.path) else {
      game = .bad("No GTAIV.exe in this folder. Drop the folder Grand Theft Auto IV is installed in.")
      return
    }
    for sub in ["pc", "common"] where !FileManager.default.fileExists(atPath: folder.appendingPathComponent(sub).path) {
      game = .bad("The game's \(sub) folder is missing next to GTAIV.exe: drop the whole installed game.")
      return
    }
    gameFolder = folder
    game = .busy("Checking GTAIV.exe…")
    let hash = await Task.detached { Engine.sha256(exe) }.value
    guard gameFolder == folder else { return }  // another one was dropped meanwhile
    if hash == Kit.exeSHA256 {
      game = .ok("GTAIV.exe 1.0.8.0: \(folder.path)")
      UserDefaults.standard.set(folder.path, forKey: "GameFolder")
    } else {
      game = .bad("This GTAIV.exe is not version 1.0.8.0, the one Liberty Pro Max supports. "
                  + "The Steam Complete Edition becomes 1.0.8.0 with the community's GTA Downgraders.")
    }
  }

  nonisolated static func sha256(_ url: URL) -> String {
    guard let h = try? FileHandle(forReadingFrom: url) else { return "" }
    defer { try? h.close() }
    var hasher = SHA256()
    while let chunk = try? h.read(upToCount: 1 << 22), !chunk.isEmpty { hasher.update(data: chunk) }
    return hasher.finalize().map { String(format: "%02x", $0) }.joined()
  }

  // MARK: The developer tools

  func checkTools() async {
    // Nothing that could open Apple's installer on its own until the tools
    // are there: xcrun would, xcode-select -p does not.
    guard await Run.line("/usr/bin/xcode-select", ["-p"]) != nil,
          await Run.line("/usr/bin/xcrun", ["--find", "clang++"]) != nil,
          let sdk = await Run.line("/usr/bin/xcrun", ["--show-sdk-path"]) else {
      if tools.isBusy, let since = toolsSince, Date().timeIntervalSince(since) < 1800 { return }
      stopWaitingForTools()
      tools = .bad("Apple's Command Line Tools are needed to build the game (free, about 1 GB).")
      return
    }
    stopWaitingForTools()
    // The recipe's sources need at least this compiler.
    let want = (try? Recipe.load().clang) ?? 0
    let version = await Run.line("/usr/bin/xcrun", ["clang++", "--version"]) ?? ""
    let have = Int(version.range(of: #"clang version (\d+)"#, options: .regularExpression)
      .map { version[$0].split(separator: " ").last.map(String.init) ?? "" } ?? "") ?? 0
    toolsOutdated = want > 0 && have > 0 && have < want
    if toolsOutdated {
      tools = .bad("These Command Line Tools are too old (clang \(have); \(want) or newer is needed). "
                   + "Update them in System Settings › General › Software Update (macOS 15 or newer has them).")
      return
    }
    tools = .ok("Command Line Tools ready (\(URL(fileURLWithPath: sdk).lastPathComponent))")
  }

  private func stopWaitingForTools() {
    toolsTimer?.invalidate()
    toolsTimer = nil
    toolsSince = nil
  }

  /// Opens Apple's installer of the Command Line Tools and waits for it
  /// (the button stays, in case its window was closed).
  func installTools() {
    tools = .busy("Installing Apple's Command Line Tools: follow the window that opened…")
    toolsSince = Date()
    Task { _ = await Run.run("/usr/bin/xcode-select", ["--install"]) }
    toolsTimer?.invalidate()
    toolsTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
      Task { @MainActor in await self?.checkTools() }
    }
  }

  // MARK: The build

  func build() {
    guard canBuild, let folder = gameFolder else { return }
    building = true
    failure = nil
    built = nil
    log.removeAll()
    buildTask = Task {
      do {
        let app = try await runBuild(game: folder)
        built = app
        step = "Done"
        progress = 1
      } catch is CancellationError {
        step = "Stopped"
      } catch {
        failure = error.localizedDescription
        step = "Stopped"
      }
      building = false
    }
  }

  func again() { built = nil }

  private func runBuild(game: URL) async throws -> URL {
    let work = Engine.work
    let gen = work.appendingPathComponent("generated")
    try FileManager.default.createDirectory(at: gen.appendingPathComponent("native"), withIntermediateDirectories: true)
    try FileManager.default.createDirectory(at: work.appendingPathComponent("obj"), withIntermediateDirectories: true)

    // Room: the translated code, its objects and the app take about 3 GB.
    if let free = try? work.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
      .volumeAvailableCapacityForImportantUsage, free < 4_000_000_000 {
      throw Failure("About 4 GB of free disk space is needed to build; \(free / 1_000_000_000) GB is free.")
    }
    guard let sdk = await Run.line("/usr/bin/xcrun", ["--show-sdk-path"]),
          let objdump = await Run.line("/usr/bin/xcrun", ["--find", "objdump"]) else {
      throw Failure("Apple's Command Line Tools are not ready.")
    }

    // 1. The game's code, translated from its x86 to C++.
    step = "Translating the game's code"
    progress = -1
    say("== translating GTAIV.exe")
    let lift = await Run.run(Kit.path("bin/recomp_lift"),
                             ["--exe", "GTAIV.exe", "--symbols", Kit.path("symbols/gtaiv"),
                              "--manifest", Kit.path("manifest/gtaiv.manifest"), "--out", gen.path],
                             cwd: game.path)
    lift.output.split(separator: "\n").suffix(20).forEach { say(String($0)) }
    try Task.checkCancellation()
    guard lift.ok else { throw Failure("Translating the game's code failed:\n" + lift.output.suffix(2000)) }

    // 2. Its hottest functions, as plain C++.
    let natives = try String(contentsOfFile: Kit.path("manifest/native.list"), encoding: .utf8)
      .split(separator: "\n").map { $0.split(separator: " ").map(String.init) }.filter { $0.count >= 3 }
    say("== \(natives.count) hot functions")
    let exe = game.appendingPathComponent("GTAIV.exe").path
    try await parallel(natives, label: "Translating the hottest functions") { n in
      let end = String(format: "%08X", (UInt32(n[1], radix: 16) ?? 0) + 1)
      let r = await Run.run("/usr/bin/python3", [Kit.path("tools/x86n.py"), n[0], end, n[2]],
                            env: ["GTAIV_EXE": exe, "OBJDUMP": objdump],
                            stdoutFile: gen.appendingPathComponent("native/\(n[0]).inc").path)
      return r.ok ? nil : "\(n[2]): \(r.output.suffix(800))"
    }

    // 3. Compiled with the Mac's own compiler.
    let recipe = try Recipe.load()
    say("== compiling \(recipe.compile.count) files")
    try await parallel(recipe.compile, label: "Compiling") { u in
      let out = u.out.expanded(work: work.path, sdk: sdk)
      try? FileManager.default.createDirectory(atPath: (out as NSString).deletingLastPathComponent,
                                               withIntermediateDirectories: true)
      let r = await Run.run("/usr/bin/xcrun", ["clang++"] + u.args.map { $0.expanded(work: work.path, sdk: sdk) })
      return r.ok ? nil : "\(u.src):\n\(r.output.suffix(1500))"
    }

    // 4. Linked against the kit's runtime.
    step = "Linking"
    progress = -1
    say("== linking")
    let link = await Run.run("/usr/bin/xcrun", ["clang++"] + recipe.link.map { $0.expanded(work: work.path, sdk: sdk) })
    try Task.checkCancellation()
    guard link.ok else { throw Failure("Linking failed:\n" + link.output.suffix(2000)) }

    // 5. The app: the kit's bundle with this executable, signed for this Mac
    // and without the quarantine a downloaded Builder passes on to its files.
    step = "Putting the app together"
    let staged = work.appendingPathComponent(Kit.appName)
    try await Task.detached {
      let fm = FileManager.default
      try? fm.removeItem(at: staged)
      try fm.copyItem(at: Kit.root.appendingPathComponent("app/\(Kit.appName)"), to: staged)
      let exe = staged.appendingPathComponent("Contents/MacOS/gtaiv")
      try? fm.removeItem(at: exe)
      try fm.copyItem(at: work.appendingPathComponent("gtaiv"), to: exe)
    }.value
    _ = await Run.run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", staged.path])
    let fw = staged.appendingPathComponent("Contents/Frameworks")
    for d in (try? FileManager.default.contentsOfDirectory(atPath: fw.path)) ?? [] where d.hasSuffix(".dylib") {
      _ = await Run.run("/usr/bin/codesign", ["--force", "--sign", "-", fw.appendingPathComponent(d).path])
    }
    let sign = await Run.run("/usr/bin/codesign", ["--force", "--sign", "-", staged.path])
    guard sign.ok else { throw Failure("Signing the app failed:\n" + sign.output) }

    // 6. Into Applications (the user's own when the shared one is closed),
    // then the work folder is emptied (about 3 GB).
    let dest = try await Task.detached { () -> URL in
      let fm = FileManager.default
      var dest = URL(fileURLWithPath: Engine.destination).appendingPathComponent(Kit.appName)
      if !fm.isWritableFile(atPath: Engine.destination) {
        let mine = fm.homeDirectoryForCurrentUser.appendingPathComponent("Applications")
        try fm.createDirectory(at: mine, withIntermediateDirectories: true)
        dest = mine.appendingPathComponent(Kit.appName)
      }
      if fm.fileExists(atPath: dest.path) { try fm.removeItem(at: dest) }
      try fm.copyItem(at: staged, to: dest)
      try? fm.removeItem(at: work)
      return dest
    }.value
    say("== installed: \(dest.path)")

    // The game folder, so the launcher does not ask for it.
    CFPreferencesSetAppValue("GameFolder" as CFString, game.path as CFString, Kit.gameDomain)
    CFPreferencesAppSynchronize(Kit.gameDomain)
    return dest
  }

  /// Runs `body` over `items`, as many at once as the Mac has cores; a
  /// non-nil result is a failure, which stops the rest (their processes too).
  private func parallel<T: Sendable>(_ items: [T], label: String,
                                     _ body: @escaping @Sendable (T) async -> String?) async throws {
    let width = max(2, ProcessInfo.processInfo.activeProcessorCount)
    var done = 0
    var failed: String?
    step = label
    progress = 0
    await withTaskGroup(of: String?.self) { group in
      var next = 0
      func add() {
        guard next < items.count else { return }
        let item = items[next]
        next += 1
        group.addTask { await body(item) }
      }
      for _ in 0..<min(width, items.count) { add() }
      for await result in group {
        done += 1
        progress = Double(done) / Double(max(items.count, 1))
        step = "\(label) (\(done) of \(items.count))"
        if let result, failed == nil {
          failed = result
          group.cancelAll()
        }
        if failed == nil, !Task.isCancelled { add() }
      }
    }
    try Task.checkCancellation()
    if let failed { throw Failure("\(label) failed:\n\(failed)") }
  }

  // MARK: The radio

  /// The Radio Restoration mod, as downloaded (.rar or .7z) or unpacked:
  /// its 2008 songs into the port's data, where the game reads them with
  /// the launcher's "Original 2008 radio songs" on.
  func importRadio(_ url: URL) {
    guard !radio.isBusy else { return }
    radio = .busy("Unpacking the radio songs (about 1 GB, a few minutes)…")
    Task {
      do {
        try await Engine.runRadioImport(url)
        CFPreferencesSetAppValue("OriginalRadio" as CFString, kCFBooleanTrue, Kit.gameDomain)
        CFPreferencesAppSynchronize(Kit.gameDomain)
        radio = .ok("The original 2008 radio songs are in. The launcher's option is on.")
      } catch {
        radio = .bad(error.localizedDescription)
      }
    }
  }

  nonisolated private static func runRadioImport(_ src: URL) async throws {
    let fm = FileManager.default
    let sevenZ = Kit.path("bin/7zz")
    let tmp = work.appendingPathComponent("radio")
    try? fm.removeItem(at: tmp)
    try fm.createDirectory(at: tmp, withIntermediateDirectories: true)
    defer { try? fm.removeItem(at: tmp) }

    // The mod's two archives: data1.dat (the songs) and opVANILLA.dat (the
    // 2008 playlist), from the download or from its unpacked folder.
    var data1: URL?, playlist: URL?
    var isDir: ObjCBool = false
    _ = fm.fileExists(atPath: src.path, isDirectory: &isDir)
    if isDir.boolValue {
      let e = fm.enumerator(at: src, includingPropertiesForKeys: nil)
      while let f = e?.nextObject() as? URL {
        if f.lastPathComponent == "data1.dat" { data1 = f }
        if f.lastPathComponent == "opVANILLA.dat" { playlist = f }
      }
    } else {
      let r = await Run.run(sevenZ, ["e", "-y", "-o" + tmp.path, src.path, "data1.dat", "opVANILLA.dat", "-r"])
      guard r.ok else { throw Failure("This file could not be opened as the Radio Restoration mod:\n" + r.output.suffix(600)) }
      data1 = tmp.appendingPathComponent("data1.dat")
      playlist = tmp.appendingPathComponent("opVANILLA.dat")
    }
    guard let data1, let playlist, fm.fileExists(atPath: data1.path), fm.fileExists(atPath: playlist.path) else {
      throw Failure("This is not the Radio Restoration mod: its \"Radio Restorer\" files (data1.dat, opVANILLA.dat) are missing.")
    }

    // Both unpacked; their update/ folders, base first, into the data.
    let base = tmp.appendingPathComponent("base"), vanilla = tmp.appendingPathComponent("vanilla")
    for (archive, out) in [(data1, base), (playlist, vanilla)] {
      let r = await Run.run(sevenZ, ["x", "-y", "-o" + out.path, archive.path])
      guard r.ok else { throw Failure("Unpacking \(archive.lastPathComponent) failed:\n" + r.output.suffix(600)) }
    }
    let dest = radioDestination
    try? fm.removeItem(at: dest)
    try fm.createDirectory(at: dest, withIntermediateDirectories: true)
    for layer in [base, vanilla] {
      let r = await Run.run("/usr/bin/rsync", ["-a", layer.appendingPathComponent("update").path + "/", dest.path + "/"])
      guard r.ok else { throw Failure("Copying the radio songs failed:\n" + r.output.suffix(600)) }
    }
  }
}

// What the Builder does: checks the game and the developer tools, then
// translates the player's GTAIV.exe, compiles and links it with the kit and
// puts Liberty Pro Max.app in Applications.

import AppKit
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
  private var gameInput: GameInput?
  @Published private(set) var needsPatch = false
  @Published private(set) var patch: URL?
  @Published var game: Check = .none
  private var selection = UUID()
  @Published var restoreRadio = false
  @Published var radioOptions = RadioOptions()
  @Published private(set) var radioInput: RadioInput?
  @Published private(set) var radioCheck: Check = .none
  @Published private(set) var radioInstalled = false
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

  // BUILDER_WORK, BUILDER_DEST: another work folder and destination (tests).
  nonisolated static let work: URL = ProcessInfo.processInfo.environment["BUILDER_WORK"].map { URL(fileURLWithPath: $0) }
    ?? FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("Liberty Pro Max Builder")
  nonisolated static let destination: String = ProcessInfo.processInfo.environment["BUILDER_DEST"] ?? "/Applications"
  private var toolsTimer: Timer?
  private var toolsSince: Date?
  private var buildTask: Task<Void, Never>?

  var canBuild: Bool {
    if building || (needsPatch && (patch == nil || GameInput.extractor == nil))
      || (restoreRadio && radioInput == nil) {
      return false
    }
    if case .ok = game, case .ok = tools { return true }
    return false
  }

  init(loadSavedGame: Bool = true) {
    if loadSavedGame, let saved = UserDefaults.standard.string(forKey: "GameFolder") {
      Task { await self.setGame(URL(fileURLWithPath: saved)) }
    }
    if loadSavedGame { Task { await checkTools() } }
    // Quitting stops the compilers too.
    NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil,
                                           queue: .main) { [weak self] _ in
      MainActor.assumeIsolated { self?.buildTask?.cancel() }
    }
  }

  private var echo = false

  /// The build without the window: checks, builds, prints the log.
  static func headless(game: URL, patch: URL?, radio: URL?, options: RadioOptions) async -> Int32 {
    let e = Engine(loadSavedGame: false)
    e.echo = true
    await e.setGame(game)
    if let patch { e.setPatch(patch) }
    if let radio {
      e.restoreRadio = true
      e.radioOptions = options
      e.setRadio(radio)
    }
    await e.checkTools()
    guard e.canBuild else {
      if e.needsPatch && e.patch == nil { print("Supply --patch PATCH_8_INSTALLER.") }
      if e.needsPatch && GameInput.extractor == nil { print("Run brew install sevenzip.") }
      if case .bad(let message) = e.radioCheck { print(message) }
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

  func say(_ line: String) {
    if echo { print(line) }
    log.append(line)
    if log.count > 400 { log.removeFirst(log.count - 400) }
  }

  // MARK: The game

  /// A dropped or chosen folder, or GTAIV.exe itself.
  func setGame(_ url: URL) async {
    guard !building else { return }
    let current = UUID()
    selection = current
    gameInput = nil
    needsPatch = false
    failure = nil
    game = .busy("Checking GTA IV…")
    let result = await Task.detached { Result { try GameInput.inspect(url) } }.value
    guard selection == current else { return }
    switch result {
    case .success(let input):
      gameInput = input
      needsPatch = input.needsPatch
      let version = input.needsPatch ? "Complete Edition" : "GTAIV.exe 1.0.8.0"
      game = .ok("\(version) at \(input.folder.path)")
      if !echo { UserDefaults.standard.set(input.folder.path, forKey: "GameFolder") }
    case .failure(let error):
      game = .bad(error.localizedDescription)
    }
  }

  func setPatch(_ url: URL) {
    guard !building else { return }
    patch = url
    failure = nil
  }

  func setRadio(_ url: URL) {
    guard !building else { return }
    failure = nil
    do {
      radioInput = try RadioInput(url)
      radioCheck = .none
    } catch {
      radioInput = nil
      radioCheck = .bad(error.localizedDescription)
    }
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
    guard canBuild, let input = gameInput else { return }
    let installer = patch
    let request = restoreRadio ? radioInput.map { RadioRequest(input: $0, options: radioOptions) } : nil
    building = true
    failure = nil
    built = nil
    radioInstalled = false
    log.removeAll()
    buildTask = Task {
      do {
        let app = try await runBuild(input: input, patch: installer, radio: request)
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

  private func runBuild(input: GameInput, patch: URL?, radio: RadioRequest?) async throws -> URL {
    let work = Engine.work
    let gen = work.appendingPathComponent("generated")
    try FileManager.default.createDirectory(at: gen.appendingPathComponent("native"), withIntermediateDirectories: true)
    try FileManager.default.createDirectory(at: work.appendingPathComponent("obj"), withIntermediateDirectories: true)

    // Room: the translated code, its objects and the app take about 3 GB.
    let gbNeeded: Int64 = radio == nil ? 4 : 7
    if let free = try? work.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
      .volumeAvailableCapacityForImportantUsage, free < gbNeeded * 1_000_000_000 {
      throw Failure("The build needs about \(gbNeeded) GB. \(free / 1_000_000_000) GB is free.")
    }
    guard let sdk = await Run.line("/usr/bin/xcrun", ["--show-sdk-path"]),
          let objdump = await Run.line("/usr/bin/xcrun", ["--find", "objdump"]) else {
      throw Failure("Apple's Command Line Tools are not ready.")
    }

    let fm = FileManager.default
    let staging = work.appendingPathComponent(UUID().uuidString, isDirectory: true)
    try fm.createDirectory(at: staging, withIntermediateDirectories: false,
                           attributes: [.posixPermissions: 0o700])
    defer { try? fm.removeItem(at: staging) }
    if input.needsPatch {
      step = "Extracting GTAIV.exe from Patch 8"
      progress = -1
    }
    let executable = try await input.executable(patch: patch, staging: staging)
    try Task.checkCancellation()
    let game = input.folder

    step = "Preparing FusionFix"
    progress = -1
    let fusion = staging.appendingPathComponent("fusionfix")
    let downloads = work.deletingLastPathComponent().appendingPathComponent("Liberty Pro Max Downloads")
    let dependencies = await Run.run("/usr/bin/python3",
      [Kit.path("tools/fusionfix.py"), game.path, fusion.path, "--cache", downloads.path])
    try Task.checkCancellation()
    guard dependencies.ok else { throw Failure("Preparing FusionFix failed:\n" + dependencies.output.suffix(2000)) }
    say("== FusionFix data and in-game menus prepared")

    let restored = try await prepareRadio(radio)
    defer { restored?.discard() }
    try Task.checkCancellation()

    // 1. The game's code, translated from its x86 to C++.
    step = "Translating the game's code"
    progress = -1
    say("== translating GTAIV.exe")
    let lift = await Run.run(Kit.path("bin/recomp_lift"),
                             ["--exe", executable.path, "--symbols", Kit.path("symbols/gtaiv"),
                              "--manifest", Kit.path("manifest/gtaiv.manifest"), "--out", gen.path],
                             cwd: game.path)
    lift.output.split(separator: "\n").suffix(20).forEach { say(String($0)) }
    try Task.checkCancellation()
    guard lift.ok else { throw Failure("Translating the game's code failed:\n" + lift.output.suffix(2000)) }

    // 2. Its hottest functions, as plain C++.
    let natives = try String(contentsOfFile: Kit.path("manifest/native.list"), encoding: .utf8)
      .split(separator: "\n").map { $0.split(separator: " ").map(String.init) }.filter { $0.count >= 3 }
    say("== \(natives.count) hot functions")
    let exe = executable.path
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
    try fm.moveItem(at: fusion, to: staged.appendingPathComponent("Contents/Resources/fusionfix"))
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
    if let restored, let radio {
      do {
        let destination = try restored.install()
        let own = radio.options.split ? kCFBooleanTrue : kCFBooleanFalse
        CFPreferencesSetAppValue("EpisodeRadioOwn" as CFString, own, Kit.gameDomain)
        radioInstalled = true
        say("== radio restored: \(destination.path)")
      } catch {
        failure = "The game was installed. Radio Restoration failed. " + error.localizedDescription
      }
    }
    CFPreferencesAppSynchronize(Kit.gameDomain)
    return dest
  }

  private func prepareRadio(_ radio: RadioRequest?) async throws -> PreparedRadio? {
    guard let radio else { return nil }
    step = "Preparing Radio Restoration"
    progress = -1
    say("== verifying Radio Restoration files")
    return try await PreparedRadio.prepare(
      radio, in: PreparedRadio.modsFolder, catalog: Kit.root.appendingPathComponent("radio/Catalog.json"))
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
}

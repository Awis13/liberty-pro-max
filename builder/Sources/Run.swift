// Running the tools: one process, its output collected (or sent to a
// file), awaited without blocking a thread; cancelling the task stops it.

import Foundation

struct RunResult {
  let status: Int32
  let output: String
  var ok: Bool { status == 0 }
}

enum Run {
  /// Runs `exe` with `args`: its stdout and stderr are returned (stdout
  /// first), or stdout goes to `stdoutFile` when given.
  static func run(_ exe: String, _ args: [String], cwd: String? = nil, env: [String: String] = [:],
                  stdoutFile: String? = nil) async -> RunResult {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: exe)
    p.arguments = args
    if let cwd { p.currentDirectoryURL = URL(fileURLWithPath: cwd) }
    var e = ProcessInfo.processInfo.environment
    e["PYTHONDONTWRITEBYTECODE"] = "1"  // nothing written into the signed bundle
    for (k, v) in env { e[k] = v }
    p.environment = e

    let done = DispatchGroup()
    let err = Drain(done)
    p.standardError = err.pipe
    var out: Drain?
    var file: FileHandle?
    if let stdoutFile {
      FileManager.default.createFile(atPath: stdoutFile, contents: nil)
      file = FileHandle(forWritingAtPath: stdoutFile)
      p.standardOutput = file
    } else {
      out = Drain(done)
      p.standardOutput = out!.pipe
    }
    done.enter()
    p.terminationHandler = { _ in done.leave() }

    let status: Int32 = await withTaskCancellationHandler {
      await withCheckedContinuation { cont in
        var started = true
        do {
          try p.run()
        } catch {
          started = false
          err.note("cannot start \(exe): \(error.localizedDescription)")
          p.terminationHandler = nil
          done.leave()
          err.close()
          out?.close()
        }
        let ran = started
        done.notify(queue: .global()) { cont.resume(returning: ran ? p.terminationStatus : -1) }
      }
    } onCancel: {
      if p.isRunning { p.terminate() }
    }
    try? file?.close()
    return RunResult(status: status, output: (out?.text ?? "") + err.text)
  }

  /// The first line a tool prints on stdout, or nil when it fails.
  static func line(_ exe: String, _ args: [String]) async -> String? {
    let r = await run(exe, args)
    guard r.ok else { return nil }
    return r.output.split(separator: "\n").first.map(String.init)
  }
}

/// One pipe read to its end; the group is left at EOF.
final class Drain: @unchecked Sendable {
  let pipe = Pipe()
  private let lock = NSLock()
  private var data = Data()
  private var closed = false
  private let group: DispatchGroup

  init(_ group: DispatchGroup) {
    self.group = group
    group.enter()
    pipe.fileHandleForReading.readabilityHandler = { [weak self] h in
      let d = h.availableData
      if d.isEmpty { self?.close() } else { self?.append(d) }
    }
  }

  private func append(_ d: Data) {
    lock.lock()
    data.append(d)
    lock.unlock()
  }

  func note(_ s: String) { append(Data((s + "\n").utf8)) }

  /// At EOF (or when the process never started): stop reading, once.
  func close() {
    lock.lock()
    let first = !closed
    closed = true
    lock.unlock()
    guard first else { return }
    pipe.fileHandleForReading.readabilityHandler = nil
    group.leave()
  }

  var text: String {
    lock.lock()
    defer { lock.unlock() }
    return String(decoding: data, as: UTF8.self)
  }
}

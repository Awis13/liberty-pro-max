// Liberty Pro Max Builder: drop your GTA IV in, get the Mac app out.

import SwiftUI
import UniformTypeIdentifiers

@main
enum Main {
  static func main() {
    // Liberty Pro Max Builder --build GAME_FOLDER: the same build without the
    // window, its log on stdout (for testing the kit); exit status 0 when
    // the app was installed.
    let args = CommandLine.arguments
    setvbuf(stdout, nil, _IOLBF, 0)  // the windowless modes' log, line by line
    if let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count {  // the window as a PNG (layout checks)
      Task { @MainActor in
        let r = ImageRenderer(content: ContentView().environmentObject(Engine()).frame(width: 620)
          .background(Color(nsColor: .windowBackgroundColor)))
        r.scale = 2
        if let img = r.nsImage, let tiff = img.tiffRepresentation,
           let png = NSBitmapImageRep(data: tiff)?.representation(using: .png, properties: [:]) {
          try? png.write(to: URL(fileURLWithPath: args[i + 1]))
        }
        exit(0)
      }
      dispatchMain()
    }
    if let i = args.firstIndex(of: "--radio"), i + 1 < args.count {
      Task { @MainActor in exit(await Engine.headlessRadio(URL(fileURLWithPath: args[i + 1]))) }
      dispatchMain()
    }
    if let i = args.firstIndex(of: "--build"), i + 1 < args.count {
      Task { @MainActor in exit(await Engine.headless(game: URL(fileURLWithPath: args[i + 1]))) }
      dispatchMain()
    }
    BuilderApp.main()
  }
}

struct BuilderApp: App {
  @StateObject private var engine = Engine()

  var body: some Scene {
    Window("Liberty Pro Max Builder", id: "main") {
      ContentView()
        .environmentObject(engine)
        .frame(width: 620)
        .fixedSize(horizontal: false, vertical: true)
    }
    .windowResizability(.contentSize)
  }
}

struct ContentView: View {
  @EnvironmentObject var engine: Engine
  @State private var showLog = false

  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      VStack(alignment: .leading, spacing: 4) {
        Text("Liberty Pro Max").font(.system(size: 26, weight: .bold))
        Text("Builds Grand Theft Auto IV for your Mac from your own copy of the game.")
          .foregroundStyle(.secondary)
      }

      Card(number: 1, title: "Your game", state: engine.game) {
        DropZone(text: "Drop the Grand Theft Auto IV folder (or GTAIV.exe) here",
                 symbol: "folder.badge.plus") { url in
          Task { await engine.setGame(url) }
        }
      }

      Card(number: 2, title: "Apple's developer tools", state: engine.tools) {
        switch engine.tools {
        case .bad where !engine.toolsOutdated: Button("Install the Command Line Tools") { engine.installTools() }
        case .busy: Button("Open the installer again") { engine.installTools() }.controlSize(.small)
        default: EmptyView()
        }
      }

      Card(number: 3, title: "Original 2008 radio songs (optional)", state: engine.radio) {
        VStack(alignment: .leading, spacing: 6) {
          DropZone(text: "Drop the Radio Restoration mod here (the .rar as downloaded, or its folder)",
                   symbol: "radio") { url in engine.importRadio(url) }
            .disabled(engine.radio.isBusy)
          Link("Get the mod: GTA Downgraders by Tomasak / Fusion Team",
               destination: URL(string: "https://github.com/Tomasak/GTA-Downgraders/releases")!)
            .font(.caption)
        }
      }

      Divider()
      buildSection
    }
    .padding(26)
  }

  @ViewBuilder private var buildSection: some View {
    if let app = engine.built {
      HStack(spacing: 10) {
        Image(systemName: "checkmark.seal.fill").foregroundStyle(.green).font(.title2)
        VStack(alignment: .leading) {
          Text("Liberty Pro Max is ready").font(.headline)
          Text(app.deletingLastPathComponent().path).font(.caption).foregroundStyle(.secondary)
        }
        Spacer()
        Button("Build again") { engine.again() }
        Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([app]) }
        Button("Play") { NSWorkspace.shared.open(app) }.keyboardShortcut(.defaultAction)
      }
    } else if engine.building {
      VStack(alignment: .leading, spacing: 8) {
        Text(engine.step).font(.headline)
        if engine.progress >= 0 {
          ProgressView(value: engine.progress)
        } else {
          ProgressView().progressViewStyle(.linear)
        }
        Text("This takes 10 to 20 minutes. The Mac may get warm and loud.")
          .font(.caption).foregroundStyle(.secondary)
      }
    } else {
      HStack {
        if let failure = engine.failure {
          Label(failure.split(separator: "\n").first.map(String.init) ?? failure,
                systemImage: "exclamationmark.triangle.fill")
            .foregroundStyle(.red).lineLimit(2)
        } else {
          Text("The finished app goes to Applications.").foregroundStyle(.secondary)
        }
        Spacer()
        Button("Build Liberty Pro Max") { engine.build() }
          .keyboardShortcut(.defaultAction)
          .controlSize(.large)
          .disabled(!engine.canBuild)
      }
    }

    if !engine.log.isEmpty || engine.failure != nil {
      DisclosureGroup("Details", isExpanded: $showLog) {
        ScrollView {
          Text((engine.failure.map { [$0] } ?? []).joined() + "\n" + engine.log.suffix(200).joined(separator: "\n"))
            .font(.system(size: 11, design: .monospaced))
            .frame(maxWidth: .infinity, alignment: .leading)
            .textSelection(.enabled)
        }
        .frame(height: 160)
      }
      .font(.caption)
    }
  }
}

/// A numbered step with its state and its controls.
struct Card<Content: View>: View {
  let number: Int
  let title: String
  let state: Check
  @ViewBuilder let content: Content

  var body: some View {
    HStack(alignment: .top, spacing: 14) {
      ZStack {
        Circle().fill(color.opacity(0.18)).frame(width: 30, height: 30)
        if case .ok = state {
          Image(systemName: "checkmark").font(.system(size: 13, weight: .bold)).foregroundStyle(color)
        } else {
          Text("\(number)").font(.system(size: 14, weight: .semibold)).foregroundStyle(color)
        }
      }
      VStack(alignment: .leading, spacing: 8) {
        Text(title).font(.headline)
        switch state {
        case .none: EmptyView()
        case .busy(let s): HStack(spacing: 6) { ProgressView().controlSize(.small); Text(s).font(.callout) }
        case .ok(let s): Text(s).font(.callout).foregroundStyle(.secondary).lineLimit(2).truncationMode(.middle)
        case .bad(let s): Text(s).font(.callout).foregroundStyle(.orange)
        }
        content
      }
      Spacer(minLength: 0)
    }
  }

  private var color: Color {
    switch state {
    case .ok: return .green
    case .bad: return .orange
    default: return .accentColor
    }
  }
}

/// Takes a dropped file or folder, or one chosen with the button.
struct DropZone: View {
  let text: String
  let symbol: String
  let onURL: (URL) -> Void
  @State private var targeted = false

  var body: some View {
    HStack(spacing: 10) {
      Image(systemName: symbol).font(.title3).foregroundStyle(.secondary)
      Text(text).font(.callout).foregroundStyle(.secondary)
      Spacer()
      Button("Choose…") { choose() }
    }
    .padding(12)
    .background(RoundedRectangle(cornerRadius: 10)
      .strokeBorder(style: StrokeStyle(lineWidth: 1.5, dash: [6, 4]))
      .foregroundStyle(targeted ? Color.accentColor : Color.secondary.opacity(0.5)))
    .onDrop(of: [.fileURL], isTargeted: $targeted) { providers in
      guard let p = providers.first else { return false }
      _ = p.loadObject(ofClass: URL.self) { url, _ in
        if let url { DispatchQueue.main.async { onURL(url) } }
      }
      return true
    }
  }

  private func choose() {
    let panel = NSOpenPanel()
    panel.canChooseDirectories = true
    panel.canChooseFiles = true
    panel.allowsMultipleSelection = false
    if panel.runModal() == .OK, let url = panel.url { onURL(url) }
  }
}

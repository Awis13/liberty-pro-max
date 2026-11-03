// The kit inside the Builder (Contents/Resources/kit), made by
// builder/make_builder.sh:
//   bin/recomp_lift                   the lifter
//   tools/x86n.py                     the hot-function translator
//   symbols/, manifest/               what the lifter reads
//   src/, include/                    the port's and the runtime's sources
//   app/Liberty Pro Max.app           the app without its executable
//   recipe.json                       how to compile and link the game
// Everything that is not the game's code comes built; only the code
// translated from the player's own GTAIV.exe is compiled on their Mac.

import Foundation

enum Kit {
  static var root: URL { Bundle.main.resourceURL!.appendingPathComponent("kit") }
  static func path(_ rel: String) -> String { root.appendingPathComponent(rel).path }

  /// SHA-256 of GTAIV.exe 1.0.8.0, the one version the port supports.
  static let exeSHA256 = "3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f"
  /// The game's settings domain (its launcher's NSUserDefaults).
  static let gameDomain = "io.github.awis13.liberty-pro-max" as CFString
  static let appName = "Liberty Pro Max.app"
}

/// The compile and link steps, captured from the reference build with
/// placeholders: @KIT@ (the kit), @WORK@ (the work folder), @SDK@ (the
/// macOS SDK of the player's Command Line Tools).
struct Recipe: Decodable {
  struct Unit: Decodable {
    let src: String
    let out: String
    let args: [String]
  }
  let clang: Int  // the reference build's clang major version
  let compile: [Unit]
  let link: [String]

  static func load() throws -> Recipe {
    let data = try Data(contentsOf: Kit.root.appendingPathComponent("recipe.json"))
    return try JSONDecoder().decode(Recipe.self, from: data)
  }
}

extension String {
  func expanded(work: String, sdk: String) -> String {
    replacingOccurrences(of: "@KIT@", with: Kit.root.path)
      .replacingOccurrences(of: "@WORK@", with: work)
      .replacingOccurrences(of: "@SDK@", with: sdk)
  }
}

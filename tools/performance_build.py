#!/usr/bin/env python3
"""Build pinned comparison inputs without changing the active checkout/vendor.

Uses local dependency sources and already generated game code. Full shader
preparation is a separate, explicit command, never a side effect of this tool.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
from performance_camera import fixed_camera


def output(*command):
    return subprocess.check_output(command)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--main", default="main")
    p.add_argument("--feature", default="HEAD")
    p.add_argument("--only-feature", action="store_true", help="Build only the feature ref for comparison with existing frozen controls")
    p.add_argument("--renderer-source", type=Path, help="Local dependency repository containing the requested pins")
    p.add_argument("--jobs", type=int, default=4)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    refs = {name: output("git", "-C", str(root), "rev-parse", ref).decode().strip()
            for name, ref in [("main", args.main), ("feature", args.feature)]}
    commands = []
    for dep in ["softfloat", "coremath", "zydis"]:
        shutil.copytree(root / "out/build/release/_deps" / (dep + "-src"),
                        out / "deps" / (dep + "-src"), ignore=shutil.ignore_patterns(".git"))
    for name, ref in refs.items():
        if args.only_feature and name == "main":
            continue
        src = out / (name + "-source")
        src.mkdir()
        with tarfile.open(fileobj=io.BytesIO(output("git", "-C", str(root), "archive", ref))) as archive:
            archive.extractall(src, filter="data")
        vendor = src / "vendor/x86recomp"
        vendor.parent.mkdir(exist_ok=True)
        subprocess.run(["git", "clone", "--shared", "--no-checkout", str((args.renderer_source or root / "vendor/x86recomp").resolve()), str(vendor)], check=True)
        pin = (src / "X86RECOMP").read_text().split()[1]
        subprocess.run(["git", "-C", str(vendor), "checkout", "--detach", pin], check=True)
        if name == "main":
            # Only the controlled benchmark normalization, never the renderer.
            (src / "src/bench.cpp").write_bytes(output("git", "-C", str(root), "show", refs["feature"] + ":src/bench.cpp"))
            shutil.copytree(root / "generated", src / "generated")
        else:
            if args.only_feature:
                shutil.copytree(root / "generated", src / "generated")
            else:
                (src / "generated").symlink_to(out / "main-source/generated", target_is_directory=True)
        bench = src / "src/bench.cpp"
        bench.write_text(fixed_camera(bench.read_text()))
        present = vendor / "src/runtime/hle/present.cpp"
        text = present.read_text().replace("#include <SDL3/SDL.h>", "#include <SDL3/SDL.h>\n#include <dlfcn.h>")
        anchor = "  // <ID>_HITCH=ms: report frames that took longer than that."
        assert text.count(anchor) == 1
        text = text.replace(anchor, '  // Identical diagnostic-only callback in both isolated builds.\n  static const auto measure = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "LibertyMeasureFrame"));\n  if (measure) measure();\n' + anchor)
        present.write_text(text)
        if name == "main":
            metal = vendor / "src/runtime/gfx/metal_device.mm"
            text = metal.read_text()
            anchor = "  static std::string RecipePath() {\n"
            assert text.count(anchor) == 1
            text = text.replace(anchor, anchor + '''    // Diagnostic path isolation; keep the shipped recipe/MSL policy.
    if (const char* path = std::getenv("LIBERTY_MEASURE_LEGACY_CACHE")) {
      NSString* dir = [NSString stringWithUTF8String:path];
      [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
      return [dir stringByAppendingPathComponent:@"metal-pipelines-v1.bin"].fileSystemRepresentation;
    }
''')
            metal.write_text(text)
        command = ["cmake", "--preset", "release", "-S", str(src), "-B", str(out / (name + "-build"))]
        command += ["-DFETCHCONTENT_SOURCE_DIR_" + dep.upper() + "=" + str(out / "deps" / (dep + "-src"))
                    for dep in ["softfloat", "coremath", "zydis"]]
        commands.append(command)
        subprocess.run(command, check=True)
        command = ["cmake", "--build", str(out / (name + "-build")), "--target", "gtaiv"]
        if name == "feature":
            command.append("recomp_shader_prepare")
        command += ["-j", str(args.jobs)]
        commands.append(command)
        with (out / (name + "-build.log")).open("w") as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    probe = out / "performance_probe.dylib"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-dynamiclib", str(root / "tools/performance_probe.cpp"), "-o", str(probe)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(probe)], check=True)
    generated_root = out / ("feature-source/generated" if args.only_feature else "main-source/generated")
    generated = {str(f.relative_to(generated_root)): hashlib.sha256(f.read_bytes()).hexdigest()
                 for f in generated_root.rglob("*") if f.is_file()}
    (out / "build-inputs.json").write_text(json.dumps({"refs": refs, "commands": commands,
                                                     "built_variants": ["feature"] if args.only_feature else list(refs),
                                                     "generated_sha256": generated}, indent=2) + "\n")


if __name__ == "__main__":
    main()

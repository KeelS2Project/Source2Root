#!/usr/bin/env python3
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
import package_extensions

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "dependencies.lock.json").read_text())
WINDOWS = os.name == "nt"
PLATFORM = "win64" if WINDOWS else "linuxsteamrt64"
OS = "windows" if WINDOWS else "linux"
SUFFIX = ".dll" if WINDOWS else ".so"


def run(*args, cwd=None, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def tree(source, target):
    shutil.copytree(source, target, ignore=shutil.ignore_patterns(".git", "__pycache__", "*.pyc"))


def snapshot(source, target):
    data = subprocess.check_output(["git", "archive", "HEAD"], cwd=source)
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        archive.extractall(target, filter="data")


def archive(source, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                bundle.write(path, path.relative_to(source).as_posix())


def extract(source, target):
    with zipfile.ZipFile(source) as bundle:
        bundle.extractall(target)
        if not WINDOWS:
            for entry in bundle.infolist():
                (target / entry.filename).chmod((entry.external_attr >> 16) & 0o777)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))
    if dirty and not args.allow_dirty:
        raise RuntimeError("Commit the candidate before packaging (local experiments may use --allow-dirty)")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    build = output / "build"
    binaries = build / args.configuration if WINDOWS else build
    keel = output / "build-keels2"
    deps = output / "deps"
    pawn = output / "build-sourcepawn"
    pawn_arch = OS + "-x86_64"
    compiler = "spcomp.exe" if WINDOWS else "spcomp"
    compiler_banner = subprocess.run([pawn / "spcomp" / pawn_arch / compiler],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
    if not re.search(r"^SourcePawn Compiler " + re.escape(LOCK["sourcepawn"]["version"]) + r"(?:\.|\s|$)",
                     compiler_banner, re.M):
        raise RuntimeError("Built SourcePawn compiler version does not match the dependency lock; rebuild dependencies")
    stage = output / "package-stage"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    dist = output / "dist"
    dist.mkdir(exist_ok=True)
    prefix = f"Source2Root-1.0.0-foundation-{revision[:12]}-{OS}-x64"
    source = stage / "source"
    own = source / "Source2Root"
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
    for name in filter(None, tracked):
        if (ROOT / name).is_file():
            copy(ROOT / name, own / name)
    for name in ("sourcepawn", "ambuild", "keels2"):
        actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=deps / name, text=True).strip()
        if actual != LOCK[name]["revision"] or subprocess.check_output(
                ["git", "diff", "HEAD", "--name-only"], cwd=deps / name, text=True).strip():
            raise RuntimeError(f"Dependency sources differ from the lock: {name}")
        snapshot(deps / name, source / "dependencies" / name)
    for path in ("third_party/amtl", "third_party/amtl/third_party/googletest"):
        snapshot(deps / "sourcepawn" / path, source / "dependencies/sourcepawn" / path)
    cache = (build / "CMakeCache.txt").read_text()
    extensions = package_extensions.inventory(build, args.configuration)
    if extensions["platform"] != PLATFORM:
        raise RuntimeError("Extension inventory platform does not match the packager")
    extension_sources = package_extensions.source_inputs(extensions, source, LOCK)
    sdk_root = Path(re.search(r"^KEELS2_SOURCE_SDK_RESOLVED_ROOT:INTERNAL=(.+)$", cache, re.M)[1])
    tree(sdk_root, source / "dependencies/hl2sdk")
    json_archive = deps / "json-source.tar.gz"
    if not json_archive.exists():
        url = f"https://codeload.github.com/nlohmann/json/tar.gz/{LOCK['json']['revision']}"
        with urllib.request.urlopen(url, timeout=60) as response:
            json_archive.write_bytes(response.read())
    if sha(json_archive) != LOCK["json"]["source_archive_sha256"]:
        raise RuntimeError("JSON source archive checksum mismatch")
    with tarfile.open(json_archive) as upstream:
        upstream.extractall(source / "dependencies/json", filter="data")
    copy(deps / "json.hpp", source / "dependencies/json.hpp")
    copy(ROOT / "licenses/GPL-3.0.txt", source / "GPL-3.0.txt")
    provenance = {
        "schema": 1, "version": "1.0.0", "channel": "foundation development candidate",
        "source2root_revision": revision, "dirty": dirty, "os": OS, "architecture": "x64",
        "configuration": args.configuration, "keels2_runtime": LOCK["keels2"]["revision"],
        "extension_sources": extension_sources,
        "dependencies": LOCK, "python": platform.python_version(),
        "cmake": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
        "ci_run": os.environ.get("GITHUB_RUN_ID"), "compiler_cache": [line for line in cache.splitlines()
            if re.match(r"CMAKE_(CXX_COMPILER|CXX_FLAGS|BUILD_TYPE):", line)]}
    # Paths in build provenance are intentionally reduced to tool names; SDKs must relocate.
    provenance["compiler_cache"] = [re.sub(r"(?<==).+[/\\]", "", line) if "COMPILER:" in line else line
                                      for line in provenance["compiler_cache"]]
    provenance["compiler_cache"] = [line.replace(str(output), "<OUTPUT>").replace(output.as_posix(), "<OUTPUT>")
        .replace(str(ROOT), "<SOURCE>").replace(ROOT.as_posix(), "<SOURCE>") for line in provenance["compiler_cache"]]
    provenance["compiler_cache"] = [re.sub(
        r"(-f(?:file|debug|macro)-prefix-map=|/pathmap:)[^=]+=", r"\1<BUILD_ROOT>=", line)
        for line in provenance["compiler_cache"]]
    compiler_info = next((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")).read_text()
    provenance["compiler"] = dict(re.findall(r'set\(CMAKE_CXX_(COMPILER_ID|COMPILER_VERSION|PLATFORM_ID|SIMULATE_VERSION) "([^"\n]*)"\)', compiler_info))
    provenance["runner_image"] = os.environ.get("ImageVersion", platform.platform())
    provenance["sourcepawn_build_options"] = ["--targets=x86_64", "--enable-optimize"] + ([] if WINDOWS else ["--enable-debug"])
    provenance["path_mapping"] = "SourcePawn diagnostic paths are relative; Windows uses /pathmap and basename-only PDB metadata"
    (source / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    source_archive = dist / (prefix + "-source.zip")
    archive(source, source_archive)
    runtime = stage / "runtime"
    developer = stage / "developer"
    optional = stage / "extensions"
    tree(keel / "package", runtime)
    native_root = runtime / "addons/keels2/plugins"
    if native_root.exists():
        shutil.rmtree(native_root)
    native = native_root / PLATFORM
    module_prefix = "" if WINDOWS else "lib"
    copy(binaries / (module_prefix + "source2root" + SUFFIX), native / ("source2root" + SUFFIX))
    copy(pawn / "libsourcepawn" / pawn_arch / ("libsourcepawn" + SUFFIX),
         runtime / "addons/source2root/bin" / PLATFORM / ("libsourcepawn" + SUFFIX))
    for manifest in sorted((ROOT / "plugins").glob("*/plugin.json")):
        plugin = manifest.parent.name
        copy(build / "scripts" / (plugin + ".smx"), runtime / "addons/source2root/plugins" / plugin / (plugin + ".smx"))
        copy(manifest, runtime / "addons/source2root/plugins" / plugin / "plugin.json")
    copy(ROOT / "configs/source2root.cfg", runtime / "cfg/source2root/source2root.cfg")
    for name in ("admins.cfg", "admin_groups.cfg", "allowed_maps.txt", "map_menu.txt"):
        copy(ROOT / "configs" / name, runtime / "addons/source2root/configs" / name)
    tree(output / "sdk", developer / "sdk")
    for header in ("extension.h", "extension.hpp", "native.h", "native.hpp", "callbacks.h", "http.h", "work_queue.hpp"):
        copy(ROOT / "include/source2root" / header, developer / "sdk/include/source2root" / header)
    extension_catalog = package_extensions.assemble(extensions, build, args.configuration, optional, developer)
    copy(ROOT / "cmake/Source2RootConfig.cmake", developer / "sdk/lib/cmake/Source2Root/Source2RootConfig.cmake")
    tree(ROOT / "examples", developer / "examples")
    tree(ROOT / "plugins", developer / "plugins")
    tree(ROOT / "scripting", developer / "scripting")
    tree(ROOT / "configs/extensions", developer / "configs/extensions")
    copy(pawn / "spcomp" / pawn_arch / compiler, developer / "compiler/bin" / PLATFORM / compiler)
    packages = (("runtime", runtime), ("developer", developer), ("extensions", optional))
    for _, package in packages:
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "dependencies.lock.json"):
            copy(ROOT / name, package / name)
        tree(ROOT / "licenses", package / "licenses")
        if (source / "extension-licenses").exists():
            tree(source / "extension-licenses", package / "licenses/extensions")
        if package != optional:
            copy(ROOT / "tools/live.py", package / "tools/live.py")
            copy(ROOT / "tools/migrate_admins.py", package / "tools/migrate_admins.py")
        copy(source_archive, package / "sources" / source_archive.name)
        copy(source / "provenance.json", package / "provenance.json")
        for path in package.rglob("*.cmake"):
            text = path.read_text()
            if str(output) in text or str(ROOT) in text:
                raise RuntimeError(f"Absolute build path in installed SDK: {path}")
    dependency_report = []
    for _, package in packages:
        for path in sorted(package.rglob("*")):
            if not path.is_file() or (path.suffix not in (".dll", ".exe")
                    and not re.search(r"\.so(?:\.\d+)*$", path.name) and path.name != "spcomp"):
                continue
            if WINDOWS:
                dumpbin = shutil.which("dumpbin")
                if not dumpbin:
                    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
                    install = subprocess.check_output([str(vswhere), "-latest", "-property", "installationPath"], text=True).strip()
                    dumpbin = str(sorted(Path(install).glob("VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"))[-1])
                report = subprocess.check_output([dumpbin, "/dependents", str(path)], text=True)
            else:
                run("strip", "--strip-unneeded", path)
                report = subprocess.check_output(["readelf", "-d", str(path)], text=True)
                for paths in re.findall(r"\((?:RPATH|RUNPATH)\).*\[([^]]*)\]", report):
                    if any(not entry.startswith(("$ORIGIN/", "${ORIGIN}/"))
                           and entry not in ("$ORIGIN", "${ORIGIN}") for entry in paths.split(":")):
                        raise RuntimeError(f"Non-relative dynamic search path: {path}")
                if re.search(r"Shared library: \[[^]]*/", report):
                    raise RuntimeError(f"Absolute dynamic dependency: {path}")
            data = path.read_bytes()
            for directory in (output, ROOT):
                for spelling in {str(directory), directory.as_posix(), str(directory).replace("/", "\\")}:
                    for encoding in ("utf-8", "utf-16le"):
                        if spelling.encode(encoding) in data:
                            start = data.index(spelling.encode(encoding))
                            detail = data[start:start + 240].split(b"\0", 1)[0].decode("utf-8", "replace")
                            raise RuntimeError(f"Absolute builder path remains in packaged binary: {path}: {detail}")
            dependency_report.append(str(path.relative_to(stage)) + "\n" + report)
    (dist / (prefix + "-dependencies.txt")).write_text("\n".join(dependency_report))
    archives = {}
    for name, package in packages:
        files = {p.relative_to(package).as_posix(): sha(p) for p in sorted(package.rglob("*")) if p.is_file()}
        (package / "files.sha256.json").write_text(json.dumps(files, indent=2) + "\n")
        archives[name] = dist / (prefix + f"-{name}.zip")
        archive(package, archives[name])
    verify = output / "package-verification"
    if verify.exists():
        shutil.rmtree(verify)
    for name, bundle in archives.items():
        extract(bundle, verify / name)
        for name_, expected in json.loads((verify / name / "files.sha256.json").read_text()).items():
            if sha(verify / name / name_) != expected:
                raise RuntimeError(f"Extracted file checksum failed: {name_}")
    extract(source_archive, verify / "source")
    dev = verify / "developer"
    scripts = verify / "compiled"
    scripts.mkdir()
    for example in sorted([*(dev / "examples").glob("*/*.sp"), *(dev / "plugins").glob("*/*.sp")]):
        run(dev / "compiler/bin" / PLATFORM / compiler, example.relative_to(dev), "-i", "scripting/include",
            "-i", "plugins/include", "-o", scripts / (example.stem + ".smx"), cwd=dev)
    extension_build = verify / "extension-build"
    run("cmake", "-S", dev / "examples/native", "-B", extension_build,
        f"-DCMAKE_PREFIX_PATH={dev / 'sdk'}", f"-DCMAKE_BUILD_TYPE={args.configuration}",
        f"-DKEELS2_SOURCE_SDK_ROOT={verify / 'source/dependencies/hl2sdk'}")
    run("cmake", "--build", extension_build, "--config", args.configuration, "--parallel", "2")
    ext_bin = extension_build / args.configuration if WINDOWS else extension_build
    fixture_source = verify / "source/Source2Root/tests/fixtures"
    for example in (fixture_source / "hello/hello.sp", verify / "source/Source2Root/tests/scripts/random_native.sp"):
        run(dev / "compiler/bin" / PLATFORM / compiler, example.name, "-i", dev / "scripting/include",
            "-o", scripts / (example.stem + ".smx"), cwd=example.parent)
    fixture_build = verify / "fixture-build"
    run("cmake", "-S", fixture_source / "native", "-B", fixture_build,
        f"-DCMAKE_PREFIX_PATH={dev / 'sdk'}", f"-DCMAKE_BUILD_TYPE={args.configuration}",
        f"-DKEELS2_SOURCE_SDK_ROOT={verify / 'source/dependencies/hl2sdk'}")
    run("cmake", "--build", fixture_build, "--config", args.configuration, "--parallel", "2")
    fixture_bin = fixture_build / args.configuration if WINDOWS else fixture_build
    rt = verify / "runtime"
    host_name = "keels2_host.dll" if WINDOWS else "libkeels2_host.so"
    tier_dir = keel / args.configuration if WINDOWS else keel
    tier_name = "tier0.dll" if WINDOWS else "libtier0.so"
    harness = binaries / ("sr_module_integration.exe" if WINDOWS else "sr_module_integration")
    adapter = binaries / (module_prefix + "sr_host_adapter" + SUFFIX)
    base = [harness, rt / "addons/keels2/bin" / PLATFORM / host_name, adapter, tier_dir / tier_name,
            rt / "addons/keels2/plugins" / PLATFORM / ("source2root" + SUFFIX)]
    tail = [fixture_bin / ("sr_example" + SUFFIX), rt / "addons/source2root/bin" / PLATFORM / ("libsourcepawn" + SUFFIX),
            scripts / "hello.smx", fixture_source / "hello/plugin.json"]
    for variant, module in (("shipped", dev / "extensions" / PLATFORM / ("source2root_random" + SUFFIX)),
                            ("rebuilt", ext_bin / ("source2root_random" + SUFFIX))):
        run(*base, *tail, verify / ("host-" + variant), "package", build / "test-fixtures/gameevents.pb",
            module, scripts / "roll.smx", dev / "examples/roll/plugin.json",
            verify / "extensions/addons/keels2/plugins" / PLATFORM)
    expected_scripts = {p.parent.name for p in (ROOT / "plugins").glob("*/plugin.json")}
    actual_scripts = {p.parent.name for p in (rt / "addons/source2root/plugins").glob("*/*.smx")}
    actual_native = {p.relative_to(rt / "addons/keels2/plugins").as_posix()
                     for p in (rt / "addons/keels2/plugins").rglob("*") if p.is_file()}
    if actual_scripts != expected_scripts or actual_native != {PLATFORM + "/source2root" + SUFFIX}:
        raise RuntimeError("Unexpected test or example plugins in runtime archive")
    files = [source_archive, *archives.values(), dist / (prefix + "-dependencies.txt")]
    (dist / (prefix + "-SHA256SUMS.txt")).write_text("".join(f"{sha(p)}  {p.name}\n" for p in files))
    (dist / (prefix + "-verification.json")).write_text(json.dumps({
        "revision": revision, "dirty": dirty, "extracted_checksums": "pass", "compiler": "pass",
        "installed_sdk_extension": "pass", "shipped_and_rebuilt_actual_module_execution": "pass",
        "runtime_script_plugins": sorted(actual_scripts), "test_fixtures_installed": False,
        "optional_extensions": extension_catalog["modules"], "optional_extension_load_and_shutdown": "pass",
        "dynamic_dependency_inventory": "pass", "real_cs2_client": "external gate, not tested"}, indent=2) + "\n")
    print(f"Candidate packages verified: {dist}", flush=True)


if __name__ == "__main__":
    main()

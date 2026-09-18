#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import venv

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "dependencies.lock.json").read_text())


def run(*args, cwd=None, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def checkout(path, repository, revision, recursive=False, local_source=None):
    if not path.exists():
        run("git", "clone", local_source or repository, path)
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=path, text=True)
    if dirty:
        raise RuntimeError(f"Refusing to change dirty dependency: {path}")
    try:
        run("git", "cat-file", "-e", revision + "^{commit}", cwd=path)
    except subprocess.CalledProcessError:
        run("git", "fetch", local_source or "origin", revision, cwd=path)
    run("git", "checkout", "--detach", revision, cwd=path)
    if recursive:
        run("git", "submodule", "update", "--init", "--recursive", cwd=path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--configuration", default="RelWithDebInfo")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--keels2-source", type=Path,
        help="Local repository containing the locked KeelS2 commit; copied into the build dependencies")
    parser.add_argument("--cmake-arg", action="append", default=[],
        help="Additional Source2Root configure argument, e.g. --cmake-arg=-DOPENSSL_ROOT_DIR=path")
    args = parser.parse_args()
    output = args.output.resolve()
    deps = output / "deps"
    deps.mkdir(parents=True, exist_ok=True)
    for name in ("keels2", "sourcepawn", "ambuild"):
        dependency = LOCK[name]
        local_source = args.keels2_source.resolve(strict=True) if name == "keels2" and args.keels2_source else None
        if local_source:
            run("git", "cat-file", "-e", dependency["revision"] + "^{commit}", cwd=local_source)
        checkout(deps / name, dependency["repository"], dependency["revision"], name == "sourcepawn", local_source)
    for path, key in (("third_party/amtl", "amtl_revision"),
                      ("third_party/amtl/third_party/googletest", "googletest_revision")):
        actual = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                         cwd=deps / "sourcepawn" / path, text=True).strip()
        if actual != LOCK["sourcepawn"][key]:
            raise RuntimeError(f"Wrong SourcePawn submodule: {path}")
    header = deps / "json.hpp"
    if not header.exists():
        url = f"https://raw.githubusercontent.com/nlohmann/json/{LOCK['json']['revision']}/single_include/nlohmann/json.hpp"
        with urllib.request.urlopen(url, timeout=60) as response:
            header.write_bytes(response.read())
    if hashlib.sha256(header.read_bytes()).hexdigest() != LOCK["json"]["header_sha256"]:
        raise RuntimeError("JSON header checksum mismatch")
    python = output / "venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(output / "venv")
    ambuild_source = output / "ambuild-install-source"
    if ambuild_source.exists():
        shutil.rmtree(ambuild_source)
    shutil.copytree(deps / "ambuild", ambuild_source,
                    ignore=shutil.ignore_patterns(".git", "__pycache__", "*.egg-info"))
    run(python, "-m", "pip", "install", "setuptools==75.8.0", ambuild_source)
    env = os.environ.copy()
    if os.name != "nt":
        env.update(CC="clang", CXX="clang++", CXXFLAGS=
                   f'-Wno-error=deprecated-declarations "-ffile-prefix-map={deps / "sourcepawn"}=sourcepawn"')
    else:
        env["CXXFLAGS"] = f'/experimental:deterministic /pathmap:"{(deps / "sourcepawn").as_posix()}"=sourcepawn'
        env["LINK"] = (env.get("LINK", "") + " /PDBALTPATH:%_PDB%").strip()
    cmake_flags = []
    if os.name == "nt":
        mapped_flags = f'/experimental:deterministic /pathmap:"{output.as_posix()}"=build /pathmap:"{ROOT.as_posix()}"=source2root'
        cmake_flags = [f'-DCMAKE_CXX_FLAGS={mapped_flags}', f'-DCMAKE_C_FLAGS={mapped_flags}',
                       '-DCMAKE_SHARED_LINKER_FLAGS=/PDBALTPATH:%_PDB%',
                       '-DCMAKE_MODULE_LINKER_FLAGS=/PDBALTPATH:%_PDB%',
                       '-DCMAKE_EXE_LINKER_FLAGS=/PDBALTPATH:%_PDB%']
    pawn_build = output / "build-sourcepawn"
    pawn_build.mkdir(exist_ok=True)
    pawn_version = (deps / "sourcepawn/product.version").read_text().strip()
    if pawn_version != LOCK["sourcepawn"]["version"]:
        raise RuntimeError("SourcePawn source version does not match the dependency lock")
    version_header = pawn_build / "includes/sourcemod_version.h"
    if version_header.exists() and f'#define SM_VERSION_STRING "{pawn_version}"' not in version_header.read_text().splitlines():
        version_header.unlink()
    pawn_options = ["--targets=x86_64", "--enable-optimize"]
    if os.name != "nt":
        pawn_options.append("--enable-debug")
    run(python, deps / "sourcepawn/configure.py", *pawn_options,
        cwd=pawn_build, env=env)
    ambuild = output / "venv" / ("Scripts/ambuild.exe" if os.name == "nt" else "bin/ambuild")
    run(ambuild, "-j", "2", cwd=pawn_build, env=env)
    sdk_build = output / "build-keels2"
    sdk = output / "sdk"
    run("cmake", "-S", deps / "keels2", "-B", sdk_build,
        "-DBUILD_TESTING=ON", f"-DCMAKE_BUILD_TYPE={args.configuration}", f"-DCMAKE_INSTALL_PREFIX={sdk}", *cmake_flags)
    run("cmake", "--build", sdk_build, "--config", args.configuration, "--parallel", "2",
        "--target", "keels2_unload_preparation_tests", "keels2_source2_service_plugin", "keels2_player_input_c_abi")
    run("ctest", "--test-dir", sdk_build, "-C", args.configuration, "--output-on-failure",
        "-R", "^(bootstrap_unload_preparation|bootstrap_source2_service|player_input_c_abi|clean_authoring_contract|schema_entity_native_bridge|native_message_contract)$")
    run("cmake", "--install", sdk_build, "--config", args.configuration)
    build = output / ("build-sanitizers" if args.sanitizers else "build")
    run("cmake", "-S", ROOT, "-B", build, f"-DCMAKE_BUILD_TYPE={args.configuration}",
        "-UKeelS2_DIR", f"-DCMAKE_PREFIX_PATH={sdk}", f"-DSR_KEELS2_BUILD={sdk_build}", f"-DSOURCEPAWN_ROOT={deps / 'sourcepawn'}",
        f"-DSOURCEPAWN_BUILD={pawn_build}", f"-DSR_JSON_INCLUDE={deps}",
        f"-DSR_SANITIZERS={'ON' if args.sanitizers else 'OFF'}", *cmake_flags, *args.cmake_arg)
    run("cmake", "--build", build, "--config", args.configuration, "--parallel", "2")
    run("ctest", "--test-dir", build, "-C", args.configuration, "--output-on-failure", "--timeout", "30")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build private Windows TLS/compression libraries and emit a CMake preload file."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--vcpkg-source", required=True, type=Path,
        help="Checkout at the locked vcpkg revision, or its packaged corresponding sources")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Run this helper on Windows")
    lock = json.loads((ROOT / "dependencies.lock.json").read_text())
    recipe = args.vcpkg_source.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (recipe / ".git").exists():
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=recipe, text=True).strip()
        dirty = subprocess.check_output(["git", "diff", "HEAD", "--name-only"], cwd=recipe, text=True).strip()
        if revision != lock["vcpkg"]["revision"] or dirty:
            raise RuntimeError("Use the clean vcpkg revision in dependencies.lock.json")
    else:
        manifest = json.loads((recipe.parent.parent / "extension-source-inputs.json").read_text())["vcpkg"]
        if manifest["upstream"]["revision"] != lock["vcpkg"]["revision"]:
            raise RuntimeError("Packaged vcpkg sources do not match the lock")
        for name, expected in manifest["files"].items():
            if hashlib.sha256((recipe / name).read_bytes()).hexdigest() != expected:
                raise RuntimeError(f"Packaged vcpkg source changed: {name}")
    if not (recipe / "vcpkg.exe").is_file():
        subprocess.run(["cmd.exe", "/d", "/c", str(recipe / "bootstrap-vcpkg.bat"), "-disableMetrics"], check=True)
    triplet = lock["vcpkg"]["triplet"]
    overlays = output / "vcpkg-overlay-ports"
    for name in lock["vcpkg"]["ports"]:
        port = overlays / name
        shutil.copytree(recipe / "ports" / name, port, dirs_exist_ok=True)
        script = port / "portfile.cmake"
        content = script.read_text()
        if name == "openssl":
            if content.count("    PATCHES\n") != 1:
                raise RuntimeError("Review the updated OpenSSL recipe before applying build metadata mapping")
            content = content.replace("    PATCHES\n", "    PATCHES\n        source2root-buildinfo.patch\n")
            shutil.copy2(ROOT / "cmake/openssl_buildinfo.patch", port / "source2root-buildinfo.patch")
        content += '\nfile(WRITE "$ENV{SR_WINDOWS_DEPENDENCY_ROOT}/' + name + '-source-path.txt" "${SOURCE_PATH}")\n'
        script.write_text(content)
    # Classic vcpkg install can retain an installed package when an overlay
    # changes without a version bump. Keep each recipe's installation separate.
    recipe_inputs = {"vcpkg": lock["vcpkg"], "triplet_sha256": hashlib.sha256(
        (ROOT / "cmake/triplets" / (triplet + ".cmake")).read_bytes()).hexdigest(),
        "overlay_files": {p.relative_to(overlays).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(overlays.rglob("*")) if p.is_file()}}
    recipe_id = hashlib.sha256(json.dumps(recipe_inputs, sort_keys=True).encode()).hexdigest()[:16]
    installed = output / "vcpkg-installed" / recipe_id
    env = dict(os.environ, VCPKG_MAX_CONCURRENCY="2", VCPKG_DISABLE_METRICS="1",
        SR_WINDOWS_DEPENDENCY_ROOT=str(output))
    command = [str(recipe / "vcpkg.exe"), "install", *[name + ":" + triplet for name in lock["vcpkg"]["ports"]],
        "--classic", "--binarysource=clear", "--no-print-usage",
        "--overlay-triplets=" + str(ROOT / "cmake/triplets"),
        "--overlay-ports=" + str(overlays),
        "--x-buildtrees-root=" + str(output / "vcpkg-buildtrees"),
        "--x-packages-root=" + str(output / "vcpkg-packages"),
        "--x-install-root=" + str(installed),
        "--downloads-root=" + str(output / "vcpkg-downloads")]
    subprocess.run(command, env=env, check=True)
    prefix = installed / triplet
    values = {"OPENSSL_ROOT_DIR": prefix, "ZLIB_ROOT": prefix, "SR_VCPKG_SOURCE": recipe}
    for name in lock["vcpkg"]["ports"]:
        source = Path((output / (name + "-source-path.txt")).read_text()).resolve(strict=True)
        if not source.is_relative_to(output / "vcpkg-buildtrees" / name / "src"):
            raise RuntimeError(f"Unexpected {name} corresponding source path")
        values["SR_" + name.upper() + "_SOURCE"] = source
    perl = list((output / "vcpkg-downloads/tools/perl").glob("*/perl/bin/perl.exe"))
    if len(perl) == 1:
        values["SR_PG_PERL"] = perl[0]
    # FindOpenSSL uses legacy EAY cache entries on Windows. Changing only its
    # root can leave a prior library linked against the newly selected headers.
    cached = ["OPENSSL_INCLUDE_DIR", "OPENSSL_SSL_LIBRARY", "OPENSSL_CRYPTO_LIBRARY",
        "LIB_EAY_RELEASE", "LIB_EAY_DEBUG", "LIB_EAY_LIBRARY_DEBUG",
        "SSL_EAY_RELEASE", "SSL_EAY_DEBUG", "SSL_EAY_LIBRARY_DEBUG",
        "ZLIB_INCLUDE_DIR", "ZLIB_LIBRARY_RELEASE", "ZLIB_LIBRARY_DEBUG"]
    lines = [f"unset({key} CACHE)" for key in cached]
    lines += [f'set({key} "{path.as_posix()}" CACHE PATH "Private Windows dependency" FORCE)'
        for key, path in values.items()]
    lines += ["set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL \"Private static TLS\" FORCE)",
        "set(ZLIB_USE_STATIC_LIBS TRUE CACHE BOOL \"Private static compression\" FORCE)"]
    cache = output / "windows-dependencies.cmake"
    cache.write_text("\n".join(lines) + "\n")
    (output / "windows-dependencies-build.json").write_text(json.dumps({
        "recipe_id": recipe_id, "recipe_inputs": recipe_inputs,
        "vcpkg": lock["vcpkg"], "triplet_sha256": hashlib.sha256(
            (ROOT / "cmake/triplets" / (triplet + ".cmake")).read_bytes()).hexdigest(),
        "openssl_buildinfo_patch_sha256": hashlib.sha256(
            (ROOT / "cmake/openssl_buildinfo.patch").read_bytes()).hexdigest(),
        "source_paths": {key: str(path) for key, path in values.items()}, "configure_preload": str(cache)}, indent=2) + "\n")
    print(f"Configure Source2Root with --cmake-arg=-C{cache}")


if __name__ == "__main__":
    main()

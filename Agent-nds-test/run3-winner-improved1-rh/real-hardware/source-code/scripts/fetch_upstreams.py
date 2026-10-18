#!/usr/bin/env python3
"""Fetch or verify the exact, locally used upstream snapshots.

This script creates source checkouts under vendor/; it does not copy upstream
sources into Git, grant redistribution rights, or modify an existing checkout.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "upstream-lock.json"
DEFAULT_VENDOR_DIR = ROOT / "vendor"
_REVISION = re.compile(r"^[0-9a-f]{40}$")
_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


class FetchError(RuntimeError):
    """A pinned upstream is absent, modified, or could not be fetched safely."""


def _run_git(args: list[str], *, cwd: Path | None = None) -> str:
    env = os.environ.copy()
    env["GIT_TERMINAL_PROMPT"] = "0"
    try:
        proc = subprocess.run(
            ["git", *args],
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        raise FetchError(f"Cannot run Git; install Git and retry: {exc}") from exc
    if proc.returncode:
        detail = (proc.stderr or proc.stdout).strip()
        raise FetchError(f"git {' '.join(args)} failed: {detail or proc.returncode}")
    return proc.stdout.strip()


def _safe_relative_path(value: str, *, label: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if not value or path.is_absolute() or ".." in path.parts or "" in path.parts:
        raise FetchError(f"Unsafe {label} path in upstream lock: {value!r}")
    return path


def load_lock(path: Path) -> list[dict[str, Any]]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise FetchError(f"Upstream lock file is missing: {path}") from exc
    except json.JSONDecodeError as exc:
        raise FetchError(f"Invalid JSON in {path}: {exc}") from exc
    except OSError as exc:
        raise FetchError(f"Cannot read upstream lock {path}: {exc}") from exc

    if not isinstance(lock, dict) or lock.get("schema") != 1:
        raise FetchError("Unsupported upstream-lock schema (expected schema 1)")
    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        raise FetchError("Upstream lock must contain a non-empty dependencies list")

    seen: set[str] = set()
    for dependency in dependencies:
        if not isinstance(dependency, dict):
            raise FetchError("Each upstream dependency entry must be an object")
        name = dependency.get("name")
        url = dependency.get("url")
        revision = dependency.get("revision")
        required = dependency.get("required_paths")
        if not isinstance(name, str) or not _NAME.fullmatch(name):
            raise FetchError(f"Invalid dependency name: {name!r}")
        if name in seen:
            raise FetchError(f"Duplicate dependency name: {name}")
        seen.add(name)
        if not isinstance(url, str) or not url.strip():
            raise FetchError(f"Missing repository URL for {name}")
        if not isinstance(revision, str) or not _REVISION.fullmatch(revision):
            raise FetchError(f"{name}: revision must be a full lowercase 40-digit commit ID")
        if not isinstance(required, list) or not required or not all(
            isinstance(value, str) for value in required
        ):
            raise FetchError(f"{name}: required_paths must be a non-empty string list")
        for value in required:
            _safe_relative_path(value, label=f"{name} required")
    return dependencies


def _canonical_remote(value: str) -> str:
    value = value.strip()
    if value.startswith("git@github.com:"):
        value = "https://github.com/" + value.removeprefix("git@github.com:")
    if value.endswith(".git"):
        value = value[:-4]
    return value.rstrip("/")


def _same_remote(actual: str, expected: str) -> bool:
    # Tests and private mirrors may use local paths or file:// URLs.
    if "://" not in actual and "://" not in expected and not actual.startswith("git@") and not expected.startswith("git@"):
        return Path(actual).resolve() == Path(expected).resolve()
    return _canonical_remote(actual) == _canonical_remote(expected)


def _validate_checkout(dependency: dict[str, Any], checkout: Path) -> None:
    if checkout.is_symlink() or not checkout.is_dir():
        raise FetchError(f"{dependency['name']}: expected a real Git checkout at {checkout}")

    try:
        top = Path(_run_git(["-C", str(checkout), "rev-parse", "--show-toplevel"])).resolve()
    except FetchError as exc:
        raise FetchError(f"{dependency['name']}: not a valid Git checkout at {checkout}") from exc
    if top != checkout.resolve():
        raise FetchError(f"{dependency['name']}: checkout root mismatch ({top})")

    revision = _run_git(["-C", str(checkout), "rev-parse", "HEAD"])
    if revision != dependency["revision"]:
        raise FetchError(
            f"{dependency['name']}: wrong revision {revision}; expected {dependency['revision']}"
        )

    remote = _run_git(["-C", str(checkout), "remote", "get-url", "origin"])
    if not _same_remote(remote, dependency["url"]):
        raise FetchError(
            f"{dependency['name']}: origin is {remote!r}, expected {dependency['url']!r}"
        )

    dirty = _run_git(
        ["-C", str(checkout), "status", "--porcelain", "--untracked-files=all"]
    )
    if dirty:
        raise FetchError(f"{dependency['name']}: checkout has local changes; refusing to use it")

    for value in dependency["required_paths"]:
        relative = _safe_relative_path(value, label=f"{dependency['name']} required")
        candidate = checkout.joinpath(*relative.parts)
        if not candidate.exists():
            raise FetchError(f"{dependency['name']}: required upstream path is missing: {value}")


def check_dependency(dependency: dict[str, Any], vendor_dir: Path) -> None:
    target = vendor_dir / dependency["name"]
    if target.is_symlink() or not target.exists():
        raise FetchError(
            f"{dependency['name']}: missing at {target}; run `python3 scripts/fetch_upstreams.py`"
        )
    _validate_checkout(dependency, target)


def fetch_dependency(dependency: dict[str, Any], vendor_dir: Path) -> bool:
    """Fetch a missing dependency. Return True if fetched; never overwrite an existing path."""
    vendor_dir.mkdir(parents=True, exist_ok=True)
    target = vendor_dir / dependency["name"]
    if target.exists() or target.is_symlink():
        _validate_checkout(dependency, target)
        return False

    staging = Path(tempfile.mkdtemp(prefix=f".{dependency['name']}.fetch-", dir=vendor_dir))
    try:
        _run_git(["init", "--quiet", str(staging)])
        _run_git(["-C", str(staging), "remote", "add", "origin", dependency["url"]])
        _run_git(
            ["-C", str(staging), "fetch", "--quiet", "--depth", "1", "origin", dependency["revision"]]
        )
        _run_git(["-C", str(staging), "checkout", "--quiet", "--detach", "FETCH_HEAD"])
        _validate_checkout(dependency, staging)
        os.replace(staging, target)
        return True
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def _absolute_path(value: str, root: Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", default=str(DEFAULT_LOCK), help="lock file (default: upstream-lock.json)")
    parser.add_argument("--vendor-dir", default=str(DEFAULT_VENDOR_DIR), help="local dependency directory")
    parser.add_argument("--check", action="store_true", help="verify pinned checkouts without network access")
    args = parser.parse_args(argv)
    lock_path = _absolute_path(args.lock, Path.cwd())
    vendor_dir = _absolute_path(args.vendor_dir, Path.cwd())

    try:
        dependencies = load_lock(lock_path)
    except FetchError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 3

    failures = 0
    for dependency in dependencies:
        try:
            if args.check:
                check_dependency(dependency, vendor_dir)
                action = "verified"
            else:
                action = "fetched" if fetch_dependency(dependency, vendor_dir) else "already present"
            print(f"{dependency['name']}: {action} at {dependency['revision']}")
        except (FetchError, OSError) as exc:
            failures += 1
            print(f"BLOCKED: {exc}", file=sys.stderr)

    if failures:
        return 2
    if args.check:
        print("Pinned upstream checkouts are clean and ready.")
    else:
        print("Pinned upstreams are available locally. Redistribution rights are unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Install UEAtelier's UnrealMcp plugin workflow into an existing Unreal project.

This helper is intentionally conservative: it copies only the reusable plugin
and workflow folders, skips generated/local artifacts, and updates the target
.uproject after creating a backup.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from datetime import datetime
from pathlib import Path


ROOT_MARKERS = ("Plugins/UnrealMcp/UnrealMcp.uplugin", "Tools", "Schemas", "Docs")
WORKFLOW_DIRS = ("Tools", "Schemas", "Docs")
PLUGIN_RELATIVE = Path("Plugins") / "UnrealMcp"
SKIP_DIR_NAMES = {
    ".git",
    ".vs",
    ".vscode",
    "__pycache__",
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
}
SKIP_TOOL_DIRS = {
    Path("Tools") / "UnrealMcpSupervisor",
}
REQUIRED_PLUGINS = ("PythonScriptPlugin", "UnrealMcp")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def find_repo_root(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if all((candidate / marker).exists() for marker in ROOT_MARKERS):
            return candidate
    fail(f"Could not find UEAtelier repo root from {start}")


def resolve_project_path(value: str) -> Path:
    path = Path(value).expanduser().resolve()
    if path.is_dir():
        projects = sorted(path.glob("*.uproject"))
        if len(projects) != 1:
            fail(f"Expected exactly one .uproject in {path}, found {len(projects)}")
        return projects[0]
    if path.suffix != ".uproject":
        fail(f"Target must be a .uproject file or project directory: {path}")
    if not path.exists():
        fail(f"Target .uproject does not exist: {path}")
    return path


def should_ignore(src: Path, root: Path) -> bool:
    rel = src.relative_to(root)
    if src.is_dir() and src.name in SKIP_DIR_NAMES:
        return True
    if any(rel == skipped or skipped in rel.parents for skipped in SKIP_TOOL_DIRS):
        return True
    if src.suffix in {".pyc", ".pyo"}:
        return True
    if src.name in {".DS_Store"}:
        return True
    return False


def copy_tree_clean(src: Path, dst: Path, repo_root: Path, dry_run: bool) -> None:
    if not src.exists():
        fail(f"Source path does not exist: {src}")

    if src.resolve() == dst.resolve():
        print(f"Skipped {dst}; source and destination are the same path.")
        return

    if dry_run:
        print(f"DRY RUN: would replace {dst} from {src}")
        return

    if dst.exists():
        shutil.rmtree(dst)

    def ignore(directory: str, names: list[str]) -> set[str]:
        ignored: set[str] = set()
        directory_path = Path(directory)
        for name in names:
            child = directory_path / name
            try:
                if should_ignore(child.resolve(), repo_root):
                    ignored.add(name)
            except FileNotFoundError:
                continue
        return ignored

    shutil.copytree(src, dst, ignore=ignore)
    print(f"Copied {src} -> {dst}")


def ensure_plugins_enabled(uproject: Path, dry_run: bool) -> None:
    data = json.loads(uproject.read_text(encoding="utf-8-sig"))
    plugins = data.setdefault("Plugins", [])
    if not isinstance(plugins, list):
        fail(f"{uproject} has a non-array Plugins field")

    changed = False
    by_name = {}
    for plugin in plugins:
        if isinstance(plugin, dict) and isinstance(plugin.get("Name"), str):
            by_name[plugin["Name"]] = plugin

    for name in REQUIRED_PLUGINS:
        entry = by_name.get(name)
        if entry is None:
            entry = {"Name": name, "Enabled": True, "TargetAllowList": ["Editor"]}
            plugins.append(entry)
            changed = True
            continue

        if entry.get("Enabled") is not True:
            entry["Enabled"] = True
            changed = True
        if "TargetAllowList" not in entry:
            entry["TargetAllowList"] = ["Editor"]
            changed = True

    if not changed:
        print(f"Plugins already enabled in {uproject}")
        return

    if dry_run:
        print(f"DRY RUN: would enable {', '.join(REQUIRED_PLUGINS)} in {uproject}")
        return

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = uproject.with_suffix(uproject.suffix + f".bak-{timestamp}")
    shutil.copy2(uproject, backup)
    uproject.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"Updated {uproject}")
    print(f"Backup written to {backup}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install UEAtelier's UnrealMcp plugin workflow into an existing Unreal project."
    )
    parser.add_argument(
        "--project",
        required=True,
        help="Target .uproject path or target project directory.",
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="UEAtelier repository root. Defaults to auto-detecting from this script.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned changes without copying files or editing the .uproject.",
    )
    args = parser.parse_args()

    script_path = Path(__file__).resolve()
    repo_root = Path(args.repo_root).expanduser().resolve() if args.repo_root else find_repo_root(script_path.parent)
    project_file = resolve_project_path(args.project)
    project_dir = project_file.parent

    if (project_dir / "Engine" / "Plugins").exists():
        fail("Target looks like an Unreal Engine root, not a project root. Install UEAtelier as a project plugin.")

    print(f"UEAtelier repo: {repo_root}")
    print(f"Target project: {project_file}")

    copy_tree_clean(repo_root / PLUGIN_RELATIVE, project_dir / PLUGIN_RELATIVE, repo_root, args.dry_run)
    for folder in WORKFLOW_DIRS:
        copy_tree_clean(repo_root / folder, project_dir / folder, repo_root, args.dry_run)
    ensure_plugins_enabled(project_file, args.dry_run)

    print("Install plan complete.")
    print("Next: close Unreal Editor, rebuild the target project, reopen it, then run /tool unreal.editor_status {}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

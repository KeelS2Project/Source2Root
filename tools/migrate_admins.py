#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path
import re


COMMAND_PERMISSIONS = {
    name: "admin." + {"admin": "menu", "map": "changemap"}.get(name, name)
    for name in ("help", "admin", "who", "reloadadmins", "kick", "ban", "unban", "slap", "slay",
                 "mute", "unmute", "gag", "ungag", "silence", "unsilence", "say", "map", "restart")
}
BASE = 76561197960265728


def lower(value):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in value)


def invalid_constant(value):
    raise ValueError(f"Invalid JSON constant: {value}")


def unique_pairs(pairs):
    result = {}

    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")

        result[key] = value

    return result


def read(path, maximum=1024 * 1024):
    with path.open("rb") as source:
        raw = source.read(maximum + 1)

    if len(raw) > maximum:
        raise ValueError(f"Input exceeds {maximum} bytes: {path}")

    data = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_pairs, parse_constant=invalid_constant)

    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1:
        raise ValueError(f"Expected JSON schema 1: {path}")

    return raw, data


def identity(value):
    if not isinstance(value, str):
        raise ValueError("Steam identity must be a string")

    if match := re.fullmatch(r"STEAM_[01]:([01]):([0-9]+)", value):
        account = int(match[2]) * 2 + int(match[1])
        result = BASE + account
    elif match := re.fullmatch(r"\[U:1:([0-9]+)\]", value):
        result = BASE + int(match[1])
    elif re.fullmatch(r"[0-9]+", value):
        result = int(value)
    else:
        raise ValueError(f"Invalid Steam identity: {value}")

    if not BASE < result <= BASE + 0xffffffff:
        raise ValueError(f"Steam identity is outside the individual public account range: {value}")

    return str(result)


def text(value, limit, field):
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > limit or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError(f"{field} requires 1..{limit} printable bytes")

    return value


def immunity(value):
    if type(value) is not int or not 0 <= value <= 2147483647:
        raise ValueError("Immunity must be an integer from 0 to 2147483647")

    return value


def named_permission(value):
    if not isinstance(value, str) or len(value) > 96 or not re.fullmatch(r"[a-z0-9_-]+(?:\.[a-z0-9_-]+)*", value):
        raise ValueError(f"Invalid named permission: {value}")

    return value


def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def prepare(admin_path, ban_path=None):
    original, data = read(admin_path)
    entries = data.get("admins")

    if not isinstance(entries, list) or len(entries) > 4096:
        raise ValueError("Expected an admins array with at most 4096 entries")

    native = "groups" in data
    groups, admins, seen, imported = {}, [], set(), {}
    extra = {key: value for key, value in data.items() if key not in ("schema", "groups", "admins")}
    converted = {"sr_show_activity": 13, "allowed_maps": []}

    if native:
        announce = data.get("announce_actions", True)

        if type(announce) is not bool:
            raise ValueError("announce_actions must be a boolean")

        maps = data.get("allowed_maps", [])

        if not isinstance(maps, list) or len(maps) > 1024 or any(
                not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]{1,64}", name) for name in maps):
            raise ValueError("allowed_maps requires at most 1024 lowercase map names")

        converted = {"sr_show_activity": 13 if announce else 0, "allowed_maps": maps}
        extra.pop("announce_actions", None)
        extra.pop("allowed_maps", None)

        if not isinstance(data["groups"], dict):
            raise ValueError("Expected a groups object")

        for name, group in data["groups"].items():
            text(name, 64, "Group name")
            key = lower(name)

            if key in groups or not isinstance(group, dict) or not isinstance(group.get("permissions"), list):
                raise ValueError(f"Duplicate or invalid group: {name}")

            permissions = set()

            for value in group["permissions"]:
                if not isinstance(value, str):
                    raise ValueError("Legacy command permissions must be strings")

                if value == "*":
                    permissions.update(COMMAND_PERMISSIONS.values())
                elif value in COMMAND_PERMISSIONS:
                    permissions.add(COMMAND_PERMISSIONS[value])
                else:
                    raise ValueError(f"Unknown legacy command permission: {value}")

            groups[key] = (name, immunity(group.get("immunity", 0)), sorted(permissions))
            unknown = {k: v for k, v in group.items() if k not in ("permissions", "immunity")}

            if unknown:
                extra[f"groups/{name}"] = unknown

    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ValueError("Administrator entries must be objects")

        account = identity(entry.get("steamid" if native else "identity"))

        if account in seen:
            raise ValueError(f"Duplicate normalized administrator identity: {account}")

        seen.add(account)

        if native:
            group_name = text(entry.get("group"), 64, "Administrator group")

            if lower(group_name) not in groups:
                raise ValueError(f"Unknown group for administrator {account}: {group_name}")

            group = groups[lower(group_name)]
        else:
            values = entry.get("permissions")

            if not isinstance(values, list):
                raise ValueError("Foundation permissions must be an array")

            permissions = tuple(sorted({named_permission(value) for value in values}))

            if permissions not in imported:
                imported[permissions] = "imported_" + account

            group_name = imported[permissions]
            group = groups.setdefault(group_name, (group_name, 0, list(permissions)))

        label = text(entry.get("name", account), 128, "Administrator label")
        level = immunity(entry.get("immunity", group[1]))
        admins.append((label, account, group[0], level))
        allowed = {"steamid", "group", "immunity", "name"} if native else {"identity", "permissions", "immunity", "name"}
        unknown = {k: v for k, v in entry.items() if k not in allowed}

        if unknown:
            extra[f"admins/{index}"] = unknown

    if len(groups) > 128:
        raise ValueError("Migration requires more than 128 groups; consolidate groups before migrating")

    group_lines = ['"Groups"', '{']

    for name, level, permissions in sorted(groups.values()):
        group_lines += ["    " + quote(name), "    {", "        \"immunity\" " + quote(level)]

        if lower(name) != "root" and permissions:
            group_lines += ['        "permissions"', '        {']
            group_lines += ["            " + quote(value) + ' "1"' for value in permissions]
            group_lines += ['        }']

        group_lines += ['    }']

    group_lines += ['}', '']
    admin_lines = ['"Admins"', '{']

    for label, account, group_name, level in admins:
        admin_lines += ["    " + quote(label), "    {", '        "identity" ' + quote(account),
                        '        "group" ' + quote(group_name), '        "immunity" ' + quote(level), "    }"]

    admin_lines += ['}', '']
    files = {"configs/admins.cfg": "\n".join(admin_lines).encode(),
             "configs/admin_groups.cfg": "\n".join(group_lines).encode(), "originals/admins.json": original}
    files["configs/allowed_maps.txt"] = ("\n".join(converted["allowed_maps"]) + "\n").encode()
    files["cfg/source2root/source2root.cfg"] = (
            f"sr_show_activity {converted['sr_show_activity']}\n"
            'sr_chat_public_trigger "!"\nsr_chat_silent_trigger "/"\nsr_debug 0\n').encode()
    report = {"schema": 1, "source_format": "legacy native" if native else "foundation permissions",
              "source_admins": str(admin_path.resolve()), "admin_count": len(admins), "group_count": len(groups),
              "administrators": [{"identity": a, "group": g, "immunity": i} for _, a, g, i in admins],
              "legacy_permission_mapping": COMMAND_PERMISSIONS if native else {},
              "converted_settings": converted, "unconverted_settings": extra, "installed": False,
              "policy_changes": ["The root group grants every named permission.",
                                 "Other targets require strictly lower immunity, including zero-immunity non-admins.",
                                 "Self-targeting is allowed when the command permission is granted; console can target anyone.",
                                 "Legacy announcements enabled becomes activity 13; disabled becomes 0. The issuer receives one private confirmation.",
                                 "An empty map allowlist still permits every installed map; names and order are preserved."]}

    if ban_path:
        original_bans, bans = read(ban_path, 16 * 1024 * 1024)

        if not isinstance(bans.get("bans"), list):
            raise ValueError("Expected a bans array")

        files["data/bans.json"] = files["originals/bans.json"] = original_bans
        report.update(source_bans=str(ban_path.resolve()), ban_count=len(bans["bans"]), bans_copied_without_changes=True)

    if extra:
        files["unconverted-settings.json"] = (json.dumps(extra, indent=2, ensure_ascii=False) + "\n").encode()

    report["files_sha256"] = {name: hashlib.sha256(value).hexdigest() for name, value in files.items()}
    files["migration-review.json"] = (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode()
    return files, report


def migrate(admin_path, destination, ban_path=None):
    files, report = prepare(admin_path, ban_path)
    destination.mkdir(mode=0o700, parents=True, exist_ok=False)

    for name, value in files.items():
        path = destination / name
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)

        with path.open("xb") as output:
            output.write(value)

    return report


def main():
    parser = argparse.ArgumentParser(description="Prepare administrator migration files in a new review directory. Inputs are never modified.")
    parser.add_argument("--admins", type=Path, required=True)
    parser.add_argument("--bans", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    try:
        report = migrate(args.admins, args.output, args.bans)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Could not prepare migration: {error}\n")

    print(f"Prepared {report['admin_count']} administrators and {report['group_count']} groups in {args.output}.")

    if report.get("unconverted_settings"):
        print("Review unconverted-settings.json before installation; those settings have not been applied.")

    print("Original files are preserved. No server files were changed.")


if __name__ == "__main__":
    main()

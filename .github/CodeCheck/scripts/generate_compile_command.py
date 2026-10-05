from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / "core"
OUTPUT = ROOT / ".github" / "CodeCheck" / "compile_commands.json"

EXCLUDED_PATHS = {
    Path("core/external"),
    Path("core/p2p/kcp"),
    Path("core/protocol/noise-cpp"),
    Path("legacy"),
    Path("old"),
    Path("third_party"),
    Path("deps"),
}


def is_excluded(path: Path) -> bool:
    relative = path.relative_to(ROOT)

    return any(
        excluded == relative or excluded in relative.parents
        for excluded in EXCLUDED_PATHS
    )


commands = []

for source in sorted(CORE.rglob("*.cpp")):
    if is_excluded(source):
        continue

    relative_source = source.relative_to(ROOT).as_posix()

    commands.append(
        {
            "directory": ".",
            "command": (
                f"g++ -std=c++20 -Icore/ "
                f"-c {relative_source} "
                f"-o {source.stem}.o"
            ),
            "file": relative_source,
        }
    )

OUTPUT.write_text(
    json.dumps(commands, indent=4, ensure_ascii=False) + "\n",
    encoding="utf-8",
)

print(f"Generated {len(commands)} compilation commands.")
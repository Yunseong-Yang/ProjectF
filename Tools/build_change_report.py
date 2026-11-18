"""Create a complete request-scoped unified patch without Git or network access."""
from pathlib import Path
import difflib
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Saved" / "Implementation" / "20261029_FullPlan"
BASELINE = OUTPUT / "baseline"
TRACKED = ("Source", "Config", "Tools")
paths = {Path("ProjectF.uproject"), Path(".gitignore"), Path(".gitattributes"), Path("Docs/AssetApprovals.json"), Path(".agents/ue-project-context.md")}
for relative in TRACKED:
    for tree in (ROOT, BASELINE):
        directory = tree / relative
        if directory.exists():
            paths.update(path.relative_to(tree) for path in directory.rglob("*")
                         if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc")
patch, changes = [], []
for relative in sorted(paths):
    original, current = BASELINE / relative, ROOT / relative
    before = original.read_bytes() if original.exists() else b""
    after = current.read_bytes() if current.exists() else b""
    if before == after:
        continue
    name = relative.as_posix()
    old = before.decode("utf-8-sig").splitlines(keepends=True)
    new = after.decode("utf-8-sig").splitlines(keepends=True)
    diff = list(difflib.unified_diff(old, new, fromfile=f"a/{name}" if original.exists() else "/dev/null",
                                   tofile=f"b/{name}" if current.exists() else "/dev/null"))
    patch.extend(diff)
    changes.append({"path": name, "added": sum(line.startswith("+") and not line.startswith("+++") for line in diff),
                    "removed": sum(line.startswith("-") and not line.startswith("---") for line in diff),
                    "before_sha256": hashlib.sha256(before).hexdigest(), "after_sha256": hashlib.sha256(after).hexdigest()})
OUTPUT.mkdir(parents=True, exist_ok=True)
(OUTPUT / "ProjectF_implementation.patch").write_text("".join(patch), encoding="utf-8", newline="")
(OUTPUT / "code-changes.json").write_text(json.dumps(changes, ensure_ascii=False, indent=2), encoding="utf-8")
summary = ["# 파일별 코드 변경 요약\n", "요청 시작 시점의 원본과 비교한 전체 패치입니다. 새 파일은 빈 파일과 비교했습니다.\n",
           "| 파일 | 추가 | 삭제 |\n|---|---:|---:|\n"]
for change in changes:
    summary.append(f"| {change['path']} | {change['added']} | {change['removed']} |\n")
(OUTPUT / "Code_Diff_Summary.md").write_text("".join(summary), encoding="utf-8")
baseline_content = json.loads((OUTPUT / "content-baseline.json").read_text(encoding="utf-8-sig"))
modified = []
for item in baseline_content:
    path = ROOT / item["Path"]
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest().upper() != item["SHA256"]:
        modified.append(item["Path"])
(OUTPUT / "original-content-verification.json").write_text(json.dumps({
    "original_files": len(baseline_content), "changed_or_missing": modified,
    "new_game_content": [str(path.relative_to(ROOT)) for path in (ROOT / "Content" / "ProjectF").rglob("*") if path.is_file()],
}, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"changed_code_files": len(changes), "patch": str(OUTPUT / "ProjectF_implementation.patch"),
                  "original_content_changed": len(modified)}, ensure_ascii=False))

"""Request-start-relative source patch, preserving the earlier implementation report."""
from pathlib import Path
import difflib
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Saved/Implementation/20261030_Continuation"
BASELINE = OUTPUT / "baseline"
paths = {Path("ProjectF.uproject"), Path("Docs/AssetApprovals.json"), Path(".agents/ue-project-context.md")}
for prefix in ("Source", "Config", "Tools"):
    for tree in (ROOT, BASELINE):
        paths.update(p.relative_to(tree) for p in (tree / prefix).rglob("*") if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc")
patch, changes = [], []
for relative in sorted(paths):
    old, new = BASELINE / relative, ROOT / relative
    before, after = old.read_bytes() if old.exists() else b"", new.read_bytes() if new.exists() else b""
    if before == after: continue
    diff = list(difflib.unified_diff(before.decode("utf-8-sig").splitlines(keepends=True), after.decode("utf-8-sig").splitlines(keepends=True),
                                    fromfile="a/" + relative.as_posix() if old.exists() else "/dev/null", tofile="b/" + relative.as_posix() if new.exists() else "/dev/null"))
    patch.extend(diff)
    changes.append({"file": relative.as_posix(), "added": sum(d.startswith("+") and not d.startswith("+++") for d in diff),
                    "removed": sum(d.startswith("-") and not d.startswith("---") for d in diff),
                    "before_sha256": hashlib.sha256(before).hexdigest(), "after_sha256": hashlib.sha256(after).hexdigest()})
(OUTPUT / "ProjectF_continuation.patch").write_text("".join(patch), encoding="utf-8", newline="")
(OUTPUT / "code-changes.json").write_text(json.dumps(changes, indent=2), encoding="utf-8")
summary = ["# 이번 요청의 파일별 변경\n\n", "| 파일 | 추가 | 삭제 |\n|---|---:|---:|\n"]
summary.extend(f"| {c['file']} | {c['added']} | {c['removed']} |\n" for c in changes)
(OUTPUT / "Code_Diff_Summary.md").write_text("".join(summary), encoding="utf-8")
original = json.loads((ROOT / "Saved/Implementation/20261029_FullPlan/content-baseline.json").read_text(encoding="utf-8-sig"))
changed = [item["Path"] for item in original if not (ROOT / item["Path"]).is_file() or hashlib.sha256((ROOT / item["Path"]).read_bytes()).hexdigest().upper() != item["SHA256"]]
(OUTPUT / "original-content-verification.json").write_text(json.dumps({"original_files": len(original), "changed_or_missing": changed}, indent=2), encoding="utf-8")
print(json.dumps({"changed_code_files": len(changes), "original_content_changed": len(changed), "patch": str(OUTPUT / "ProjectF_continuation.patch")}))

# Binary assets have a separate before/after identity record; they are not text hunks.
binary_extensions = (".uasset", ".umap", ".ubulk", ".uexp", ".uptnl")
asset_before = {Path(item["Path"]).as_posix(): item["SHA256"].lower() for item in original if Path(item["Path"]).suffix in binary_extensions}
for path in (BASELINE / "Content").rglob("*"):
    if path.is_file() and path.suffix in binary_extensions: asset_before[path.relative_to(BASELINE).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
asset_changes, current_assets = [], set()
for path in sorted((ROOT / "Content").rglob("*")):
    if not path.is_file() or path.suffix not in binary_extensions: continue
    name = path.relative_to(ROOT).as_posix(); current_assets.add(name)
    after = hashlib.sha256(path.read_bytes()).hexdigest(); before = asset_before.get(name)
    if after != before: asset_changes.append({"file": name, "before_sha256": before, "after_sha256": after, "bytes": path.stat().st_size})
for name in sorted(set(asset_before) - current_assets):
    asset_changes.append({"file": name, "before_sha256": asset_before[name], "after_sha256": None})
(OUTPUT / "asset-changes.json").write_text(json.dumps(asset_changes, indent=2), encoding="utf-8")
for name in ("ProjectF_Design_and_Development_Plan.md", "Agent_No_Credit_Policy.md"):
    assert (ROOT / "Docs" / name).read_bytes() == (BASELINE / "Docs" / name).read_bytes(), name
print(json.dumps({"changed_binary_assets": len(asset_changes), "design_and_cost_policy_preserved": True}))

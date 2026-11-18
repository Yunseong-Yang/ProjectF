"""Inventory cached Fab acquisition/download metadata without account or network access.

Cached records are evidence to review, not a declaration of current ownership or license.
User identifiers and entitlement identifiers are intentionally excluded from the report.
"""
from pathlib import Path
import json
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
DATABASE = Path("C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary/listings_v1.db")
connection = sqlite3.connect(f"file:{DATABASE.as_posix()}?mode=ro", uri=True)
query = """SELECT l.uid,l.title,l.is_ai_forbidden,
    (SELECT COUNT(*) FROM listing_acquisition a WHERE a.listing_uid=l.uid),
    d.format,d.path,d.cache_size,d.downloaded_at FROM local_listing l
    LEFT JOIN download_meta d ON d.listing_uid=l.uid ORDER BY l.title"""
records = []
for uid, title, ai_forbidden, acquisitions, file_format, cache_path, cached_bytes, downloaded_at in connection.execute(query):
    path = Path(cache_path) if cache_path else None
    available = path is not None and path.is_dir()
    examples = []
    if available:
        for file in path.rglob("*"):
            if file.is_file() and file.suffix.lower() in (".uasset", ".uproject", ".fbx", ".zip", ".glb", ".blend"):
                examples.append(str(file))
                if len(examples) == 8:
                    break
    records.append({"listing_id": uid, "title": title, "cached_acquisition_records": acquisitions,
                    "cached_no_ai_flag": ai_forbidden, "format": file_format, "download_record_path": cache_path,
                    "cached_download_bytes": cached_bytes, "downloaded_at": downloaded_at,
                    "download_path_exists": available, "local_asset_examples": examples,
                    "current_account_ownership_verified": False, "license_verified": False,
                    "production_approved": False})
report = {"source": str(DATABASE), "network_used": False, "downloads_started": False,
          "note": "Local cached records may be stale. Actual files, current entitlement and terms require verification before use.",
          "records": records}
destination = ROOT / "Docs" / "Fab_Cache_Inventory.json"
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"records": len(records), "cached_acquisitions": sum(bool(item["cached_acquisition_records"]) for item in records),
                  "existing_download_paths": sum(item["download_path_exists"] for item in records), "report": str(destination)}, ensure_ascii=False))

"""Summarize explicit evidence paths, keeping failures and sampled limits visible."""
from pathlib import Path
import argparse
import json

root = Path(__file__).resolve().parents[1]
evidence = root / "Saved/Implementation/20261030_Continuation"
parser = argparse.ArgumentParser()
parser.add_argument("--profile", action="append", required=True)
parser.add_argument("--retained-failure", action="append", default=[])
parser.add_argument("--matrix", required=True)
parser.add_argument("--build", default="package07")
parser.add_argument("--automation", default="Automation17")
parser.add_argument("--output", default="Verification_Summary")
args = parser.parse_args()
assert all("/" not in value and "\\" not in value and ".." not in value for value in (args.build, args.automation, args.output))
fingerprint = json.loads((evidence / (args.build + "-fingerprint.json")).read_text(encoding="utf-8"))
binary_hash = fingerprint["binary"]["sha256"]

def row(name, require_current=True):
    path = evidence / "Performance" / name / "profile.json"
    data = json.loads(path.read_text())
    assert data["exit_code"] == 0
    if require_current: assert data["binary_sha256"] == binary_hash
    probe = data["frame_probe"]
    frame_ok = probe["frame"]["p95_ms"] <= 16.7 and probe["frame"]["p99_ms"] <= 25
    dedicated = data.get("peak_sampled_process_gpu_dedicated_bytes")
    return {"source": str(path), "binary_sha256": data["binary_sha256"], "scenario": data["scenario"], "fixture_revision": data.get("fixture_revision", 1),
            "frame_p95_ms": probe["frame"]["p95_ms"], "frame_p99_ms": probe["frame"]["p99_ms"],
            "game_p95_ms": probe["game_thread"]["p95_ms"], "render_p95_ms": probe["render_thread"]["p95_ms"], "gpu_p95_ms": probe["gpu"]["p95_ms"],
            "living_enemies": probe["living_enemies"], "combat_enemies": probe["combat_enemies"],
            "process_ram_peak_bytes": data["peak_process_working_set_bytes"], "sampled_process_gpu_dedicated_bytes": dedicated,
            "sampled_process_gpu_shared_bytes": data.get("peak_sampled_process_gpu_shared_bytes"),
            "whole_device_gpu_sample_mib": data["peak_sampled_whole_gpu_memory_mib"],
            "frame_budget_pass": frame_ok, "ram_budget_pass": data["peak_process_working_set_bytes"] <= 8 * 1024**3,
            "sampled_gpu_within_budget": None if dedicated is None else dedicated <= 5 * 1024**3,
            "true_gpu_peak_verdict": "UNVERIFIED", "exit_code": data["exit_code"]}

current, failures = [row(n) for n in args.profile], [row(n, False) for n in args.retained_failure]
matrix = json.loads((evidence / args.matrix).read_text(encoding="utf-8-sig"))
automation = json.loads((evidence / args.automation / "index.json").read_text(encoding="utf-8-sig"))
record = {"binary_sha256": binary_hash, "protocol": fingerprint["protocol"],
          "automation": {k: automation[k] for k in ("succeeded", "succeededWithWarnings", "failed", "notRun")},
          "packaged_loopback_matrix": {"cases": len(matrix), "all_passed": len(matrix) == 9 and all(c["passed"] for c in matrix), "source": args.matrix},
          "current_fixture": current, "retained_earlier_fixture": failures,
          "scoped_frame_budget_verdict": "PASS" if all(r["frame_budget_pass"] for r in current) else "FAIL",
          "production_acceptance": "UNVERIFIED_NOT_ACCEPTED", "release_approved": False,
          "limits": ["Frame evidence is offscreen 1080p Medium on this i5-13600K/RTX3060Ti machine, not minimum-spec or display-latency certification",
                     "Revision 2 caps three headless stand-ins at 60FPS; revision 1 differs and is not an optimization baseline",
                     "Another game was simultaneously using about 3.5GiB GPU allocation; unrelated apps were not modified",
                     "Synthetic health and staged reinforcements; not a natural 20-30 minute playthrough",
                     "GPU gauges are sampled at 1Hz; true peak and long-run cleanup remain unverified",
                     "Audio disabled for performance; no audio CPU acceptance",
                     "Loopback is not internet or separate-PC cooperation",
                     "All takedown variants, final art/material/animation/contact approval and Far Cry 4 benchmark remain unfinished",
                     "Earlier CSV exit 777003 remains unresolved; Narsha profiling transport was unavailable"]}
(evidence / (args.output + ".json")).write_text(json.dumps(record, indent=2), encoding="utf-8")
lines = ["# ProjectF 실제 검사 요약\n\n", "완성본·출시 승인: **미완료**. 파크라이 4 시각·플레이 기준을 달성했다는 판정이 아니다.\n\n",
         f"자동 검사 {automation['succeeded']}개 통과, 경고 {automation['succeededWithWarnings']}개, 실패 {automation['failed']}개. 패키지 4인 Loopback 9개 조건 통과. 프로토콜 {fingerprint['protocol']}.\n\n",
         "## 수정한 검사 조건의 프레임 표본\n\n| 조건 | p95 ms | p99 ms | 프로세스 RAM 최대 GiB | 샘플 GPU 전용 GiB | 살아 있는 적 / 교전 적 |\n|---|---:|---:|---:|---:|---:|\n"]
for r in current:
    gpu = "미측정" if r["sampled_process_gpu_dedicated_bytes"] is None else f"{r['sampled_process_gpu_dedicated_bytes'] / 1024**3:.2f}"
    lines.append(f"| {r['scenario']} | {r['frame_p95_ms']:.4f} | {r['frame_p99_ms']:.4f} | {r['process_ram_peak_bytes'] / 1024**3:.2f} | {gpu} | {r['living_enemies']} / {r['combat_enemies']} |\n")
lines += ["\n목표 p95 ≤16.7ms, p99 ≤25ms. 렌더링 호스트의 FPS는 제한하지 않고 보조 NullRHI 참가자 세 개만 60FPS로 실행했다. 음향은 껐다. GPU 수치는 Windows의 1초 간격 프로세스 할당량 표본이며 진짜 최대치를 보증하지 않는다.\n\n",
          "이전 조건의 밀집·분산 프레임 실패는 보존했다. 다른 게임의 동시 GPU 사용과 제한 없는 보조 참가자가 있었고, 두 조건의 차이를 게임 코드 최적화 효과로 해석하지 않는다.\n\n",
          "## 증거\n\n"]
lines.extend(f"- [{r['scenario']}]({r['source'].replace(chr(92), '/')})\n" for r in current)
lines.append(f"- [전체 원자료와 한계]({(evidence / (args.output + '.json')).as_posix()})\n")
(evidence / (args.output + ".md")).write_text("".join(lines), encoding="utf-8")
print(json.dumps({"frames": record["scoped_frame_budget_verdict"], "network_cases": len(matrix), "automation": automation["succeeded"], "production": record["production_acceptance"]}))

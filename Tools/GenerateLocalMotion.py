"""Generate one Kimodo sample from installed local weights with network access denied."""
from pathlib import Path
import hashlib
import importlib
import json
import os
import socket
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
INSTALL = Path("C:/AI/Kimodo")
OUTPUT = ROOT / "ArtSource" / "Animations" / "Kimodo" / "WalkTurn_v1"
OUTPUT.mkdir(parents=True, exist_ok=True)
stem = OUTPUT / "AS_Guard_WalkTurn_Source"
if stem.with_suffix(".npz").exists():
    raise SystemExit("An existing motion is preserved; choose a new versioned output directory.")
sys.path.insert(0, str(INSTALL))
from local_models import configure_environment
configure_environment()
os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1",
                  TEXT_ENCODER_MODE="local", TEXT_ENCODER_DEVICE="cpu", LOCAL_CACHE="True",
                  WANDB_MODE="disabled")
blocked = []


def deny_connection(*args, **kwargs):
    blocked.append("network_connection_denied")
    raise RuntimeError("NO_CREDIT: network connections are disabled for local motion generation")


socket.socket.connect = deny_connection
socket.socket.connect_ex = deny_connection
socket.create_connection = deny_connection
import torch
torch.set_num_threads(6)
loader = importlib.import_module("kimodo.model.load_model")


def deny_checkpoint_fallback(*args, **kwargs):
    raise RuntimeError("NO_CREDIT: local checkpoint missing; remote fallback is disabled")


loader._resolve_hf_model_path = deny_checkpoint_fallback
select_local = loader._select_text_encoder_conf


def checked_encoder(*args, **kwargs):
    config = select_local(*args, **kwargs)
    if config.get("_target_") != "kimodo.model.LLM2VecEncoder":
        raise RuntimeError("NO_CREDIT: a non-local text encoder was selected")
    return config


loader._select_text_encoder_conf = checked_encoder
from kimodo.scripts.generate import main
prompt = "A person walks forward at a steady patrol pace, turns ninety degrees to the left, and comes to a controlled stop"
sys.argv = ["kimodo_gen", prompt, "--model", "Kimodo-SOMA-RP-v1.2", "--duration", "4",
            "--num_samples", "1", "--diffusion_steps", "100", "--seed", "42", "--bvh",
            "--bvh_standard_tpose", "--save_example_dir", "--output", str(stem)]
record = {"model": "Kimodo-SOMA-RP-v1.2", "prompt": prompt, "seed": 42, "duration_seconds": 4,
          "diffusion_steps": 100, "text_encoder": "local LLM2Vec on CPU", "motion_device": "local CUDA",
          "network_allowed": False, "remote_fallback_allowed": False, "status": "running"}
record_path = OUTPUT / "generation.json"
record_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
started = time.monotonic()
try:
    main()
    record["status"] = "generated_needs_animation_review"
    record["files"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in OUTPUT.iterdir() if path.is_file() and path != record_path}
except Exception as error:
    record["status"] = "failed_without_remote_fallback"
    record["error"] = f"{type(error).__name__}: {error}"
    raise
finally:
    record["elapsed_seconds"] = time.monotonic() - started
    record["blocked_network_attempts"] = len(blocked)
    record_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
    print("PF_LOCAL_MOTION", json.dumps(record), flush=True)

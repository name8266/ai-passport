"""Not executed in the user's 'do not compile' iteration.

Examples for later offline validation of defaults, filtering and response.
"""
import importlib
import os
from pathlib import Path

def test_defaults_and_off_by_default(monkeypatch, tmp_path: Path):
    from cryptography.fernet import Fernet
    monkeypatch.setenv("HUB_ADMIN_PASSWORD", "testing-super-strong-admin-password")
    monkeypatch.setenv("HUB_DEVICE_TOKEN", "testing-long-random-device-token")
    monkeypatch.setenv("HUB_ENCRYPTION_KEY", Fernet.generate_key().decode())
    monkeypatch.setenv("HUB_DATA_DIR", str(tmp_path))
    service = importlib.import_module("app")
    cfg = service.load_config()
    assert cfg.enabled is False
    assert cfg.model == "deepseek-v4-flash"
    assert cfg.endpoint.startswith("https://api.deepseek.com/")
    assert cfg.interval_minutes == 60
    assert cfg.redact_sensitive is True
    assert cfg.encrypted_api_key == ""

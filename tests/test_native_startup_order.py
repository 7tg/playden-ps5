"""Keep the live PS5 startup wiring fail-closed before any private storage/service."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/homebrew/ps5.cpp").read_text().split("int main() {", 1)[1]
gate = main.index("if (root_access_missing)")
end = main.index("return 0;", gate)
assert "const bool root_access_missing = !native_access.runtime_ready;" in main[:gate]
for operation in ("PrepareGameSettingsStorage(", "Storage auth_storage", "LocalSessionRepository sessions",
                  "InstallService installs", "CloudService cloud", "self_update.Start()", "runtime_host->Start()"):
    assert main.index(operation) > end, operation
startup = (root / "src/platform/ps5/native_startup.cpp").read_text()
assert "if (!moved.runtime_ready)" in startup
failure = startup.split("if (!moved.runtime_ready)", 1)[1].split("}", 1)[0]
assert "result.runtime_ready = false;" in failure

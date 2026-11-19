import pathlib
import subprocess
script = pathlib.Path(__file__).with_name("run.sh")
normalized = script.with_name("run.normalized.sh")
normalized.write_text(script.read_text().replace("\r\n", "\n"), newline="\n")
try:
    raise SystemExit(subprocess.run(["bash", "tests/neural/run.normalized.sh"], cwd=script.parents[2]).returncode)
finally:
    normalized.unlink(missing_ok=True)

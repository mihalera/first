import pathlib
import re
root = pathlib.Path(__file__).resolve().parents[2]
parameter_table = (root / "Source/ParameterTable.cpp").read_text(encoding="utf-8")
processor = (root / "Source/PluginProcessor.cpp").read_text(encoding="utf-8")
registered = set(re.findall(r'ParameterID\s*\{\s*"([^"]+)"', parameter_table))
start = processor.index("static const auto ids")
end = processor.index("return ids;", start)
tracked = set(re.findall(r'"([^"]+)"', processor[start:end]))
missing = sorted(registered - tracked)
unknown = sorted(tracked - registered)
print(f"registered={len(registered)} tracked={len(tracked)}")
if missing:
    print("missing:", ", ".join(missing))
if unknown:
    print("unknown:", ", ".join(unknown))
raise SystemExit(bool(missing or unknown))

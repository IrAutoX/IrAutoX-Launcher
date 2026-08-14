from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")

# Preview fixes are committed directly on the feature branch now.
# Keep this helper idempotent so push/PR builds cannot fail when source blocks evolve.
qrc_path = "resources/resources.qrc"
qrc = read(qrc_path)
if "<file>Vazir.ttf</file>" not in qrc:
    qrc = qrc.replace("    <file>theme.qss</file>\n", "    <file>theme.qss</file>\n    <file>Vazir.ttf</file>\n")
write(qrc_path, qrc)

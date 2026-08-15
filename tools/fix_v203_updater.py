from pathlib import Path

path = Path("src/updater/main_v203.cpp")
text = path.read_text(encoding="utf-8")
text = text.replace("    const QString script = QStringLiteral(\n", "    QString script = QStringLiteral(\n", 1)
path.write_text(text, encoding="utf-8")

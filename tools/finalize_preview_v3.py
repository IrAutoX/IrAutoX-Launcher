from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp_path = ROOT / "src/ui/MainWindow.cpp"
h_path = ROOT / "src/ui/MainWindow.h"
cpp = cpp_path.read_text(encoding="utf-8")
h = h_path.read_text(encoding="utf-8")

# Avoid the C++ most-vexing-parse: QNetworkRequest x(QUrl(...)) can be parsed as a function.
cpp = cpp.replace("QNetworkRequest request(QUrl(value));", "QNetworkRequest request{QUrl(value)};")
cpp = cpp.replace("QNetworkRequest req(QUrl(value));", "QNetworkRequest req{QUrl(value)};")

old = '''void MainWindow::setStartupGameId(qint64 gameId)
{
    m_startupGameId = gameId;
}
'''
new = '''void MainWindow::setStartupGameId(qint64 gameId)
{
    if (gameId <= 0)
        return;
    if (!m_user.isEmpty()) {
        QTimer::singleShot(0, this, [this, gameId] { checkAndLaunch(gameId); });
        return;
    }
    m_startupGameId = gameId;
}
'''
if old in cpp:
    cpp = cpp.replace(old, new, 1)

if "#include <QSize>" not in h:
    h = h.replace("#include <QPoint>\n", "#include <QPoint>\n#include <QSize>\n")

cpp_path.write_text(cpp, encoding="utf-8", newline="\n")
h_path.write_text(h, encoding="utf-8", newline="\n")

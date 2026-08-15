from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "src/ui/MainWindow.cpp"
cpp = path.read_text(encoding="utf-8")

# Generated MainWindow.cpp is committed back to the branch by Actions. On a later
# run apply_v202.py replaces applyGameIcon again, while the previously generated
# applyGameAsset block may still be present before it. Keep only the newest block.
signature = "void MainWindow::applyGameAsset(QLabel *label, const QJsonObject &game, const QSize &size, bool banner, bool circular)"
positions = []
start = 0
while True:
    index = cpp.find(signature, start)
    if index < 0:
        break
    positions.append(index)
    start = index + len(signature)
if len(positions) > 1:
    cpp = cpp[:positions[0]] + cpp[positions[-1]:]

old = '''            if (id > 0)
                m_games.insert(id, game);
'''
new = '''            if (id > 0) {
                m_games.insert(id, game);
                precacheGameAssets(game);
            }
'''
cpp = cpp.replace(old, new)

cpp = cpp.replace(
    '''        renderStore();
        if (!m_startupGameName.isEmpty())''',
    '''        renderStore();
        renderLibrary();
        if (!m_startupGameName.isEmpty())''',
    1,
)

old_banner = '''    QPixmap banner = decodedPixmap(game.value(QStringLiteral("banner")).toString(), QSize(1000, 230), true);
    if (banner.isNull()) {
        m_detailBanner->setPixmap(QPixmap{});
        m_detailBanner->setText(tr("IrAutoX  •  %1").arg(m_detailName->text()));
    } else {
        m_detailBanner->setText(QString{});
        m_detailBanner->setPixmap(banner);
    }
'''
new_banner = '''    m_detailBanner->setText(tr("IrAutoX  •  %1").arg(m_detailName->text()));
    applyGameBanner(m_detailBanner, game, QSize(1000, 230));
'''
cpp = cpp.replace(old_banner, new_banner, 1)

single_game = '''        if (id > 0)
            m_games.insert(id, game);
        updateDetail(game, message.value(QStringLiteral("reviews")).toArray());
'''
single_game_new = '''        if (id > 0) {
            m_games.insert(id, game);
            precacheGameAssets(game);
        }
        updateDetail(game, message.value(QStringLiteral("reviews")).toArray());
'''
cpp = cpp.replace(single_game, single_game_new, 1)

path.write_text(cpp, encoding="utf-8", newline="\n")

#pragma once

#include "core/AppSettings.h"
#include "core/DownloadManager.h"
#include "core/GameLibrary.h"
#include "core/ProtocolClient.h"

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QEvent;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QSystemTrayIcon;
class QVBoxLayout;
class QJsonArray;
class QJsonValue;

namespace irautox {

class LoginDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void requestGameLaunch(qint64 gameId)
    {
        if (gameId > 0)
            m_commandLaunchId = gameId;
        if (m_commandLaunchId <= 0)
            return;
        if (!m_user.isEmpty() && m_client.isConnected()) {
            const qint64 pending = m_commandLaunchId;
            m_commandLaunchId = 0;
            checkAndLaunch(pending);
            return;
        }
        QTimer::singleShot(400, this, [this] { requestGameLaunch(0); });
    }

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onServerMessage(const QJsonObject &message);
    void onConnectionState(bool connected, const QString &detail);
    void onLoginRequested(const QString &username, const QString &password, bool remember);
    void onRegisterRequested(const QString &username, const QString &password, bool remember);
    void onDownloadAdded(const DownloadRequest &request);
    void onDownloadProgress(qint64 gameId, DownloadManager::State state, int percent,
                            qint64 received, qint64 total, double speed, const QString &detail);
    void onDownloadCompleted(const DownloadRequest &request);
    void onDownloadFailed(qint64 gameId, const QString &error);

private:
    enum Page { StorePage, LibraryPage, DownloadsPage, FriendsPage, ProfilePage, SettingsPage, AdminPage, DetailPage };

    void setupUi();
    QWidget *createStorePage();
    QWidget *createLibraryPage();
    QWidget *createDownloadsPage();
    QWidget *createFriendsPage();
    QWidget *createProfilePage();
    QWidget *createSettingsPage();
    QWidget *createAdminPage();
    QWidget *createDetailPage();
    void setupTray();
    void showLogin();
    void setCurrentPage(Page page);
    QPushButton *addNavigationButton(const QString &text, Page page);

    void requestInitialData();
    void renderStore();
    void renderLibrary();
    void renderFriends(const QJsonArray &friends, const QJsonArray &requests);
    void renderReviews(const QJsonArray &reviews);
    void showGameDetails(qint64 gameId);
    void updateDetail(const QJsonObject &game, const QJsonArray &reviews);
    void installCurrentGame(bool update = false);
    void checkAndLaunch(qint64 gameId);
    void launchGame(qint64 gameId);
    void uninstallGame(qint64 gameId);
    void openInstallDirectory(qint64 gameId);
    void saveSettings();
    void applyStartupSetting(bool enabled);
    void logout();
    void uploadAvatar();
    void submitReview();
    void refreshAdminGames(const QJsonArray &games);
    void publishAdminGame();
    void clearAdminForm();
    void writeInstallMarker(const DownloadRequest &request) const;
    bool hasValidInstallMarker(const InstalledGame &game) const;

    static qint64 jsonId(const QJsonValue &value);
    static QString safeFolderName(const QString &name, qint64 id);

    AppSettings m_settings;
    ProtocolClient m_client;
    GameLibrary m_library;
    DownloadManager m_downloadManager;
    LoginDialog *m_loginDialog = nullptr;
    QJsonObject m_user;
    QString m_sessionUsername;
    QString m_sessionPassword;
    bool m_rememberSession = false;
    bool m_quitting = false;

    QHash<qint64, QJsonObject> m_games;
    QJsonObject m_currentGame;
    qint64 m_currentGameId = 0;
    qint64 m_pendingUpdateCheck = 0;
    qint64 m_commandLaunchId = 0;
    QHash<qint64, QFrame *> m_downloadRows;

    QStackedWidget *m_pages = nullptr;
    QButtonGroup *m_navigation = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_announcementLabel = nullptr;
    QFrame *m_announcementBar = nullptr;
    QPushButton *m_profileButton = nullptr;
    QPushButton *m_adminNavButton = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QTimer *m_announcementTimer = nullptr;

    QGridLayout *m_storeGrid = nullptr;
    QVBoxLayout *m_libraryLayout = nullptr;
    QVBoxLayout *m_downloadsLayout = nullptr;
    QVBoxLayout *m_friendsLayout = nullptr;
    QVBoxLayout *m_requestsLayout = nullptr;
    QLabel *m_globalDownloadLabel = nullptr;
    QLabel *m_globalDownloadSpeed = nullptr;
    QProgressBar *m_globalDownloadProgress = nullptr;

    QLabel *m_profileAvatar = nullptr;
    QLabel *m_profileName = nullptr;
    QLabel *m_profileRole = nullptr;
    QLabel *m_profileStats = nullptr;

    QLabel *m_detailBanner = nullptr;
    QLabel *m_detailIcon = nullptr;
    QLabel *m_detailName = nullptr;
    QLabel *m_detailVersion = nullptr;
    QLabel *m_detailDescription = nullptr;
    QPushButton *m_detailAction = nullptr;
    QLineEdit *m_reviewText = nullptr;
    QSpinBox *m_reviewRating = nullptr;
    QVBoxLayout *m_reviewsLayout = nullptr;

    QLineEdit *m_friendUsername = nullptr;
    QLineEdit *m_downloadPath = nullptr;
    QLineEdit *m_serverHost = nullptr;
    QSpinBox *m_serverPort = nullptr;
    QCheckBox *m_minimizeToTray = nullptr;
    QCheckBox *m_closeToTray = nullptr;
    QCheckBox *m_startWithWindows = nullptr;

    QPlainTextEdit *m_adminAnnouncement = nullptr;
    QListWidget *m_adminGames = nullptr;
    QLineEdit *m_adminName = nullptr;
    QPlainTextEdit *m_adminDescription = nullptr;
    QLineEdit *m_adminVersion = nullptr;
    QLineEdit *m_adminUrl = nullptr;
    QLineEdit *m_adminExecutable = nullptr;
    QLineEdit *m_adminSha256 = nullptr;
    QLineEdit *m_adminIconPath = nullptr;
    QLineEdit *m_adminBannerPath = nullptr;
    qint64 m_editingGameId = 0;
};

} // namespace irautox

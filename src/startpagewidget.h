#pragma once
#include <KSharedConfig>
#include <QFrame>
#include <QPersistentModelIndex>
#include <QScrollArea>
#include <QSet>
#include <QUrl>
#include <QMouseEvent>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using EnterEventType = QEnterEvent;
#else
using EnterEventType = QEvent;
#endif

class FlowLayout;
class KFilePlacesModel;
class QCheckBox;
class QLabel;
class QListWidget;
class QToolButton;
class KCapacityBar;

struct TileInfo {
    QString id;          // config key
    QString title;
    QString icon;
    QUrl url;
    QString subtitle;
    qint64 total = -1;   // capacity bar, -1 = none
    qint64 used = 0;
    bool hidden = false; // hidden by the user (only shown in edit mode)
    QPersistentModelIndex placeIndex;
};

class Tile : public QFrame
{
    Q_OBJECT
public:
    Tile(const TileInfo &info, bool editMode, QWidget *parent);
    const TileInfo &info() const { return m_info; }
Q_SIGNALS:
    void activated(bool newTab);
    void toggleHidden();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(EnterEventType *) override;
    void leaveEvent(QEvent *) override;
private:
    TileInfo m_info;
    bool m_edit;
    bool m_hover = false;
};

class StartPageWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit StartPageWidget(QWidget *parent);
    void refresh();
Q_SIGNALS:
    void openUrlRequested(const QUrl &url, bool newTab);
private:
    struct Section {
        QString key;
        QWidget *container = nullptr;
        QCheckBox *enabled = nullptr;
        QLabel *title = nullptr;
        FlowLayout *flow = nullptr;
        QListWidget *list = nullptr;
    };
    Section makeSection(const QString &key, const QString &title, bool isList);
    void setEditMode(bool on);
    void rebuildTiles();
    void fillSection(Section &s, const QList<TileInfo> &tiles, bool addButton);
    QList<TileInfo> localTiles() const;
    QList<TileInfo> placeTiles(bool devices) const;
    void fetchRemoteSizes();
    void fetchFolderSizes();
    void scheduleRebuild();
    QString deviceKey(const QModelIndex &idx) const;
    void loadRecent(QListWidget *list, const QUrl &url, bool files);
    void activateTile(const TileInfo &t, bool newTab);
    bool sectionEnabled(const QString &key) const;
    QStringList hiddenIds(const QString &key) const;
    void toggleHidden(const QString &key, const QString &id);
    void addLocalFolder();
    void updateVisibility();

    KSharedConfigPtr m_config;
    KFilePlacesModel *m_places;
    QToolButton *m_editButton;
    QLabel *m_pinnedLabel;
    QFrame *m_separator;
    QWidget *m_recentRow;
    Section m_local, m_remote, m_devices, m_recentFiles, m_recentLocations;
    bool m_edit = false;
    bool m_rebuildQueued = false;
    QSet<QString> m_sizeJobs;
};

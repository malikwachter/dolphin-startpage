#include "startpagewidget.h"
#include "flowlayout.h"

#include <KCapacityBar>
#include <KConfigGroup>
#include <KFileItem>
#include <KFilePlacesModel>
#include <KIO/DirectorySizeJob>
#include <KIO/FileSystemFreeSpaceJob>
#include <KIO/Global>
#include <KIO/JobUiDelegateFactory>
#include <KIO/ListJob>
#include <KIO/OpenUrlJob>
#include <kio_version.h>
#include <Solid/Block>
#include <Solid/Device>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

static const QString kLocal = QStringLiteral("Local");
static const QString kRemote = QStringLiteral("Remote");
static const QString kDevices = QStringLiteral("Devices");
static const QString kRecentFiles = QStringLiteral("RecentFiles");
static const QString kRecentLocations = QStringLiteral("RecentLocations");
static const int kRecentLimit = 25;

static QString capacityText(qint64 used, qint64 total)
{
    return QStringLiteral("%1 / %2 (%3%)")
        .arg(KIO::convertSize(used), KIO::convertSize(total))
        .arg(total > 0 ? qRound(100.0 * used / total) : 0);
}

// ---------------------------------------------------------------- Tile

Tile::Tile(const TileInfo &info, bool editMode, QWidget *parent)
    : QFrame(parent), m_info(info), m_edit(editMode)
{
    setFixedSize(176, 96);
    setCursor(Qt::PointingHandCursor);
    setToolTip(info.url.toDisplayString(QUrl::PreferLocalFile));
    setAttribute(Qt::WA_Hover);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(4);
    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon::fromTheme(info.icon, QIcon::fromTheme(QStringLiteral("folder"))).pixmap(32, 32));
    top->addWidget(icon);
    auto *title = new QLabel(info.title, this);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    title->setFont(f);
    title->setText(title->fontMetrics().elidedText(info.title, Qt::ElideRight, editMode ? 90 : 118));
    top->addWidget(title, 1);
    if (editMode) {
        auto *btn = new QToolButton(this);
        btn->setAutoRaise(true);
        btn->setText(info.hidden ? QStringLiteral("+") : QStringLiteral("−"));
        btn->setIcon(QIcon::fromTheme(info.hidden ? QStringLiteral("list-add") : QStringLiteral("list-remove")));
        btn->setToolTip(info.hidden ? QStringLiteral("Show on start page") : QStringLiteral("Remove from start page"));
        connect(btn, &QToolButton::clicked, this, &Tile::toggleHidden);
        top->addWidget(btn, 0, Qt::AlignTop);
    }
    v->addLayout(top);
    v->addStretch(1);
    if (!info.subtitle.isEmpty()) {
        auto *sub = new QLabel(info.subtitle, this);
        QFont sf = sub->font();
        sf.setPointSizeF(sf.pointSizeF() * 0.85);
        sub->setFont(sf);
        sub->setWordWrap(true);
        sub->setEnabled(false); // KDE-ish "inactive text" colour
        v->addWidget(sub);
    }
    if (info.total > 0) {
        auto *bar = new KCapacityBar(KCapacityBar::DrawTextInline, this);
        bar->setValue(qRound(100.0 * info.used / info.total));
        bar->setBarHeight(4);
        bar->setFixedHeight(6);
        v->addWidget(bar);
    }
    if (editMode && info.hidden) {
        auto *fx = new QGraphicsOpacityEffect(this);
        fx->setOpacity(0.4);
        setGraphicsEffect(fx);
    }
}

void Tile::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    QColor bg = pal.color(QPalette::Button);
    QColor border = pal.color(QPalette::Mid);
    if (m_hover && !m_info.hidden) {
        bg = QColor::fromRgbF(
            bg.redF() * 0.8 + pal.color(QPalette::Highlight).redF() * 0.2,
            bg.greenF() * 0.8 + pal.color(QPalette::Highlight).greenF() * 0.2,
            bg.blueF() * 0.8 + pal.color(QPalette::Highlight).blueF() * 0.2);
        border = pal.color(QPalette::Highlight);
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    p.fillPath(path, bg);
    p.setPen(QPen(border, 1));
    p.drawPath(path);
}

void Tile::mouseReleaseEvent(QMouseEvent *e)
{
    if (!rect().contains(e->pos())) return;
    if (e->button() == Qt::LeftButton)
        Q_EMIT activated(e->modifiers() & Qt::ControlModifier);
    else if (e->button() == Qt::MiddleButton)
        Q_EMIT activated(true);
}

void Tile::enterEvent(EnterEventType *) { m_hover = true; update(); }
void Tile::leaveEvent(QEvent *) { m_hover = false; update(); }

// ---------------------------------------------------------------- StartPageWidget

StartPageWidget::StartPageWidget(QWidget *parent)
    : QScrollArea(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("dolphinstartpagerc")))
    , m_places(new KFilePlacesModel(this))
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    auto *content = new QWidget;
    content->setBackgroundRole(QPalette::Base);
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(18, 12, 18, 12);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    m_pinnedLabel = new QLabel(QStringLiteral("Pinned"), content);
    QFont hf = m_pinnedLabel->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.4);
    m_pinnedLabel->setFont(hf);
    header->addWidget(m_pinnedLabel);
    header->addStretch(1);
    m_editButton = new QToolButton(content);
    m_editButton->setCheckable(true);
    m_editButton->setAutoRaise(true);
    m_editButton->setText(QStringLiteral("Edit"));
    m_editButton->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
    m_editButton->setToolTip(QStringLiteral("Edit start page"));
    connect(m_editButton, &QToolButton::toggled, this, &StartPageWidget::setEditMode);
    header->addWidget(m_editButton);
    root->addLayout(header);

    m_local = makeSection(kLocal, QStringLiteral("Local"), false);
    m_remote = makeSection(kRemote, QStringLiteral("Remote"), false);
    m_devices = makeSection(kDevices, QStringLiteral("Devices"), false);
    root->addWidget(m_local.container);
    root->addWidget(m_remote.container);
    root->addWidget(m_devices.container);

    m_separator = new QFrame(content);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setFrameShadow(QFrame::Sunken);
    root->addWidget(m_separator);

    m_recentRow = new QWidget(content);
    auto *recent = new QHBoxLayout(m_recentRow);
    recent->setContentsMargins(0, 0, 0, 0);
    m_recentFiles = makeSection(kRecentFiles, QStringLiteral("Recent Files"), true);
    m_recentLocations = makeSection(kRecentLocations, QStringLiteral("Recent Locations"), true);
    recent->addWidget(m_recentFiles.container, 1);
    auto *vline = new QFrame(m_recentRow);
    vline->setFrameShape(QFrame::VLine);
    vline->setFrameShadow(QFrame::Sunken);
    recent->addWidget(vline);
    recent->addWidget(m_recentLocations.container, 1);
    root->addWidget(m_recentRow, 1);

    setWidget(content);

    // Remote places / (removable) devices change dynamically, e.g. USB hotplug
    auto queue = [this] { scheduleRebuild(); };
    connect(m_places, &QAbstractItemModel::rowsInserted, this, queue);
    connect(m_places, &QAbstractItemModel::rowsRemoved, this, queue);
    connect(m_places, &QAbstractItemModel::dataChanged, this, queue);
    connect(m_places, &QAbstractItemModel::modelReset, this, queue);
    connect(m_places, &KFilePlacesModel::setupDone, this, queue);
}

StartPageWidget::Section StartPageWidget::makeSection(const QString &key, const QString &title, bool isList)
{
    Section s;
    s.key = key;
    s.container = new QWidget;
    auto *v = new QVBoxLayout(s.container);
    v->setContentsMargins(isList ? 0 : 24, 0, 0, 0);
    v->setSpacing(6);
    auto *h = new QHBoxLayout;
    s.enabled = new QCheckBox(s.container);
    s.enabled->setToolTip(QStringLiteral("Show this section"));
    s.enabled->setChecked(sectionEnabled(key));
    connect(s.enabled, &QCheckBox::toggled, this, [this, key](bool on) {
        KConfigGroup(m_config, QStringLiteral("Sections")).writeEntry(key, on);
        m_config->sync();
        updateVisibility();
    });
    h->addWidget(s.enabled);
    s.title = new QLabel(title, s.container);
    QFont f = s.title->font();
    f.setPointSizeF(f.pointSizeF() * 1.2);
    s.title->setFont(f);
    h->addWidget(s.title);
    h->addStretch(1);
    v->addLayout(h);
    if (isList) {
        s.list = new QListWidget(s.container);
        s.list->setFrameShape(QFrame::NoFrame);
        s.list->setIconSize(QSize(22, 22));
        s.list->setUniformItemSizes(true);
        s.list->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(s.list, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
            const QUrl url = it->data(Qt::UserRole).toUrl();
            if (it->data(Qt::UserRole + 1).toBool()) {
                Q_EMIT openUrlRequested(url, false);
            } else {
                auto *job = new KIO::OpenUrlJob(url);
                job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
                job->start();
            }
        });
        v->addWidget(s.list, 1);
    } else {
        auto *body = new QWidget(s.container);
        s.flow = new FlowLayout(body, 10);
        v->addWidget(body);
    }
    s.enabled->hide();
    return s;
}

bool StartPageWidget::sectionEnabled(const QString &key) const
{
    return KConfigGroup(m_config, QStringLiteral("Sections")).readEntry(key, true);
}

QStringList StartPageWidget::hiddenIds(const QString &key) const
{
    return KConfigGroup(m_config, key).readEntry("Hidden", QStringList());
}

void StartPageWidget::toggleHidden(const QString &key, const QString &id)
{
    KConfigGroup g(m_config, key);
    QStringList hidden = g.readEntry("Hidden", QStringList());
    if (!hidden.removeAll(id)) hidden.append(id);
    g.writeEntry("Hidden", hidden);
    m_config->sync();
    QTimer::singleShot(0, this, &StartPageWidget::rebuildTiles); // don't delete the sender now
}

void StartPageWidget::setEditMode(bool on)
{
    m_edit = on;
    m_editButton->setIcon(QIcon::fromTheme(on ? QStringLiteral("dialog-ok-apply") : QStringLiteral("document-edit")));
    m_editButton->setToolTip(on ? QStringLiteral("Done") : QStringLiteral("Edit start page"));
    rebuildTiles();
}

void StartPageWidget::refresh()
{
    m_config->reparseConfiguration();
    rebuildTiles();
    fetchRemoteSizes();
    fetchFolderSizes();
    if (sectionEnabled(kRecentFiles)) loadRecent(m_recentFiles.list, QUrl(QStringLiteral("recentlyused:/files")), true);
    if (sectionEnabled(kRecentLocations)) loadRecent(m_recentLocations.list, QUrl(QStringLiteral("recentlyused:/locations")), false);
}

void StartPageWidget::updateVisibility()
{
    Section *all[] = {&m_local, &m_remote, &m_devices, &m_recentFiles, &m_recentLocations};
    for (Section *s : all) {
        const bool on = sectionEnabled(s->key);
        s->enabled->setVisible(m_edit);
        s->container->setVisible(on || m_edit);
        if (s->flow) s->flow->parentWidget()->setEnabled(on);
        if (s->list) s->list->setEnabled(on);
    }
    const bool recentVisible = m_recentFiles.container->isVisible() || m_recentLocations.container->isVisible();
    m_recentRow->setVisible(recentVisible);
    m_separator->setVisible(recentVisible);
    const bool pinnedVisible = m_local.container->isVisible() || m_remote.container->isVisible() || m_devices.container->isVisible();
    m_pinnedLabel->setText(pinnedVisible ? QStringLiteral("Pinned") : QString());
}

QList<TileInfo> StartPageWidget::localTiles() const
{
    const struct { QStandardPaths::StandardLocation loc; const char *icon; } defs[] = {
        {QStandardPaths::HomeLocation, "user-home"},
        {QStandardPaths::DesktopLocation, "user-desktop"},
        {QStandardPaths::DocumentsLocation, "folder-documents"},
        {QStandardPaths::DownloadLocation, "folder-download"},
        {QStandardPaths::MusicLocation, "folder-music"},
        {QStandardPaths::PicturesLocation, "folder-pictures"},
        {QStandardPaths::MoviesLocation, "folder-videos"},
    };
    QList<QPair<QString, QString>> entries; // path, icon
    for (const auto &d : defs) {
        const QString p = QStandardPaths::writableLocation(d.loc);
        if (!p.isEmpty() && QDir(p).exists()) entries.append({p, QString::fromLatin1(d.icon)});
    }
    const QStringList extra = KConfigGroup(m_config, kLocal).readEntry("Extra", QStringList());
    for (const QString &p : extra)
        if (QDir(p).exists()) entries.append({p, QStringLiteral("folder")});

    const QStringList hidden = hiddenIds(kLocal);
    QList<TileInfo> out;
    for (const auto &e : entries) {
        TileInfo t;
        t.id = e.first;
        t.icon = e.second;
        t.url = QUrl::fromLocalFile(e.first);
        t.title = e.first == QDir::homePath() ? QStringLiteral("Home") : QDir(e.first).dirName();
        t.hidden = hidden.contains(t.id);
        const int n = QDir(e.first).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).count();
        const qint64 size = KConfigGroup(m_config, QStringLiteral("FolderSizeCache")).readEntry(t.id, qint64(-1));
        t.subtitle = QStringLiteral("%1 items\n%2").arg(n).arg(size >= 0 ? KIO::convertSize(size) : QStringLiteral("calculating size…"));
        out.append(t);
    }
    return out;
}

QList<TileInfo> StartPageWidget::placeTiles(bool devices) const
{
    const QString key = devices ? kDevices : kRemote;
    const QStringList hidden = hiddenIds(key);
    const KConfigGroup cache(m_config, QStringLiteral("RemoteCache"));
    QList<TileInfo> out;
    QHash<QString, int> seen; // device key -> index in out
    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex idx = m_places->index(row, 0);
        const auto group = m_places->groupType(idx);
        const bool match = devices ? (group == KFilePlacesModel::DevicesType || group == KFilePlacesModel::RemovableDevicesType)
                                   : group == KFilePlacesModel::RemoteType;
        if (!match || m_places->isHidden(idx) || m_places->url(idx).scheme() == QLatin1String("startpage")) continue;

        TileInfo t;
        t.placeIndex = idx;
        t.title = m_places->text(idx);
        t.icon = m_places->icon(idx).name();
        if (t.icon.isEmpty()) t.icon = devices ? QStringLiteral("drive-harddisk") : QStringLiteral("folder-network");
        t.url = m_places->url(idx);
        if (devices) {
            const Solid::Device dev = m_places->deviceForIndex(idx);
            t.id = dev.isValid() ? dev.udi() : t.url.toString();
            if (m_places->setupNeeded(idx)) {
                t.subtitle = QStringLiteral("Not mounted – click to mount");
            } else if (t.url.isLocalFile()) {
                const QStorageInfo si(t.url.toLocalFile());
                if (si.isValid() && si.bytesTotal() > 0) {
                    t.total = si.bytesTotal();
                    t.used = si.bytesTotal() - si.bytesAvailable();
                    t.subtitle = capacityText(t.used, t.total);
                }
            }
            if (t.subtitle.isEmpty() && dev.isValid()) t.subtitle = dev.description();
        } else {
            t.id = t.url.toString();
            const QStringList c = cache.readEntry(t.id, QStringList()); // total, available, epoch
            if (c.size() == 3) {
                t.total = c[0].toLongLong();
                t.used = t.total - c[1].toLongLong();
                t.subtitle = QStringLiteral("%1\nas of %2").arg(capacityText(t.used, t.total),
                    QLocale().toString(QDateTime::fromSecsSinceEpoch(c[2].toLongLong()), QLocale::ShortFormat));
            } else {
                t.subtitle = t.url.scheme() == QLatin1String("remote") ? QString() : QStringLiteral("No data yet");
            }
        }
        t.hidden = hidden.contains(t.id);
        if (devices) {
            const QString key = deviceKey(idx);
            if (!key.isEmpty()) {
                const int prev = seen.value(key, -1);
                if (prev >= 0) {
                    // keep the mounted entry, otherwise the one with the nicer (shorter) name
                    const TileInfo &o = out[prev];
                    const bool oMounted = o.total > 0, tMounted = t.total > 0;
                    if (tMounted > oMounted || (tMounted == oMounted && t.title.size() < o.title.size())) out[prev] = t;
                    continue;
                }
                seen.insert(key, out.size());
            }
        }
        out.append(t);
    }
    return out;
}

QString StartPageWidget::deviceKey(const QModelIndex &idx) const
{
    auto canon = [](const QString &dev) {
        const QString c = QFileInfo(dev).canonicalFilePath();
        return c.isEmpty() ? dev : c;
    };
    const QUrl url = m_places->url(idx);
    if (!m_places->setupNeeded(idx) && url.isLocalFile()) {
        const QStorageInfo si(url.toLocalFile());
        if (si.isValid() && si.device().startsWith("/dev/")) return canon(QString::fromLocal8Bit(si.device()));
    }
    const Solid::Device dev = m_places->deviceForIndex(idx);
    if (const auto *b = dev.as<Solid::Block>()) return canon(b->device());
    return {};
}

void StartPageWidget::fillSection(Section &s, const QList<TileInfo> &tiles, bool addButton)
{
    s.flow->clear();
    const QString key = s.key;
    for (const TileInfo &t : tiles) {
        if (t.hidden && !m_edit) continue;
        auto *tile = new Tile(t, m_edit, s.flow->parentWidget());
        connect(tile, &Tile::activated, this, [this, t](bool newTab) { activateTile(t, newTab); });
        connect(tile, &Tile::toggleHidden, this, [this, key, t] {
            // custom local pins are really removed, built-in ones only hidden
            KConfigGroup g(m_config, kLocal);
            QStringList extra = g.readEntry("Extra", QStringList());
            if (key == kLocal && extra.removeAll(t.id)) {
                g.writeEntry("Extra", extra);
                m_config->sync();
                QTimer::singleShot(0, this, &StartPageWidget::rebuildTiles);
            } else {
                toggleHidden(key, t.id);
            }
        });
        s.flow->addWidget(tile);
    }
    if (addButton && m_edit) {
        TileInfo add;
        add.title = QStringLiteral("Add folder…");
        add.icon = QStringLiteral("folder-new");
        auto *tile = new Tile(add, false, s.flow->parentWidget());
        tile->setToolTip(QStringLiteral("Pin another folder"));
        connect(tile, &Tile::activated, this, &StartPageWidget::addLocalFolder);
        s.flow->addWidget(tile);
    }
    s.flow->parentWidget()->updateGeometry();
}

void StartPageWidget::rebuildTiles()
{
    fillSection(m_local, localTiles(), true);
    fillSection(m_remote, placeTiles(false), false);
    fillSection(m_devices, placeTiles(true), false);
    updateVisibility();
}

void StartPageWidget::addLocalFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Pin folder"), QDir::homePath());
    if (dir.isEmpty()) return;
    KConfigGroup g(m_config, kLocal);
    QStringList extra = g.readEntry("Extra", QStringList());
    if (!extra.contains(dir)) extra.append(dir);
    g.writeEntry("Extra", extra);
    m_config->sync();
    QTimer::singleShot(0, this, &StartPageWidget::rebuildTiles);
}

void StartPageWidget::activateTile(const TileInfo &t, bool newTab)
{
    if (m_edit) return;
    if (t.placeIndex.isValid() && m_places->setupNeeded(t.placeIndex)) {
        const QPersistentModelIndex idx = t.placeIndex;
        auto *ctx = new QObject(this);
        connect(m_places, &KFilePlacesModel::setupDone, ctx, [this, idx, newTab, ctx](const QModelIndex &done, bool ok) {
            if (done != idx) return;
            ctx->deleteLater();
            if (ok) Q_EMIT openUrlRequested(m_places->url(done), newTab);
        });
        m_places->requestSetup(t.placeIndex);
        return;
    }
    if (t.url.isValid()) Q_EMIT openUrlRequested(t.url, newTab);
}

void StartPageWidget::scheduleRebuild()
{
    if (m_rebuildQueued) return;
    m_rebuildQueued = true;
    QTimer::singleShot(150, this, [this] {
        m_rebuildQueued = false;
        if (isVisible()) rebuildTiles();
    });
}

void StartPageWidget::fetchFolderSizes()
{
    if (!sectionEnabled(kLocal)) return;
    for (const TileInfo &t : localTiles()) {
        if (t.hidden || m_sizeJobs.contains(t.id)) continue;
        const QString id = t.id;
        m_sizeJobs.insert(id);
        KIO::DirectorySizeJob *job = KIO::directorySize(t.url);
        job->setUiDelegate(nullptr);
        connect(job, &KJob::result, this, [this, job, id] {
            m_sizeJobs.remove(id);
            if (job->error()) return;
            KConfigGroup(m_config, QStringLiteral("FolderSizeCache")).writeEntry(id, qint64(job->totalSize()));
            m_config->sync();
            scheduleRebuild();
        });
    }
}

void StartPageWidget::fetchRemoteSizes()
{
    if (!sectionEnabled(kRemote)) return;
    for (const TileInfo &t : placeTiles(false)) {
        if (t.hidden || t.url.scheme() == QLatin1String("remote")) continue;
        const QString id = t.id;
        KIO::FileSystemFreeSpaceJob *job = KIO::fileSystemFreeSpace(t.url);
        job->setUiDelegate(nullptr); // never pop up error dialogs from the start page
        auto store = [this, id](qint64 total, qint64 avail) {
            if (total <= 0) return;
            KConfigGroup(m_config, QStringLiteral("RemoteCache"))
                .writeEntry(id, QStringList{QString::number(total), QString::number(avail),
                                            QString::number(QDateTime::currentSecsSinceEpoch())});
            m_config->sync();
            scheduleRebuild();
        };
#if KIO_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(job, &KJob::result, this, [job, store] {
            if (!job->error()) store(job->size(), job->availableSize());
        });
#else
        connect(job, &KIO::FileSystemFreeSpaceJob::result, this,
                [store](KIO::Job *j, KIO::filesize_t size, KIO::filesize_t available) {
                    if (!j->error()) store(size, available);
                });
#endif
    }
}

void StartPageWidget::loadRecent(QListWidget *list, const QUrl &url, bool files)
{
    list->clear();
    KIO::ListJob *job = KIO::listDir(url, KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    auto entries = std::make_shared<KIO::UDSEntryList>();
    connect(job, &KIO::ListJob::entries, this, [entries](KIO::Job *, const KIO::UDSEntryList &e) { entries->append(e); });
    connect(job, &KJob::result, list, [list, url, entries, files] {
        QList<KFileItem> items;
        for (const KIO::UDSEntry &e : std::as_const(*entries)) {
            if (e.stringValue(KIO::UDSEntry::UDS_NAME) == QLatin1String(".")) continue;
            items.append(KFileItem(e, url, false, true));
        }
        auto stamp = [](const KFileItem &i) {
            const QDateTime a = i.time(KFileItem::AccessTime);
            return a.isValid() ? a : i.time(KFileItem::ModificationTime);
        };
        std::sort(items.begin(), items.end(), [&](const KFileItem &a, const KFileItem &b) { return stamp(a) > stamp(b); });
        for (const KFileItem &i : std::as_const(items).mid(0, kRecentLimit)) {
            const QUrl target = i.targetUrl();
            auto *it = new QListWidgetItem(QIcon::fromTheme(i.iconName()), i.text(), list);
            it->setData(Qt::UserRole, target);
            it->setData(Qt::UserRole + 1, !files || i.isDir());
            it->setToolTip(target.toDisplayString(QUrl::PreferLocalFile));
        }
        if (items.isEmpty()) {
            auto *it = new QListWidgetItem(QStringLiteral("Nothing here yet"), list);
            it->setFlags(Qt::NoItemFlags);
        }
    });
}

#include "startpageplugin.h"
#include "startpagewidget.h"

#include <KConfigGroup>
#include <KCoreDirLister>
#include <KFilePlacesModel>
#include <KSharedConfig>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QVBoxLayout>

static const char *kAttachedProp = "_startpage_attached";
static const QUrl kStartUrl(QStringLiteral("startpage:/"));

StartPagePlugin::StartPagePlugin(QObject *parent) : KOverlayIconPlugin(parent)
{
    if (QCoreApplication::applicationName() != QLatin1String("dolphin")) return;
    qApp->installEventFilter(this);
    const auto widgets = QApplication::allWidgets();
    for (QWidget *w : widgets)
        if (w->inherits("DolphinView")) attach(w);
    QTimer::singleShot(0, this, &StartPagePlugin::firstRunSetup);
}

bool StartPagePlugin::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Polish && watched->isWidgetType() && watched->inherits("DolphinView"))
        attach(static_cast<QWidget *>(watched));
    return false;
}

void StartPagePlugin::attach(QWidget *view)
{
    if (view->property(kAttachedProp).toBool()) return;
    view->setProperty(kAttachedProp, true);
    new ViewController(view);
}

void StartPagePlugin::firstRunSetup()
{
    KSharedConfigPtr cfg = KSharedConfig::openConfig(QStringLiteral("dolphinstartpagerc"));
    KConfigGroup g(cfg, QStringLiteral("General"));

    // v0.1 added a regular "Startpage" place; it now lives above "Places" instead.
    if (!g.readEntry("PlaceRemoved", false)) {
        KFilePlacesModel model;
        for (int row = model.rowCount() - 1; row >= 0; --row) {
            const QModelIndex idx = model.index(row, 0);
            if (model.url(idx).scheme() == QLatin1String("startpage")) model.removePlace(idx);
        }
        g.writeEntry("PlaceRemoved", true);
    }

    // Make startpage:/ what Dolphin opens when started without a URL.
    // Done once, so the user can change it later in Dolphin's settings.
    if (!g.readEntry("HomeConfigured", false)) {
        KConfigGroup dg(KSharedConfig::openConfig(QStringLiteral("dolphinrc")), QStringLiteral("General"));
        dg.writeEntry("HomeUrl", kStartUrl.toString());
        dg.writeEntry("RememberOpenedTabs", false);
        dg.sync();
        g.writeEntry("HomeConfigured", true);
    }
    g.sync();
}

// ---------------------------------------------------------------------------

ViewController::ViewController(QWidget *view) : QObject(view), m_view(view)
{
    view->installEventFilter(this);
    // DolphinView::urlChanged(QUrl) – string based connect, no Dolphin headers needed
    connect(view, SIGNAL(urlChanged(QUrl)), this, SLOT(onUrlChanged(QUrl)));
    // Initial URL (and robustness): the view's KFileItemModel owns a KCoreDirLister.
    QTimer::singleShot(0, this, [this] {
        StartpagePlaceRow::installInto(m_view->window());
        const auto listers = m_view->findChildren<KCoreDirLister *>();
        for (KCoreDirLister *l : listers) {
            connect(l, &KCoreDirLister::started, this, [this, l] { onUrlChanged(l->url()); });
            onUrlChanged(l->url());
        }
    });
    // the places dock may be created after the first view
    QTimer::singleShot(1000, this, [this] { StartpagePlaceRow::installInto(m_view->window()); });
}

bool ViewController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        updateGeometry();
    return false;
}

void ViewController::updateGeometry()
{
    if (m_page && m_page->isVisible()) {
        m_page->setGeometry(m_view->rect());
        m_page->raise();
    }
}

void ViewController::onUrlChanged(const QUrl &url)
{
    const bool active = url.scheme() == QLatin1String("startpage");
    StartpagePlaceRow::setActive(m_view->window(), active);
    if (!active) {
        if (m_page) m_page->hide();
        return;
    }
    if (!m_page) {
        m_page = new StartPageWidget(m_view);
        connect(m_page, &StartPageWidget::openUrlRequested, this, &ViewController::openUrl);
    }
    if (!m_page->isVisible()) {
        m_page->show();
        m_page->refresh();
    }
    updateGeometry();
}

void ViewController::openUrl(const QUrl &url, bool newTab)
{
    if (newTab) {
        // DolphinMainWindow::openNewTab(QUrl) is a public slot
        if (QWidget *win = m_view->window())
            if (QMetaObject::invokeMethod(win, "openNewTab", Q_ARG(QUrl, url))) return;
    }
    // DolphinViewContainer::setUrl(QUrl) is a public slot
    for (QWidget *w = m_view->parentWidget(); w; w = w->parentWidget()) {
        if (w->inherits("DolphinViewContainer")) {
            if (QMetaObject::invokeMethod(w, "setUrl", Q_ARG(QUrl, url))) return;
            break;
        }
    }
    qWarning("dolphin-startpage: could not navigate, DolphinViewContainer::setUrl not found");
}

// ---------------------------------------------------------------------------

StartpagePlaceRow::StartpagePlaceRow(QWidget *placesPanel, QWidget *parent)
    : QWidget(parent), m_panel(placesPanel)
{
    setObjectName(QStringLiteral("startpagePlaceRow"));
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(kStartUrl.toString());
}

void StartpagePlaceRow::installInto(QWidget *window)
{
    if (!window || window->findChild<StartpagePlaceRow *>()) return;
    auto *dock = window->findChild<QDockWidget *>(QStringLiteral("placesDock"));
    if (!dock || !dock->widget()) return;
    QWidget *panel = dock->widget();

    auto *box = new QWidget;
    box->setBackgroundRole(panel->backgroundRole());
    box->setAutoFillBackground(true);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(new StartpagePlaceRow(panel, box));
    dock->setWidget(box); // the old content widget is hidden and released, not deleted
    lay->addWidget(panel, 1);
    panel->show();
}

void StartpagePlaceRow::setActive(QWidget *window, bool active)
{
    if (!window) return;
    if (auto *row = window->findChild<StartpagePlaceRow *>()) {
        row->m_active = active;
        row->update();
    }
}

QSize StartpagePlaceRow::sizeHint() const
{
    QSize icon = m_panel ? m_panel->property("iconSize").toSize() : QSize();
    if (!icon.isValid() || icon.isEmpty()) icon = QSize(16, 16);
    return QSize(100, qMax(icon.height(), fontMetrics().height()) + 10);
}

void StartpagePlaceRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QStyleOptionViewItem opt;
    opt.initFrom(this);
    opt.rect = rect().adjusted(2, 1, -2, -1);
    opt.features = QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
    opt.text = QStringLiteral("Startpage");
    opt.icon = QIcon::fromTheme(QStringLiteral("view-grid"),
                                QIcon::fromTheme(QStringLiteral("view-list-icons"), QIcon::fromTheme(QStringLiteral("go-home"))));
    QSize icon = m_panel ? m_panel->property("iconSize").toSize() : QSize();
    opt.decorationSize = (icon.isValid() && !icon.isEmpty()) ? icon : QSize(16, 16);
    opt.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    opt.decorationAlignment = Qt::AlignCenter; // default is top-aligned
    opt.viewItemPosition = QStyleOptionViewItem::OnlyOne;
    opt.showDecorationSelected = true;
    opt.state &= ~(QStyle::State_HasFocus | QStyle::State_MouseOver);
    if (m_hover) opt.state |= QStyle::State_MouseOver;
    if (m_active) opt.state |= QStyle::State_Selected;
    style()->drawControl(QStyle::CE_ItemViewItem, &opt, &p, m_panel ? m_panel.data() : this);
}

void StartpagePlaceRow::mouseReleaseEvent(QMouseEvent *e)
{
    if (!rect().contains(e->pos())) return;
    const bool newTab = e->button() == Qt::MiddleButton || (e->modifiers() & Qt::ControlModifier);
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton) return;
    // Emit the places panel's own signals, Dolphin handles them like any place click.
    if (m_panel && QMetaObject::invokeMethod(m_panel, newTab ? "tabRequested" : "placeActivated", Q_ARG(QUrl, kStartUrl)))
        return;
    if (QMetaObject::invokeMethod(window(), "slotPlaceActivated", Q_ARG(QUrl, kStartUrl))) return;
    qWarning("dolphin-startpage: could not open startpage:/ from places row");
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void StartpagePlaceRow::enterEvent(QEnterEvent *) { m_hover = true; update(); }
#else
void StartpagePlaceRow::enterEvent(QEvent *) { m_hover = true; update(); }
#endif
void StartpagePlaceRow::leaveEvent(QEvent *) { m_hover = false; update(); }

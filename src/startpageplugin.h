#pragma once
#include <KOverlayIconPlugin>
#include <QPointer>
#include <QUrl>
#include <QWidget>
#include <QMouseEvent>

class StartPageWidget;

// Dolphin loads every KOverlayIconPlugin into its own process on startup.
// We use that as the hook: never return overlays, but attach the start page
// UI on top of every DolphinView whose URL is startpage:/.
class StartPagePlugin : public KOverlayIconPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.dolphin.startpage" FILE "startpage-overlay.json")
public:
    explicit StartPagePlugin(QObject *parent = nullptr);
    QStringList getOverlays(const QUrl &) override { return {}; }
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void attach(QWidget *dolphinView);
    void firstRunSetup();
};

// One per DolphinView: tracks its URL and shows/hides the page.
class ViewController : public QObject
{
    Q_OBJECT
public:
    explicit ViewController(QWidget *view);
    bool eventFilter(QObject *watched, QEvent *event) override;
public Q_SLOTS:
    void onUrlChanged(const QUrl &url);
private:
    void openUrl(const QUrl &url, bool newTab);
    void updateGeometry();
    QWidget *m_view;
    QPointer<StartPageWidget> m_page;
};

// "Startpage" row drawn like a places entry, placed above the "Places" group
// of the places panel (inside the panel's dock widget).
class StartpagePlaceRow : public QWidget
{
    Q_OBJECT
public:
    StartpagePlaceRow(QWidget *placesPanel, QWidget *parent);
    static void installInto(QWidget *window);
    static void setActive(QWidget *window, bool active);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *) override;
#else
    void enterEvent(QEvent *) override;
#endif
    void leaveEvent(QEvent *) override;
private:
    QPointer<QWidget> m_panel;
    bool m_hover = false;
    bool m_active = false;
};

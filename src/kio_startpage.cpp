// KIO worker for startpage:/ — the rich UI is drawn by the in-process plugin;
// this worker makes the URL valid and lists the pinned folders as a fallback.
#include <KIO/WorkerBase>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <sys/stat.h>

class StartpageWorker : public KIO::WorkerBase
{
public:
    StartpageWorker(const QByteArray &pool, const QByteArray &app)
        : KIO::WorkerBase(QByteArrayLiteral("startpage"), pool, app) {}

    KIO::WorkerResult stat(const QUrl &) override
    {
        KIO::UDSEntry e;
        e.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
        e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, QStringLiteral("Startpage"));
        e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("view-grid"));
        e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0500);
        statEntry(e);
        return KIO::WorkerResult::pass();
    }

    KIO::WorkerResult listDir(const QUrl &) override
    {
        const struct { QStandardPaths::StandardLocation loc; const char *icon; } locs[] = {
            {QStandardPaths::HomeLocation, "user-home"},
            {QStandardPaths::DesktopLocation, "user-desktop"},
            {QStandardPaths::DocumentsLocation, "folder-documents"},
            {QStandardPaths::DownloadLocation, "folder-download"},
            {QStandardPaths::MusicLocation, "folder-music"},
            {QStandardPaths::PicturesLocation, "folder-pictures"},
            {QStandardPaths::MoviesLocation, "folder-videos"},
        };
        KIO::UDSEntryList list;
        for (const auto &l : locs) {
            const QString path = QStandardPaths::writableLocation(l.loc);
            if (path.isEmpty() || !QDir(path).exists()) continue;
            KIO::UDSEntry e;
            e.fastInsert(KIO::UDSEntry::UDS_NAME, QString::fromLatin1(l.icon));
            e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, QStandardPaths::displayName(l.loc));
            e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QString::fromLatin1(l.icon));
            e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, QUrl::fromLocalFile(path).toString());
            e.fastInsert(KIO::UDSEntry::UDS_LOCAL_PATH, path);
            e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0500);
            list.append(e);
        }
        listEntries(list);
        return KIO::WorkerResult::pass();
    }
};

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.startpage" FILE "kio_startpage.json")
};

extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_startpage"));
    if (argc != 4) return -1;
    StartpageWorker w(argv[2], argv[3]);
    w.dispatchLoop();
    return 0;
}

#include "kio_startpage.moc"

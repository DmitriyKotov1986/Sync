#ifndef TSYNC_H
#define TSYNC_H

#include <QObject>
#include <QSettings>
#include <QtSql/QSqlDatabase>
#include <QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QMap>
#include <QQueue>
#include <QFileSystemWatcher>
#include <QSet>
#include <QPair>
#include <QFileInfo>
#include "tconsole.h"
#include "thttpquery.h"

class TSync : public QObject
{
    Q_OBJECT
 public:
    typedef enum {CODE_OK, CODE_ERROR, CODE_INFORMATION} MSG_CODE; //коды ошибок

 private:
    typedef struct {
        QString Url;
        QString AZSCode;
        quint64 LastFileID;
        quint64 LastDownloadID = 0;
        quint64 DeleteFileID = 0; //ID файла отправляемого в последнем пакете
    } THTTPServerInfo;

    typedef struct {
        QString Category;
        QString Path;
        QString FileName;
        QDateTime CreateDateTime;
        QDateTime ChangeDateTime;
        QByteArray Body;
    } TFileInfo;

    typedef enum {NO_CHANGE, LOAD_FROM_SERVER, CHANGE_FILE, CHANGE_DIR} TTypeChange;

    typedef QPair<QString, QDateTime> TFile;

    typedef QSet<TFile> TOldFile;

    typedef struct {
     //   quint16 Index;
        QString Category; //категория
        TTypeChange isChange ;
        TOldFile OldFiles; //множество уже обработанных файлов
        quint64 LastID;
    } TTargetInfo;

public:
    explicit TSync(const QString &ConfigFileName, QObject *parent = nullptr);
    ~TSync();

private:
    TConsole Console;
    QSettings *Config;
    QSqlDatabase DB;
    QTimer UpdateTimer;
    THTTPQuery *HTTPQuery;
    THTTPServerInfo HTTPServerInfo;
    bool Transfering = false; //Флаг текущей передачи данных

    QMap<QString, QString> CategoryToTarget;
    QMap<QString, TTargetInfo> Targets; //карта целей для отслеживания. Ключ - цель отслеживания

    QFileSystemWatcher *FileSystemWatcher;

    QMap<quint64, QString> FileForDownload;

    bool DebugMode = false;
    QTime Timer = QTime::currentTime();

    void SendLogMsg(uint16_t Category, const QString &Msg);
    void SendToHTTPServer();
    void GetOldFileName();
    void AddFileToDB(const QFileInfo& FileInfo, const QString& Category);
    qint64 FindHash(const QString& HASH);
    bool SaveFile(const QString& Category, const QString& FileName, quint64 ID, const QByteArray& Body);

    QDateTime TimeAccuracy(const QDateTime &DateTime);
    void RunCMD(const QString& FileName);

signals:
    void Finished();
    void GetDataComplite();

public slots:
    void onStart();

private slots:
    void onGetCommand(const QString &cmd);
    void onHTTPGetAnswer(const QByteArray &Answer);
    void onStartGetData();
    void onSendLogMsg(uint16_t Category, const QString &Msg);
    void onDirectoryChanged(const QString &path);
    void onFileChanged(const QString &path);
    void onHTTPError();

};

#endif // TSYNC_H

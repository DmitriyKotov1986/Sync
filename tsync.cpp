#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QXmlStreamWriter>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include "tsync.h"

TSync::TSync(const QString &ConfigFileName, QObject *parent)
    : QObject(parent)
{
    Config = new QSettings(ConfigFileName, QSettings::IniFormat);
    if (Config->status() != QSettings::NoError) {
        qCritical() << "Cannot read file of configuration.";
        exit(-1);
    }

    Config->beginGroup("DATABASE");
    DB = QSqlDatabase::addDatabase(Config->value("Driver", "QODBC").toString(), "MainDB");
    DB.setDatabaseName(Config->value("DataBase", "SystemMonitorDB").toString());
    DB.setUserName(Config->value("UID", "SYSDBA").toString());
    DB.setPassword(Config->value("PWD", "MASTERKEY").toString());
    DB.setConnectOptions(Config->value("ConnectionOprions", "").toString());
    DB.setPort(Config->value("Port", "3051").toUInt());
    DB.setHostName(Config->value("Host", "localhost").toString());
    Config->endGroup();

    Config->beginGroup("SYSTEM");
    UpdateTimer.setInterval(Config->value("Interval", "60000").toInt());
    UpdateTimer.setSingleShot(false);
    DebugMode = Config->value("Debug", "1").toBool();

    Config->endGroup();
    QObject::connect(&UpdateTimer, SIGNAL(timeout()), this, SLOT(onStartGetData()));

    Config->beginGroup("SERVER");
    HTTPServerInfo.AZSCode = Config->value("UID", "000").toString();
    HTTPServerInfo.Url = "http://" + Config->value("Host", "localhost").toString() + ":" + Config->value("Port", "80").toString() +
                  "/CGI/SYNC&" + HTTPServerInfo.AZSCode +"&" + Config->value("PWD", "123456").toString();
    HTTPServerInfo.LastFileID = Config->value("LastFileID", "0").toULongLong();
    HTTPServerInfo.DeleteFileID = HTTPServerInfo.LastFileID;
    HTTPServerInfo.LastDownloadID = Config->value("LastDownloadID", "0").toULongLong();
    Config->endGroup();

    HTTPQuery = new THTTPQuery(HTTPServerInfo.Url, this);
    QObject::connect(HTTPQuery, SIGNAL(GetAnswer(const QByteArray &)), this, SLOT(onHTTPGetAnswer(const QByteArray &)));
    QObject::connect(HTTPQuery, SIGNAL(SendLogMsg(uint16_t, const QString &)), this, SLOT(onSendLogMsg(uint16_t, const QString &)));
    QObject::connect(HTTPQuery, SIGNAL(ErrorOccurred()), this, SLOT(onHTTPError()));

    FileSystemWatcher = new QFileSystemWatcher(this);
    QObject::connect(FileSystemWatcher, SIGNAL(directoryChanged(const QString &)), this, SLOT(onDirectoryChanged(const QString &)));
    QObject::connect(FileSystemWatcher, SIGNAL(fileChanged(const QString &)), this, SLOT(onFileChanged(const QString &)));
}

TSync::~TSync()
{
    Config->deleteLater();
    HTTPQuery->deleteLater();
    FileSystemWatcher->deleteLater();

    SendLogMsg(MSG_CODE::CODE_OK, "Successfully finished");
    DB.close();
}

void TSync::onStart()
{
    if (!DB.open()) {
        qCritical() << "Cannot connect to database. Error: " << DB.lastError().text();
        exit(-1);
    };

     //считываем количество целей для синхронизации
    Config->beginGroup("SYNCTARGETS");
    uint Count = Config->value("Count", "0").toUInt();
    Config->endGroup();
    //загружаем цели

    for (quint16 i = 0; i < Count; ++i) {
        Config->beginGroup("TARGET" + QString::number(i));
        QString TargetName = Config->value("Target", "C://").toString();
        TTargetInfo tmp;
        //сразу выставляем значение как будто цели изменились, чтобы при запуске свериться в БД
        if (Config->value("LoadingFromServer", "true").toBool()) { //Загружается с сервера
            tmp.isChange = TTypeChange::LOAD_FROM_SERVER;
        }
        else { //отслеживаються локальные объекты
            if ((TargetName.right(1) == "/") || (TargetName.right(1) == "\\")) tmp.isChange = TTypeChange::CHANGE_DIR;
            else tmp.isChange = TTypeChange::CHANGE_FILE;
            if (!FileSystemWatcher->addPath(TargetName)){
                SendLogMsg(MSG_CODE::CODE_INFORMATION, "Tracking target limit reached. Target " + TargetName  + "will be ignored");
            }
        }
        tmp.Category = Config->value("Category", "n/a").toString();
        tmp.LastID = Config->value("LastID", "0").toULongLong();
        tmp.clearDirAfterSync = Config->value("ClearDirAfterSync", false).toBool();
        tmp.ignoreEmptyFile = Config->value("IgnoreEmptyFile", false).toBool();
        Targets.insert(TargetName, tmp);
        CategoryToTarget.insert(tmp.Category, TargetName);
        Config->endGroup();
        if (DebugMode) {
            qDebug() << "Add target for monitoring: " << TargetName << tmp.LastID << tmp.ignoreEmptyFile <<  tmp.clearDirAfterSync;
        }
    }

    GetOldFileName(); //загружаем имена файлов которые уже изменялись

    SendLogMsg(MSG_CODE::CODE_OK, "Successfully started");

    UpdateTimer.start(); //запускаем таймер обновления данных

    onStartGetData();
}

void TSync::SendLogMsg(uint16_t Category, const QString &Msg)
{
    //Str.replace(QRegularExpression("'"), "''");
    if (DebugMode) {
        qDebug() << Msg;
    }
    QSqlQuery QueryLog(DB);
    DB.transaction();
    QString QueryText = "INSERT INTO LOG (CATEGORY, SENDER, MSG) VALUES ( "
                        + QString::number(Category) + ", "
                        "\'Sync\', "
                        "\'" + Msg +"\'"
                        ")";

    if (!QueryLog.exec(QueryText)) {
        qDebug() << "FAIL Cannot execute query. Error: " << QueryLog.lastError().text() << " Query: "<< QueryLog.lastQuery();
        DB.rollback();
        return;
    }
    if (!DB.commit()) {
        qDebug() << "FAIL Cannot commit transation. Error: " << DB.lastError().text();
        DB.rollback();
        return;
    };
}

void TSync::SendToHTTPServer()
{
    //форматируем XML документ
    QString XMLStr;
    QXmlStreamWriter XMLWriter(&XMLStr);
    XMLWriter.setAutoFormatting(true);
    XMLWriter.writeStartDocument("1.0");
    XMLWriter.writeStartElement("Root");
    XMLWriter.writeTextElement("AZSCode", HTTPServerInfo.AZSCode);
    XMLWriter.writeTextElement("ClientVersion", QCoreApplication::applicationVersion());
    XMLWriter.writeTextElement("ProtocolVersion", "0.1");
    if (DebugMode) {
        qDebug() << "Starting the formation of a request to the server. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
        qDebug() << "->Last download ID:" << HTTPServerInfo.LastDownloadID;
    }
    XMLWriter.writeTextElement("LastID", QString::number(HTTPServerInfo.LastDownloadID));
    //список категорий запрашиваемых с сервера
    for (const auto &Item : Targets) {
        if (Item.isChange == TTypeChange::LOAD_FROM_SERVER) {
            XMLWriter.writeStartElement("QueryCategory");
            XMLWriter.writeTextElement("Category", Item.Category);
            XMLWriter.writeEndElement();
        }
    }

    //запрашиваем первый файл из списка доступных
    if (!FileForDownload.isEmpty()) {
        if (DebugMode) {
            qDebug() << "->Request file: " << FileForDownload.first();
        }
        XMLWriter.writeStartElement("FilesForLoad");
        XMLWriter.writeTextElement("HASH", FileForDownload.first());
        XMLWriter.writeEndElement(); //FilesForLoad
    }
    //Если доступных для скачивания файлов нет, отправляем первый доступный свой
    else {
        QSqlQuery Query(DB);
        DB.transaction();

        //выбираем 1 файл
        QString QueryText = "SELECT ID, CATEGORY, FILE_NAME, CREATE_DATE_TIME, CHANGE_DATE_TIME, BODY "
                            "FROM SYNCFILE "
                            "WHERE ID = (SELECT MIN(ID) FROM SYNCFILE WHERE ID > " + QString::number(HTTPServerInfo.LastFileID) + " ) ";

        if (!Query.exec(QueryText)) {
            DB.rollback();
            qDebug() << "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
            exit(-2);
        }

        //отправляемые на сервер первый неотправленный файл
        if (Query.next()) {
            if (DebugMode) {
                qDebug() << "->Send file: " << Query.value("FILE_NAME").toString() << " to server";
            }
            XMLWriter.writeStartElement("FilesFromClient");
            XMLWriter.writeStartElement("File");
            XMLWriter.writeTextElement("Category", Query.value("CATEGORY").toString());
            //выделяем только имя файла
            QFileInfo tmp(Query.value("FILE_NAME").toString());
            XMLWriter.writeTextElement("FileName", tmp.fileName());
            XMLWriter.writeTextElement("CreateDateTime", Query.value("CREATE_DATE_TIME").toDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
            XMLWriter.writeTextElement("ChangeDateTime", Query.value("CHANGE_DATE_TIME").toDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
            XMLWriter.writeTextElement("Body", Query.value("BODY").toString());
            XMLWriter.writeEndElement(); //File
            XMLWriter.writeEndElement(); //FilesFromClient
            HTTPServerInfo.DeleteFileID = Query.value("ID").toLongLong();
        }
        else {
            if (DebugMode) {
                qDebug() << "->No files to send to server";
            }
        }

        if (!DB.commit()) {
           DB.rollback();
           QString LastError = "Cannot commit transation. Error: " + DB.lastError().text();
           qDebug() << LastError;
           exit(-4);
        };
    }

    XMLWriter.writeEndElement(); //root
    XMLWriter.writeEndDocument();

   /* QFile f("SendToServer.xml");
    f.open(QIODevice::WriteOnly);
    f.write(XMLStr.toUtf8());
    f.close();*/

    //отправляем запрос
    if (DebugMode) {
        qDebug() << "Sending a request. Size: " << XMLStr.toUtf8().size() << "Byte. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
    }
    HTTPQuery->Run(XMLStr.toUtf8());
}

void TSync::GetOldFileName()
{
    //qDebug() << "GetOldFileName. TargetSize:" << Targets.size();
    QSqlQuery Query(DB);
    DB.transaction();

    QStringList CategoryList;
    for (auto &CategoryItem : CategoryToTarget.keys()) CategoryList.push_back("'" + CategoryItem + "'");

    QString QueryText = "SELECT ID, CATEGORY, FILE_NAME, CHANGE_DATE_TIME "
                        "FROM SYNCFILE "
                        "WHERE CATEGORY IN (" + CategoryList.join(",") + ") AND (NOT BODY LIKE '%*DELETED%')" ;

    if (!Query.exec(QueryText)) {
        qDebug() << "FAIL Cannot execute query. Error: " << Query.lastError().text() << " Query: "<< Query.lastQuery();
        DB.rollback();
        exit(-2);
    }

    while (Query.next()) {
        Targets[CategoryToTarget[Query.value("CATEGORY").toString()]].OldFiles.insert(qMakePair(Query.value("FILE_NAME").toString(),
            TimeAccuracy(Query.value("CHANGE_DATE_TIME").toDateTime())));
    }

    if (!DB.commit()) {
        qDebug() << "FAIL Cannot commit transation. Error: " << DB.lastError().text();
        DB.rollback();
        exit(-4);
    };
}

void TSync::onStartGetData()
{
    if (DebugMode) {
        qDebug() << "Start of a data exchange cycle. Time: 0 ms";
        Timer = QTime::currentTime();
    }
    for (const auto& TargetName: Targets.keys()) {
        TTargetInfo& CurrentTargetInfo = Targets[TargetName];

        if (CurrentTargetInfo.isChange == NO_CHANGE) continue;
        else if (CurrentTargetInfo.isChange == LOAD_FROM_SERVER) continue;
        //Изменился файл
        else if (CurrentTargetInfo.isChange == CHANGE_FILE) {
            QFileInfo FileInfo(TargetName); //получаем информацию о файле
            if (CurrentTargetInfo.OldFiles.find(qMakePair(TargetName, FileInfo.fileTime(QFile::FileModificationTime))) ==
                CurrentTargetInfo.OldFiles.end()) {
                //если файл отличаеться - ставим его в очередь на загрузку
                AddFileToDB(FileInfo, CurrentTargetInfo.Category);
            }
        }
        //Изменилась директория

        else if (CurrentTargetInfo.isChange == CHANGE_DIR) {
            //получаем список файлов в директории
            QDir Dir(TargetName);

            if (!Dir.isEmpty(QDir::NoDotAndDotDot)) {
                Dir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
                Dir.setSorting(QDir::Time | QDir::Reversed);

                TOldFile CurrentFileOnDir;
                for (const auto &Item : Dir.entryInfoList()) {
                     CurrentFileOnDir.insert(qMakePair(Item.absoluteFilePath(), TimeAccuracy(Item.fileTime(QFile::FileModificationTime))));
                }

                //все файлы которые поменялись в директории загружаем в БД
                TOldFile tmp = CurrentFileOnDir.subtract(CurrentTargetInfo.OldFiles);
                for (const auto& Item : tmp) {
                    QFileInfo FileInfo(Item.first); //получаем информацию о файле
                    qDebug() << "file:" << FileInfo.absoluteFilePath();
                    if (!CurrentTargetInfo.ignoreEmptyFile || (FileInfo.size() != 0)) {
                        AddFileToDB(FileInfo, CurrentTargetInfo.Category);
                        //добавляем файл в очередь для загрузки
                        CurrentTargetInfo.OldFiles.insert(qMakePair(FileInfo.absoluteFilePath(), TimeAccuracy(FileInfo.fileTime(QFileDevice::FileModificationTime))));
                    }
                    else {
                        SendLogMsg(MSG_CODE::CODE_INFORMATION, "File is empty. Ignored. File name: " + FileInfo.absoluteFilePath());
                    }
                }
                //если нужно - удаляем все файлы из директории
                if (CurrentTargetInfo.clearDirAfterSync) {
                    qDebug() << "-->Clear directory";
                    while (Dir.entryList().size() > 0) {
                        Dir.remove(Dir.entryList().first());
                    }
                }
            }
        }
        //сбрасываем флаг изменения
        CurrentTargetInfo.isChange = TTypeChange::NO_CHANGE;
    }

    if (Transfering) {
        if (DebugMode) {
            qDebug() << "The data transfer process is not yet complete. Skip. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
        }
        return;
    }
    else {
        Transfering = true;
        SendToHTTPServer();
    }
}

void TSync::onSendLogMsg(uint16_t Category, const QString &Msg)
{
    SendLogMsg(Category, Msg);
}

void TSync::onHTTPGetAnswer(const QByteArray &Answer)
{
  //  qDebug() << Answer;
    if (DebugMode) {
        qDebug() << "Get a response from the server. Size:" << Answer.size() << "Byte. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
    }

    QXmlStreamReader XMLReader(Answer);

    while ((!XMLReader.atEnd()) && (!XMLReader.hasError())) {
        QXmlStreamReader::TokenType Token = XMLReader.readNext();
        if (Token == QXmlStreamReader::StartDocument) continue;
        if (Token == QXmlStreamReader::EndDocument) break;
        if (Token == QXmlStreamReader::StartElement) {
            if (XMLReader.name().toString()  == "ProtocolVersion") continue; //пока версию протокола не проверяем
            else if (XMLReader.name().toString()  == "UndownloadedFileList") { //пришел список незагруженных файлов
                while (XMLReader.readNext() != QXmlStreamReader::EndElement) {
                    if (XMLReader.name().toString()  == "File") {
                        quint64 ID = 0;
                        QString HASH;
                        while (XMLReader.readNext() != QXmlStreamReader::EndElement) {
                            if (XMLReader.name().toString()  == "ID") ID = XMLReader.readElementText().toULongLong();
                            else if (XMLReader.name().toString()  == "HASH") HASH = XMLReader.readElementText();
                        }
                        if ((ID != 0) && (HASH != "")) {
                            if (FileForDownload.find(ID) == FileForDownload.end()) {
                                FileForDownload.insert(ID, HASH); //добавляем ХЭШ в список
                                if (DebugMode) {
                                    qDebug() << "<-Add file to queue for download. ID:" << ID << "HASH:" << HASH;
                                }
                            }
                        }
                    }
                }
            }
            else if (XMLReader.name().toString()  == "NewFileList") { //Пришел новый файл
                while (XMLReader.readNext() != QXmlStreamReader::EndElement) {
                    if (XMLReader.name().toString()  == "File") {
                        QString FileName;
                        QString HASH;
                        QByteArray Body;
                        QString Category;
                        while (XMLReader.readNext() != QXmlStreamReader::EndElement) {
                            if (XMLReader.name().toString()  == "FileName") FileName = XMLReader.readElementText();
                            else if (XMLReader.name().toString()  == "HASH") HASH = XMLReader.readElementText();
                            else if (XMLReader.name().toString()  == "Category") Category = XMLReader.readElementText();
                            else if (XMLReader.name().toString()  == "Body") Body = XMLReader.readElementText().toUtf8();
                        }
                        //если существует такая категория и пришел запрашиваемый файл
                        if ((CategoryToTarget.find(Category) != CategoryToTarget.end()) && (HASH == FileForDownload.first()) && (FileName != "")) {
                            if (SaveFile(Category, FileName, FindHash(HASH), Body)) {
                                //Сохранение прошло успешно. отмечаем это у себя
                                Config->beginGroup("SERVER");
                                Config->setValue("LastDownloadID", FileForDownload.firstKey());
                                Config->endGroup();
                                Config->sync();

                                //удаляем самую первую запись
                                HTTPServerInfo.LastDownloadID = FileForDownload.firstKey();
                                FileForDownload.erase(FileForDownload.begin());
                                //  HTTPServerInfo.LastDownloadID = FindHash(HASH);
                            }
                            else {
                                SendLogMsg(MSG_CODE::CODE_INFORMATION, "Cannot save file. Category: " + Category +
                                                                                   " File name: " + FileName +
                                                                                   " HASH:" + HASH +
                                                                                   " Request HASH: " + FileForDownload.first());
                            }
                        }
                        else {
                            SendLogMsg(MSG_CODE::CODE_INFORMATION, "Wrong file received. Category: " + Category +
                                                                               " File name: " + FileName +
                                                                               " HASH:" + HASH +
                                                                               " Request HASH: " + FileForDownload.first());
                        }
                    }
                }
            }
        }
    }

    if (XMLReader.hasError()) { //неудалось распарсить пришедшую XML
        SendLogMsg(MSG_CODE::CODE_ERROR, "Incorrect answer from server. Parser msg: " + XMLReader.errorString() + " Answer from server:" + Answer.left(200));
        return;
    }

    //если мы дошли до сюда, то обновляем данные в локальной БД
    QSqlQuery Query(DB);
    DB.transaction();

    QString QueryText = "UPDATE SYNCFILE "
                        "SET Body = '' "
                        "WHERE ID = " + QString::number(HTTPServerInfo.DeleteFileID);
    if (!Query.exec(QueryText)) {
        qDebug() << "FAIL Cannot execute query. Error: " << Query.lastError().text() << " Query: "<< Query.lastQuery();
        DB.rollback();
        exit(-2);
    }

    if (!DB.commit()) {
        qDebug() << "FAIL Cannot commit transation. Error: " << DB.lastError().text();
        DB.rollback();
        exit(-4);
    };

    if (HTTPServerInfo.LastFileID != HTTPServerInfo.DeleteFileID) {
        if (DebugMode) {
            qDebug() << "Not all files were sent to the server. Sending the following request. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
        }
        HTTPServerInfo.LastFileID = HTTPServerInfo.DeleteFileID;

        Config->beginGroup("SERVER");
        Config->setValue("LastFileID", HTTPServerInfo.LastFileID);
        Config->endGroup();
        Config->sync();

        SendToHTTPServer();
    }

    if (!FileForDownload.isEmpty()) {
        if (DebugMode) {
            qDebug() << "Not all files have been downloaded from the server. Sending the following request. Time:" << Timer.msecsTo(QTime::currentTime()) << "ms";
        }
        SendToHTTPServer();
    }



    SendLogMsg(TSync::CODE_INFORMATION, "Files has been successfully sync to the server."
                                        " Send: LastFileID: " + QString::number(HTTPServerInfo.LastFileID) +
                                        " Files waiting to be received: " + QString::number(FileForDownload.size()) +
                                        " Time: " + QString::number(Timer.msecsTo(QTime::currentTime())) + "ms");

    Transfering = false;
}

void TSync::AddFileToDB(const QFileInfo& FileInfo, const QString& Category)
{
    if (DebugMode) {
        qDebug() << "->Add file DB: " << FileInfo.absoluteFilePath();
    }

    //Сохраняем файл в БД
    QByteArray Body;

    QFile File(FileInfo.absoluteFilePath(), this);
    if (File.open(QIODevice::ReadOnly)) {
        Body = File.readAll().toBase64(QByteArray::Base64Encoding);
        File.close();

    }
    else {
        SendLogMsg(TSync::CODE_ERROR, "Cannot open file for sync. File name: " + FileInfo.absolutePath());
        return;
    }

    QSqlQuery QueryAdd(DB);
    DB.transaction();

    QString QueryText = "INSERT INTO SYNCFILE (CATEGORY, FILE_NAME, CREATE_DATE_TIME, CHANGE_DATE_TIME, BODY) VALUES ( "
                        "'" + Category + "', "
                        "'" + FileInfo.absoluteFilePath() + "', "
                        "'" + FileInfo.fileTime(QFileDevice::FileBirthTime).toString("yyyy-MM-dd hh:mm:ss.zzz") + "', "
                        "'" + FileInfo.fileTime(QFileDevice::FileModificationTime).toString("yyyy-MM-dd hh:mm:ss.zzz") + "', "
                        "? )";
    QueryAdd.prepare(QueryText);
    QueryAdd.bindValue(0, Body);
    if (!QueryAdd.exec()) {
        DB.rollback();
        qDebug() << "Cannot execute query. Error: " + QueryAdd.lastError().text() + " Query: " + QueryAdd.executedQuery();
        exit(-2);
    }
    if (!DB.commit()) {
        DB.rollback();
        qDebug() << "Cannot commit transation. Error: " + DB.lastError().text();
        exit(-4);
    };
}

qint64 TSync::FindHash(const QString &HASH)
{
    for (const auto& ItemID : FileForDownload.keys()) {
        if (FileForDownload[ItemID] == HASH) {
         //   qDebug() << "FindHash" << HASH << "ID" << ItemID;
            return ItemID;
        }
    }
    qDebug() << "Hash not find" << HASH << " return 0";
    return 0;
}

bool TSync::SaveFile(const QString &Category, const QString &FileName, quint64 ID, const QByteArray &Body)
{
    if (DebugMode) {
        qDebug() << "<-File received. HASH:" << FileForDownload[ID];
    }

    //декодируем тело файла
    QByteArray DecodeBody;
    auto result = QByteArray::fromBase64Encoding(Body);
    if (result.decodingStatus == QByteArray::Base64DecodingStatus::Ok) {
        DecodeBody = result.decoded; // Декодируем файл
    }
    else {
        SendLogMsg(MSG_CODE::CODE_INFORMATION, "Cannot decode file from Base64. Category: " + Category +
                                               " File name: " + FileName +
                                               " HASH:" + FileForDownload[ID]);
        return false;
    }
    //создаем директорию, если ее нет
    QString FullFileName = CategoryToTarget[Category] + FileName;
    QDir Dir;
    Dir.mkpath(QFileInfo(FullFileName).absolutePath());
    //пишем файл
    QFile File(FullFileName, this);
    if (File.open(QIODeviceBase::WriteOnly)) {
        File.write(DecodeBody);
        File.close();
        SendLogMsg(MSG_CODE::CODE_INFORMATION, "The file was saved successfully.  Category:"  + Category +
                                               " File name: " + FullFileName +
                                               " HASH:" + FileForDownload[ID] +
                                               " Size:" + QString::number(DecodeBody.size()));
        if (FileName.right(4) == ".cmd") RunCMD(FullFileName); //пришел командный файл
        if (FileName == "Update.zip") RunCMD(QCoreApplication::applicationDirPath() + "/Update.bat");//пришло обновление
    }
    else {
        SendLogMsg(MSG_CODE::CODE_INFORMATION, "Cannot write file. Category:"  + Category +
                                               " File name: " + FullFileName +
                                               " HASH:" + FileForDownload[ID]);
        return false;
    }


    return true;
}



QDateTime TSync::TimeAccuracy(const QDateTime &DateTime)
{
    //округляет время до точности формата
    return QDateTime::fromString(DateTime.toString("yyyy-MM-dd hh:mm:ss.zzz"), "yyyy-MM-dd hh:mm:ss.zzz");
}

void TSync::RunCMD(const QString &FileName)
{
    if (DebugMode) {
        qDebug() << "Batch file or update file found. Run.";
    }
    QProcess *Process = new QProcess(this);
    Process->setProgram("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe ");
    Process->setArguments(QStringList() << "-Command" << "Start-Process" << FileName << "-Verb RunAs");
    qDebug() << Process->arguments();
    if (Process->startDetached(nullptr)) {
        SendLogMsg(TSync::CODE_INFORMATION, "Run CMD: " + FileName);
    }
    else {
        SendLogMsg(TSync::CODE_ERROR, "Cannot run CMD: " + FileName);
    }
    Process->deleteLater();
}

void TSync::onDirectoryChanged(const QString &path)
{
    qDebug() << "Change path:" << path;
    Targets[path].isChange = CHANGE_DIR; //изменилась директория
}

void TSync::onFileChanged(const QString &path)
{
   qDebug() << "Change file:" << path;
   Targets[path].isChange = CHANGE_FILE;
}

void TSync::onHTTPError()
{
    Transfering = false;
}



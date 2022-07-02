#include <QCoreApplication>
#include <QTimer>
#include <QCommandLineParser>
#include "tsync.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    //устаналиваем основные настройки
    QCoreApplication::setApplicationName("LevelGauge");
    QCoreApplication::setOrganizationName("OOO 'SA'");
    QCoreApplication::setApplicationVersion("0.1a");

    setlocale(LC_CTYPE, ""); //настраиваем локаль

    //Создаем парсер параметров командной строки
    QCommandLineParser parser;
    parser.setApplicationDescription("Program for receiving data from the level gauge (make Veeder Root) and sending and sending them to the HTTP server.");
    parser.addHelpOption();
    parser.addVersionOption();

    //добавляем опцию Config
    QCommandLineOption Config(QStringList() << "Config", "Config file name", "ConfigFileNameValue", "Sync.ini");
    parser.addOption(Config);

    //Парсим опции командной строки
    parser.process(a);

    //читаем конфигурацию из файла
    QString ConfigFileName = parser.value(Config);
    if (!parser.isSet(Config))
        ConfigFileName = a.applicationDirPath() +"/" + parser.value(Config);

    qDebug() << "Reading configuration from " +  ConfigFileName;

    //настраиваем таймер
    QTimer Timer;
    Timer.setInterval(0);       //таймер сработает так быстро как только это возможно
    Timer.setSingleShot(true);  //таймер сработает 1 раз

    //создаем основной класс программы
    TSync Sync(ConfigFileName, &a);


    //При запуске выполняем слот Start
    QObject::connect(&Timer, SIGNAL(timeout()), &Sync, SLOT(onStart()));
    //Для завершения работы необходимоподать сигнал Finished
    QObject::connect(&Sync, SIGNAL(Finished()), &a, SLOT(quit()));

    //запускаем таймер
    Timer.start();
    //запускаем цикл обработчика событий
    return a.exec();
}

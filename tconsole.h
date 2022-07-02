/* Создает отдельный поток который слушает поток стандартного ввода
 * Когда приходит команда генерирует событие GetCommand в качестве
 * параметра передает текст комадны без знака перевода строки
 *
 * Для использования
*/
#ifndef TCONSOLE_H
#define TCONSOLE_H

#include <QObject>
#include <QThread>
#include <QTextStream>
#include <QtCore5Compat/QTextCodec>

class TConsole : public QThread
{
    Q_OBJECT
private:
    QTextStream *CmdStream;  //поток чтения команд

public:
    explicit TConsole(QObject *parent = nullptr);
    ~TConsole();

    void run() override; //основной цикл ожидания команды

signals:
     void GetCommand(const QString &cmd); //генерируется когда приходит нова команда
};

#endif // TCONSOLE_H

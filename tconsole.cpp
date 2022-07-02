#include <iostream>
#include <QTextStream>

#include "tconsole.h"


TConsole::TConsole(QObject *parent)
    : QThread(parent)
    , CmdStream(new QTextStream(stdin))  //связываем текстовый поток со стандартным потоком ввода
{
}

TConsole::~TConsole()
{
    //тормозим процесс
    this->terminate();
}

void TConsole::run()
{
    QString buf;
    while (1) {
        //ждем ввода команды
        QString tmp;
        tmp = CmdStream->read(1);
        buf += tmp;
        if (tmp == "\n") {
            emit GetCommand(buf);
            buf.clear();
        }
    }
}


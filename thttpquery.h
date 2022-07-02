#ifndef THTTPQUERY_H
#define THTTPQUERY_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QSettings>

class THTTPQuery : public QObject
{
    Q_OBJECT
private:
    QNetworkAccessManager manager; //менеджер обработки соединий
    QString NetworkError2Str(QNetworkReply::NetworkError ErrCode); //переводиит код ошибки в текстовое описание
    const QString Url;
    QNetworkReply *resp;

public:
    explicit THTTPQuery(const QString &Url, QObject *parent = nullptr);
    ~THTTPQuery();


    bool Run(const QByteArray &data); //запускает отправку запроса

public slots:
    void onReplyFinished(QNetworkReply * resp); //конец приема ответа
    void onErrorOccurred(QNetworkReply::NetworkError ErrCode); //возникла ошибка

signals:
    void GetAnswer(const QByteArray &Answer);
    void ErrorOccurred();
    void SendLogMsg(uint16_t Category, const QString &Msg);
};

#endif // THTTPQUERY_H

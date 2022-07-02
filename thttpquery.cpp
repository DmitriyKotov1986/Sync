#include <QCoreApplication>
#include "thttpquery.h"
#include "tsync.h"

THTTPQuery::THTTPQuery(const QString &Url, QObject *parent)
    : QObject(parent)
    , manager(parent)
    , Url(Url)
{
    manager.setTransferTimeout(300000);
    QObject::connect(&manager, SIGNAL(finished(QNetworkReply *)), this, SLOT(onReplyFinished(QNetworkReply *))); //событие конца обмена данными
}

THTTPQuery::~THTTPQuery()
{

}

bool THTTPQuery::Run(const QByteArray &data)
{
  //  qDebug() << "Connect: " << Url;
  //  qDebug() << data.size();

    QNetworkRequest Request(Url);
    Request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
    Request.setHeader(QNetworkRequest::UserAgentHeader, QCoreApplication::applicationName());
    Request.setHeader(QNetworkRequest::ContentLengthHeader, QString::number(data.size()));
    Request.setTransferTimeout(300000);

    resp = manager.post(Request, data);
    if (resp == nullptr) {
        emit SendLogMsg(TSync::CODE_ERROR, "Send HTTP request fail.");
        emit ErrorOccurred();
        return false;
    }
    QObject::connect(resp, SIGNAL(errorOccurred(QNetworkReply::NetworkError)), this, SLOT(onErrorOccurred(QNetworkReply::NetworkError))); //событие ошибки
    return true; //true - если запрос отправлен
}

void THTTPQuery::onReplyFinished(QNetworkReply *resp)
{
   // qDebug() << "HTTP Finished";
    if (resp->isOpen()) {
        emit GetAnswer(resp->readAll());
    }
    else {
        emit SendLogMsg(TSync::CODE_ERROR, "Response from HTTP server not received. Error: " + resp->errorString());
        emit ErrorOccurred();
    }

    resp->deleteLater();
}

void THTTPQuery::onErrorOccurred(QNetworkReply::NetworkError ErrCode)
{
    emit SendLogMsg(TSync::CODE_ERROR, "HTTP request fail. Msg: " + NetworkError2Str(ErrCode));
    resp->deleteLater();
    emit ErrorOccurred();
}

QString THTTPQuery::NetworkError2Str(QNetworkReply::NetworkError ErrCode) {
    switch (ErrCode) {
    case QNetworkReply::NoError : return "";
    case QNetworkReply::ConnectionRefusedError : return "The remote server refused the connection (the server is not accepting requests)";
    case QNetworkReply::RemoteHostClosedError : return "The remote server closed the connection prematurely, before the entire reply was received and processed";
    case QNetworkReply::HostNotFoundError : return "The remote host name was not found (invalid hostname)";
    case QNetworkReply::TimeoutError : return "The connection to the remote server timed out";
    case QNetworkReply::OperationCanceledError : return "The operation was canceled via calls to abort() or close() before it was finished.";
    case QNetworkReply::SslHandshakeFailedError : return "The SSL/TLS handshake failed and the encrypted channel could not be established. The sslErrors() signal should have been emitted.";
    case QNetworkReply::TemporaryNetworkFailureError : return "The connection was broken due to disconnection from the network, however the system has initiated roaming to another access point. The request should be resubmitted and will be processed as soon as the connection is re-established.";
    case QNetworkReply::NetworkSessionFailedError : return "The connection was broken due to disconnection from the network or failure to start the network.";
    case QNetworkReply::BackgroundRequestNotAllowedError : return "The background request is not currently allowed due to platform policy.";
    case QNetworkReply::TooManyRedirectsError : return "While following redirects, the maximum limit was reached. The limit is by default set to 50 or as set by QNetworkRequest::setMaxRedirectsAllowed(). (This value was introduced in 5.6.)";
    case QNetworkReply::InsecureRedirectError : return "While following redirects, the network access API detected a redirect from a encrypted protocol (https) to an unencrypted one (http). (This value was introduced in 5.6.)";
    case QNetworkReply::ProxyConnectionRefusedError : return "The connection to the proxy server was refused (the proxy server is not accepting requests)";
    case QNetworkReply::ProxyConnectionClosedError : return "The proxy server closed the connection prematurely, before the entire reply was received and processed";
    case QNetworkReply::ProxyNotFoundError : return "The proxy host name was not found (invalid proxy hostname)";
    case QNetworkReply::ProxyTimeoutError : return "The connection to the proxy timed out or the proxy did not reply in time to the request sent";
    case QNetworkReply::ProxyAuthenticationRequiredError : return "The proxy requires authentication in order to honour the request but did not accept any credentials offered (if any)";
    case QNetworkReply::ContentAccessDenied : return "The access to the remote content was denied (similar to HTTP error 403)";
    case QNetworkReply::ContentOperationNotPermittedError : return "The operation requested on the remote content is not permitted";
    case QNetworkReply::ContentNotFoundError : return "The remote content was not found at the server (similar to HTTP error 404)";
    case QNetworkReply::AuthenticationRequiredError : return "The remote server requires authentication to serve the content but the credentials provided were not accepted (if any)";
    case QNetworkReply::ContentReSendError : return "The request needed to be sent again, but this failed for example because the upload data could not be read a second time.";
    case QNetworkReply::ContentConflictError : return "The request could not be completed due to a conflict with the current state of the resource.";
    case QNetworkReply::ContentGoneError : return "The requested resource is no longer available at the server.";
    case QNetworkReply::InternalServerError : return "The server encountered an unexpected condition which prevented it from fulfilling the request.";
    case QNetworkReply::OperationNotImplementedError : return "The server does not support the functionality required to fulfill the request.";
    case QNetworkReply::ServiceUnavailableError : return "The server is unable to handle the request at this time.";
    case QNetworkReply::ProtocolUnknownError : return "The Network Access API cannot honor the request because the protocol is not known";
    case QNetworkReply::ProtocolInvalidOperationError : return "The requested operation is invalid for this protocol";
    case QNetworkReply::UnknownNetworkError : return "An unknown network-related error was detected";
    case QNetworkReply::UnknownProxyError : return "An unknown proxy-related error was detected";
    case QNetworkReply::UnknownContentError : return "An unknown error related to the remote content was detected";
    case QNetworkReply::ProtocolFailure : return "A breakdown in protocol was detected (parsing error, invalid or unexpected responses, etc.)";
    case QNetworkReply::UnknownServerError : return "An unknown error related to the server response was detected";
    }
    return "Undefine error";
}


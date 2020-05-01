#include <QtNetwork>
#include <QtCore>

#include <QDebug>

#include <QTcpSocket>
#include <QHash>
#include <QProcess>

#ifndef LCDCLIENT_H_
#define LCDCLIENT_H_

class LcdClient : public QObject
{
    Q_OBJECT

public:
    explicit LcdClient(QObject *parent = nullptr);

private slots:
    void readServerResponse();
    void handleSocketError(QAbstractSocket::SocketError socketError);

private:
    QTcpSocket lcdSocket;
    QProcess nmcli;
};
#endif  // LCDCLIENT_H_

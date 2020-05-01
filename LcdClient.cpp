#include "LcdClient.hpp"

// Constructor and initialization routines (Opening files, connecting to LCDd, ...)
LcdClient::LcdClient(QObject *parent)
    : QObject(parent)
{
    connect(&lcdSocket, &QIODevice::readyRead, this, &LcdClient::readServerResponse);
    connect(&lcdSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &LcdClient::handleSocketError);
    lcdSocket.abort();
    lcdSocket.connectToHost("127.0.0.1", 13666);

    lcdSocket.write("hello\n");
}

// Parse responses from LCDd (via TCP socket)
void LcdClient::readServerResponse()
{
    QString response = lcdSocket.readAll();
    QStringList lines = response.split("\n", QString::SkipEmptyParts);

    QString line;
    foreach(line, lines) {
        if (line == "success") {
            continue;
        }
        qDebug() << "LCDd resp:" << line;

        if (line.startsWith("connect ")) {
            // Set client name
            lcdSocket.write("client_set -name Netzwerk\n");

            // Add menu structure
        }
    }
}

// Handle socket errors on LCDd communication socket
void LcdClient::handleSocketError(QAbstractSocket::SocketError socketError)
{
    qWarning() << "LCDd socket error: " << socketError;
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        break;
    case QAbstractSocket::ConnectionRefusedError:
        break;
    default:
        break;
    }
}

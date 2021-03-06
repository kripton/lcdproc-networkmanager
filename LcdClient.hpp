#include <QtNetwork>
#include <QtCore>

#include <QDebug>

#include <QTcpSocket>
#include <QHash>
#include <QTimer>

#include <NetworkManagerQt/GenericTypes>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Device>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/WirelessDevice>
#include <NetworkManagerQt/AccessPoint>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/WirelessSetting>
#include <NetworkManagerQt/WirelessSecuritySetting>
#include <NetworkManagerQt/Ipv4Setting>

#ifndef LCDCLIENT_H_
#define LCDCLIENT_H_

using namespace NetworkManager;

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
    QTimer mainMenuRefreshTimer;

    QMap<QString, QStringList> menuEntries;
    QMap<QString, QString> ssidMap;
    QMap<QString, QString> wiFiConnectOptions;

    Device::Ptr findInterfaceByName(QString interfaceName);
    Connection::Ptr getOrCreateEthernetConection(QString interfaceName);
    void connectToWifi(QString interfaceName, QString apPath);
    void updateNetworkConfig(QString interfaceName, QString optionName, QString newValue);
    void updateMainMenuEntries();
    void updateSubMenuEntries(QString interfaceName);
    void scanAndConnect(QString interfaceName);

    void addMenuItem(QString parent, QString newId, QString rest);
    void delMenuItem(QString parent, QString id);
    void emptyMenu(QString id);
};
#endif  // LCDCLIENT_H_

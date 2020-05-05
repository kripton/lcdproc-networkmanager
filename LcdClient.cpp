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
            updateMainMenuEntries();

        } else if (line == "menuevent enter _client_menu_") {
            updateMainMenuEntries();

        } else if (line.startsWith("menuevent enter ") && (line.count("_") == 0)) {
            // An interface has been selected in the menu
            qDebug() << "updateSubMenuEntries(" << line.split(" ")[2] << ");";
            updateSubMenuEntries(line.split(" ")[2]);

        } else if (line.startsWith("menuevent enter ") && (line.endsWith("_list"))) {
            // Display the WiFi networks visible to interface
            qDebug() << "ScanAndConnect(" << line.split(" ")[2].split("_")[0] << ");";
            scanAndConnect(line.split(" ")[2].split("_")[0]);

        } else if (
            line.startsWith("menuevent update ") ||
            line.startsWith("menuevent select ")
        ) {
            // "Update" means some property has been changed in a menu
            // Examples: "eth0_dhcp off", "eth0_ip 192.168.1.1", "eth0_prefix 24"
            //
            // "Select" means that an action shall be executed
            // Examples: "wlan0_disconnect", "wlan0_list_567"
            QStringList parts = line.replace("_", " ").split(" ");
            QString interFaceName = parts[2];
            QString optionName = parts[3];
            QString newValue = "";
            if (parts.size() == 5) {
                newValue = parts[4];
            }
            updateNetworkConfig(interFaceName, optionName, newValue);
            updateSubMenuEntries(interFaceName);

        }
    }
}

void LcdClient::updateNetworkConfig(QString interFaceName, QString optionName, QString newValue)
{
    qDebug() << "F:updateNetworkConfig(" << interFaceName << optionName << newValue << ")";
    Device::Ptr dev;
    Connection::Ptr con;
    ConnectionSettings::Ptr settings;
    QDBusPendingReply<> reply;

    dev = findInterfaceByName(interFaceName);

    if (dev->type() == Device::Ethernet) {
        con = getOrCreateEthernetConection(interFaceName);
        settings = con->settings();
        Ipv4Setting::Ptr ipv4Setting = settings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();

        if (optionName == "dhcp") {
            if (newValue == "off") {
                if (ipv4Setting->addresses().size() == 0) {
                    IpAddress addr;
                    addr.setIp(QHostAddress("192.168.123.234"));
                    addr.setNetmask(QHostAddress("255.255.255.0"));
                    QList<IpAddress> addresses = ipv4Setting->addresses();
                    addresses.append(addr);
                    ipv4Setting->setAddresses(addresses);
                }
                ipv4Setting->setMethod(Ipv4Setting::Manual);
            } else {
                ipv4Setting->setMethod(Ipv4Setting::Automatic);
            }
        } else if (optionName == "ip") {
            IpAddress addr;
            if (ipv4Setting->addresses().size() == 0) {
                addr.setPrefixLength(24);
            } else {
                addr = ipv4Setting->addresses()[0];
            }
            addr.setIp(QHostAddress(newValue));
            QList<IpAddress> addresses = ipv4Setting->addresses();
            addresses.clear();
            addresses.append(addr);
            ipv4Setting->setAddresses(addresses);
        } else if (optionName == "prefix") {
            IpAddress addr;
            if (ipv4Setting->addresses().size() == 0) {
                addr.setIp(QHostAddress("192.168.123.234"));
            } else {
                addr = ipv4Setting->addresses()[0];
            }
            addr.setPrefixLength(newValue.toInt());
            QList<IpAddress> addresses = ipv4Setting->addresses();
            addresses.clear();
            addresses.append(addr);
            ipv4Setting->setAddresses(addresses);
        }
        reply = con->updateUnsaved(settings->toMap());
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();
        reply = con->save();
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();
        reply = activateConnection(con->path(), dev->uni(), "");
        qDebug() << reply.isValid() << reply.error();

    } else if (dev->type() == Device::Wifi) {

        if (optionName == "disconnect") {
            reply = dev->disconnectInterface();
            reply.waitForFinished();
            qDebug() << reply.isValid() << reply.error();
        }

    }
}

void LcdClient::scanAndConnect(QString interFaceName)
{
    Device::Ptr dev;
    WirelessDevice::Ptr wDev;
    QDBusPendingReply<> reply;

    dev = findInterfaceByName(interFaceName);
    wDev = dev.dynamicCast<WirelessDevice>();

    emptyMenu(QString("%1_list").arg(interFaceName));

    reply = wDev->requestScan();
    reply.waitForFinished();
    usleep(6000000);

    qDebug() << "WIFI LIST ENTRY:" << wDev->accessPoints();
    QString apPath;
    AccessPoint *ap;
    QStringList ssids;
    foreach(apPath, wDev->accessPoints()) {
        ap = new AccessPoint(apPath);
        qDebug() << "PATH:" << apPath.split("/")[5] << "SSID:" << ap->ssid();
        // We are removing duplicates here
        if (!ssids.contains(ap->ssid())) {
            addMenuItem(QString("%1_list").arg(interFaceName),
                QString("%1_list_%2").arg(interFaceName).arg(apPath.split("/")[5]),
                QString("action \"%1\"").arg(ap->ssid()));
                ssids.append(ap->ssid());
        }
        delete ap;
    }
    delMenuItem(QString("%1_list").arg(interFaceName), QString("%1_list_dummy").arg(interFaceName));

}

Device::Ptr LcdClient::findInterfaceByName(QString interFaceName)
{
    const Device::List deviceList = NetworkManager::networkInterfaces();

    Device::Ptr dev;

    foreach(dev, deviceList) {
        if (dev->interfaceName() == interFaceName) {
            break;
        }
    }
    // In case the interface was not in the list, we would
    // just have the last interface of the list. That would
    // happen if an interface was removed in the meantime.
    // Pretty low probability => assume we have the right one

    return dev;
}

Connection::Ptr LcdClient::getOrCreateEthernetConection(QString interFaceName)
{
    Connection::Ptr con;
    const Connection::List conList = NetworkManager::listConnections();

    // There should be exactly one connection with the interface name as id
    int found = 0;
    foreach(con, conList) {
        if (con->name() == interFaceName) {
            found = 1;
            break;
        }
    }
    // If not, we create one here
    if (!found) {
        QString uuid = QUuid::createUuid().toString().mid(1, QUuid::createUuid().toString().length() - 2);
        NetworkManager::ConnectionSettings *newConSettings = new NetworkManager::ConnectionSettings(NetworkManager::ConnectionSettings::Wired);
        qDebug() << "Creating new connection for" << interFaceName << ":" << newConSettings;
        newConSettings->setId(interFaceName);
        newConSettings->setUuid(uuid);
        newConSettings->setInterfaceName(interFaceName);
        newConSettings->setAutoconnect(true);
        NetworkManager::Ipv4Setting::Ptr newIpv4Setting = newConSettings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
        newIpv4Setting->setMethod(NetworkManager::Ipv4Setting::Automatic);
        QDBusPendingReply<QDBusObjectPath,QDBusObjectPath> reply = NetworkManager::addConnection(newConSettings->toMap());
        reply.waitForFinished();
        // There *could* be an error but on Kiste3000 it will simply work
        usleep(500000);
        // Re-search for the connection now that we added it
        found = 0;
        foreach(con, conList) {
            if (con->name() == interFaceName) {
                found = 1;
                break;
            }
        }
    }

    return con;
}

void LcdClient::updateSubMenuEntries(QString interFaceName)
{
    Device::Ptr dev;
    WirelessDevice::Ptr wDev;
    Connection::Ptr con;
    ConnectionSettings::Ptr settings;
    WirelessSetting::Ptr wSettings;

    // Add a dummy entry so one is not kicked out of the menu when emptying it
    addMenuItem(interFaceName, QString("%1_dummy").arg(interFaceName), "action \"ERROR\"");
    emptyMenu(interFaceName);

    dev = findInterfaceByName(interFaceName);

    if (dev->type() == Device::Ethernet) {
        con = getOrCreateEthernetConection(interFaceName);

        if (con.isNull()) {
            qDebug() << "CONNECTION STILL NOT FOUND";
            return;
        }

        settings = con->settings();

        Ipv4Setting::Ptr ipv4Setting = settings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
        QString dhcp = "off";
        if (ipv4Setting->method() == Ipv4Setting::Automatic) {
            dhcp = "on";
        }

        addMenuItem(interFaceName, QString("%1_dhcp").arg(interFaceName),
            QString("checkbox \"DHCP\" -value %1")
            .arg(dhcp));

        if (dhcp == "off") {
            // TODO? We assume that there is one IPv4 address per connection
            QHostAddress ip = QHostAddress("192.168.123.234");
            int prefixLength = 24;
            QHostAddress netmask = QHostAddress("255.255.255.0");
            if (ipv4Setting->addresses().size()) {
                ip.setAddress(ipv4Setting->addresses()[0].ip().toString());
                prefixLength = ipv4Setting->addresses()[0].prefixLength();
            }
            qDebug() << "IP:" << ip << "prefixLength:" << prefixLength;

            addMenuItem(interFaceName, QString("%1_ip").arg(interFaceName),
                QString("ip \"IP\" -value \"%1\"")
                .arg(ip.toString()));

            addMenuItem(interFaceName, QString("%1_prefix").arg(interFaceName),
                QString("numeric \"PrefixLn\" -minvalue \"1\" -maxvalue \"31\" -value \"%1\"")
                .arg(prefixLength));
        } else {
            Dhcp4Config::Ptr dhcpCfg = dev->dhcp4Config();
            // For info only ...
            if (dhcpCfg->options().contains("ip_address")) {
                addMenuItem(interFaceName, QString("%1_ipDisplay").arg(interFaceName),
                    QString("action \"%1\"")
                    .arg(dhcpCfg->optionValue("ip_address"), 16));
            }
        }

    } else if (dev->type() == Device::Wifi) {
        // IF CONNECTED:
        //  "SSID"
        //  Disconnect action
        //  IPv4Settings for active connection
        // IN ANY CASE:
        //  Connect -> SSID LIST
        //  Start AP
        wDev = dev.dynamicCast<WirelessDevice>();
        con = wDev->activeConnection()->connection();
        qDebug() << "wDev:" << wDev << "CON:" << con;
        settings = con->settings();
        qDebug() << "Settings:" << settings;
        //wSettings = WirelessSetting::Ptr(settings);
        //qDebug() << "wSettings SSID" << QString::fromUtf8(wSettings->ssid());

        addMenuItem(interFaceName, QString("%1_ssid").arg(interFaceName),
            QString("action \"SSID:%1\"")
            .arg(wDev->activeAccessPoint()->ssid()));

        addMenuItem(interFaceName, QString("%1_disconnect").arg(interFaceName),
            QString("action \"Disconnect\" -menu_result close"));

        Ipv4Setting::Ptr ipv4Setting = settings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
        QString dhcp = "off";
        if (ipv4Setting->method() == Ipv4Setting::Automatic) {
            dhcp = "on";
        }

        addMenuItem(interFaceName, QString("%1_dhcp").arg(interFaceName),
            QString("checkbox \"DHCP\" -value %1")
            .arg(dhcp));

        if (dhcp == "off") {
            // TODO? We assume that there is one IPv4 address per connection
            QHostAddress ip = QHostAddress("192.168.123.234");
            int prefixLength = 24;
            QHostAddress netmask = QHostAddress("255.255.255.0");
            if (ipv4Setting->addresses().size()) {
                ip.setAddress(ipv4Setting->addresses()[0].ip().toString());
                prefixLength = ipv4Setting->addresses()[0].prefixLength();
            }
            qDebug() << "IP:" << ip << "prefixLength:" << prefixLength;

            addMenuItem(interFaceName, QString("%1_ip").arg(interFaceName),
                QString("ip \"IP\" -value \"%1\"")
                .arg(ip.toString()));

            addMenuItem(interFaceName, QString("%1_prefix").arg(interFaceName),
                QString("numeric \"PrefixLn\" -minvalue \"1\" -maxvalue \"31\" -value \"%1\"")
                .arg(prefixLength));
        } else {
            Dhcp4Config::Ptr dhcpCfg = dev->dhcp4Config();
            // For info only ...
            if (dhcpCfg->options().contains("ip_address")) {
                addMenuItem(interFaceName, QString("%1_ipDisplay").arg(interFaceName),
                    QString("action \"%1\"")
                    .arg(dhcpCfg->optionValue("ip_address"), 16));
            }
        }

        addMenuItem(interFaceName, QString("%1_list").arg(interFaceName),
            QString("menu \"ScanAndConnect\""));

        addMenuItem(QString("%1_list").arg(interFaceName), QString("%1_list_dummy").arg(interFaceName),
            QString("action \"Scanning ...\""));

        addMenuItem(interFaceName, QString("%1_startAP").arg(interFaceName),
            QString("menu \"Start NEW AP\""));

        addMenuItem(QString("%1_startAP").arg(interFaceName), QString("%1_startAP_dummy").arg(interFaceName),
            QString("action \"StartAP\""));
    }

    delMenuItem(interFaceName, QString("%1_dummy").arg(interFaceName));
}

// Remove and re-add the main menu entries (= Network interfaces)
void LcdClient::updateMainMenuEntries()
{
    const Device::List deviceList = NetworkManager::networkInterfaces();
    const Connection::List conList = NetworkManager::listConnections();

    // TODO: Better: Instead of removing all entries, it would be better
    //               to update the user-visible texts

    // Add a dummy entry to the menu in order to not
    // have an empty client menu that would kick the user out of it
    addMenuItem("", "_dummy", "action \"No interfaces :(\"");

    // Remove all interfaces from the menu
    emptyMenu("");

    // To have the string representation of Device::State
    QMetaEnum metaEnum = QMetaEnum::fromType<Device::State>();

    // Filter the ones that are of interest here
    // and add them to our client's menu
    for (Device::Ptr dev : deviceList) {
        if (
            ((dev->type() != Device::Wifi) && (dev->type() != Device::Ethernet)) ||
            (dev->state() == Device::Unmanaged)
        ) {
            continue;
        }

        addMenuItem("", dev->interfaceName(), QString("menu \"%1(%2)\"\n")
            .arg(dev->interfaceName())
            .arg(metaEnum.valueToKey(dev->state()))
            .toLatin1());
    }

    // Remove the DUMMY entry again IF there are any. Otherwise,
    // leave it there as a hint to the user
    if (menuEntries["_"].size()) {
        delMenuItem("", "_dummy");
    }

}

// Add a menu entry and store it in menuEntries so we can empty all menus easily
void LcdClient::addMenuItem(QString parent, QString newId, QString rest)
{
    QString parentKey = parent;
    if (parent == "") {
        parentKey = "_";
    }
    if (!menuEntries.contains(parentKey)) {
        menuEntries[parentKey] = QStringList();
    }

    qDebug() << "ADD. Adding" << parent << newId << rest;

    menuEntries[parentKey].append(newId);

    lcdSocket.write(QString("menu_add_item \"%1\" \"%2\" %3\n")
        .arg(parent)
        .arg(newId)
        .arg(rest)
        .toLatin1());
}

// Remove a menu entry and remove it from menuEntries
void LcdClient::delMenuItem(QString parent, QString id)
{
    QString parentKey = parent;
    if (parent == "") {
        parentKey = "_";
    }

    // TODO: Remove recursive / Children first?

    qDebug() << "DEL. Deleting" << parent << id;

    menuEntries[parentKey].removeAll(id);
    lcdSocket.write(QString("menu_del_item \"IGNORED\" \"%1\"\n")
        .arg(id)
        .toLatin1());
}

// Remove all children of named menu id
// EXCEPT entries with id ending on "_dummy". Those exist to avoid
// kicking the user out of the current menu when clearing it
void LcdClient::emptyMenu(QString id)
{
    QString idKey = id;
    if (idKey == "") {
        idKey = "_";
    }
    if (!menuEntries.contains(idKey)) {
        return;
    }

    QString childId;
    foreach (childId, menuEntries[idKey]) {
        if (!childId.endsWith("_dummy")) {
            qDebug() << "EMPTY. Removing" << id << childId;
            delMenuItem(id, childId);
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

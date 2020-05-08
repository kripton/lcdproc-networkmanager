#include "LcdClient.hpp"

// Constructor and initialization routines (Opening files, connecting to LCDd, ...)
LcdClient::LcdClient(QObject *parent)
    : QObject(parent)
{
    connect(&mainMenuRefreshTimer, &QTimer::timeout, this, &LcdClient::updateMainMenuEntries);

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

            // Start the periodic updating of the main menu entries
            mainMenuRefreshTimer.start(500);

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
            // Examples: "eth0_dhcp off", "eth0_ip 192.168.1.1", "eth0_prefix 24", wlan0_list_1234_pass ABCDEDFG"
            //
            // "Select" means that an action shall be executed
            // Examples: "wlan0_disconnect"
            QStringList parts = line.replace("_", " ").split(" ");
            QString interfaceName = parts[2];
            QString optionName = parts[3];
            QString newValue = "";

            if ((parts.size() == 7) && (optionName == "list")) {
                wiFiConnectOptions[parts[5]] = parts[6];
                qDebug() << "wiFiConnectOptions" << wiFiConnectOptions;
                if ((parts[5] == "dhcp") && (parts[6] == "on")) {
                    lcdSocket.write(QString("menu_set_item \"\" \"%1_list_%2_ip\" -is_hidden \"true\"\n")
                        .arg(interfaceName).arg(parts[4]).toLatin1());
                    lcdSocket.write(QString("menu_set_item \"\" \"%1_list_%2_prefix\" -is_hidden \"true\"\n")
                        .arg(interfaceName).arg(parts[4]).toLatin1());
                }
                if ((parts[5] == "dhcp") && (parts[6] == "off")) {
                    lcdSocket.write(QString("menu_set_item \"\" \"%1_list_%2_ip\" -is_hidden \"false\"\n")
                        .arg(interfaceName).arg(parts[4]).toLatin1());
                    lcdSocket.write(QString("menu_set_item \"\" \"%1_list_%2_prefix\" -is_hidden \"false\"\n")
                        .arg(interfaceName).arg(parts[4]).toLatin1());
                }
                return;

            } else if (line.endsWith(" connect")) {
                connectToWifi(interfaceName, parts[4]);
                return;

            } else if (parts.size() == 5) {
                newValue = parts[4];
            }

            updateNetworkConfig(interfaceName, optionName, newValue);
            updateSubMenuEntries(interfaceName);

        }
    }
}

void LcdClient::updateNetworkConfig(QString interfaceName, QString optionName, QString newValue)
{
    qDebug() << "F:updateNetworkConfig(" << interfaceName << optionName << newValue << ")";
    Device::Ptr dev;
    Connection::Ptr con;
    ConnectionSettings::Ptr settings;
    QDBusPendingReply<> reply;

    dev = findInterfaceByName(interfaceName);

    if (dev->type() == Device::Ethernet) {
        con = getOrCreateEthernetConection(interfaceName);
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
                ipv4Setting->addresses().clear();
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
        qDebug() << "Connection" << con->path() << "(" << con->name() << ") on device" << dev->uni() << ": Updating, saving and activating, ...";
        reply = con->updateUnsaved(settings->toMap());
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();
        reply = con->save();
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();
        reply = activateConnection(con->path(), dev->uni(), "");
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();

    } else if (dev->type() == Device::Wifi) {

        if (optionName == "disconnect") {
            reply = dev->disconnectInterface();
            reply.waitForFinished();
            qDebug() << reply.isValid() << reply.error();

        }

    }
}

void LcdClient::scanAndConnect(QString interfaceName)
{
    Device::Ptr dev;
    WirelessDevice::Ptr wDev;
    QDBusPendingReply<> reply;

    dev = findInterfaceByName(interfaceName);
    wDev = dev.dynamicCast<WirelessDevice>();

    emptyMenu(QString("%1_list").arg(interfaceName));

    // Clear the list of options entered for the WiFi to connect to and set defaults
    wiFiConnectOptions.clear();
    wiFiConnectOptions["dhcp"] = "on";
    wiFiConnectOptions["ip"] = "192.168.123.234";
    wiFiConnectOptions["prefix"] = "24";

    reply = wDev->requestScan();
    reply.waitForFinished();
    usleep(10000000);

    qDebug() << "WIFI LIST ENTRY:" << wDev->accessPoints();
    QString apPath;
    AccessPoint *ap;
    QStringList ssids;
    foreach(apPath, wDev->accessPoints()) {
        ap = new AccessPoint(apPath);
        qDebug() << "PATH:" << apPath.split("/")[5] << "SSID:" << ap->ssid();
        // We are removing duplicates here
        if (!ssids.contains(ap->ssid())) {
            ssids.append(ap->ssid());

            ssidMap[apPath.split("/")[5]] = ap->ssid();

            // The list entry itself as a menu
            addMenuItem(
                QString("%1_list").arg(interfaceName),
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("menu \"%1\"").arg(ap->ssid()));

            // The network's passphrase/key
            addMenuItem(
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("%1_list_%2_pass").arg(interfaceName).arg(apPath.split("/")[5]),
                "alpha \"Password\" -value \"\" -minlength 8 -maxlength 32 -allow_caps true -allow_noncaps true -allow_numbers true -allowed_extra \"!§$%&/()=@\"");

            // IPv4 settings
            addMenuItem(
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("%1_list_%2_dhcp").arg(interfaceName).arg(apPath.split("/")[5]),
                "checkbox \"DHCP\" -value on");
            addMenuItem(
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("%1_list_%2_ip").arg(interfaceName).arg(apPath.split("/")[5]),
                "ip \"IP\" -is_hidden true -value \"192.168.123.234\"");
            addMenuItem(
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("%1_list_%2_prefix").arg(interfaceName).arg(apPath.split("/")[5]),
                "numeric \"PrefixLn\" -is_hidden true -minvalue \"1\" -maxvalue \"31\" -value \"24\"");

            // The "CONNECT" button
            addMenuItem(
                QString("%1_list_%2").arg(interfaceName).arg(apPath.split("/")[5]),
                QString("%1_list_%2_connect").arg(interfaceName).arg(apPath.split("/")[5]),
                "action \"CONNECT\"");
        }
        delete ap;
    }
    delMenuItem(QString("%1_list").arg(interfaceName), QString("%1_list_dummy").arg(interfaceName));

}

Device::Ptr LcdClient::findInterfaceByName(QString interfaceName)
{
    const Device::List deviceList = NetworkManager::networkInterfaces();

    Device::Ptr dev;

    foreach(dev, deviceList) {
        if (dev->interfaceName() == interfaceName) {
            break;
        }
    }
    // In case the interface was not in the list, we would
    // just have the last interface of the list. That would
    // happen if an interface was removed in the meantime.
    // Pretty low probability => assume we have the right one

    return dev;
}

Connection::Ptr LcdClient::getOrCreateEthernetConection(QString interfaceName)
{
    Connection::Ptr con;
    const Connection::List conList = NetworkManager::listConnections();

    // There should be exactly one connection with the interface name as id
    int found = 0;
    foreach(con, conList) {
        if (con->name() == interfaceName) {
            found = 1;
            break;
        }
    }
    // If not, we create one here
    if (!found) {
        QString uuid = QUuid::createUuid().toString().mid(1, QUuid::createUuid().toString().length() - 2);
        ConnectionSettings *newConSettings = new ConnectionSettings(ConnectionSettings::Wired);
        qDebug() << "Creating new connection for" << interfaceName << ":" << newConSettings;
        newConSettings->setId(interfaceName);
        newConSettings->setUuid(uuid);
        newConSettings->setInterfaceName(interfaceName);
        newConSettings->setAutoconnect(true);
        Ipv4Setting::Ptr newIpv4Setting = newConSettings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
        newIpv4Setting->setMethod(Ipv4Setting::Automatic);
        QDBusPendingReply<QDBusObjectPath,QDBusObjectPath> reply = addConnection(newConSettings->toMap());
        reply.waitForFinished();
        // There *could* be an error but on Kiste3000 it will magically work
        usleep(500000);
        // Re-search for the connection now that we added it
        found = 0;
        foreach(con, conList) {
            if (con->name() == interfaceName) {
                found = 1;
                break;
            }
        }
    }

    return con;
}

// Connect to a WiFi access point
void LcdClient::connectToWifi(QString interfaceName, QString apPath)
{
    // InterfaceName and last part of the AP's path is in the parameter
    // all other options are in wiFiConnectOptions

    Device::Ptr dev = findInterfaceByName(interfaceName);

    // Check if a connection with that id already exists
    // otherwise, create a new one

    qDebug() << "connectToWifi" << interfaceName << apPath;
    qDebug() << "SSID:" << ssidMap[apPath];

    Connection::Ptr con;
    const Connection::List conList = NetworkManager::listConnections();

    // There should be exactly one connection with the ssid as id
    int found = 0;
    foreach(con, conList) {
        if (con->name() == ssidMap[apPath]) {
            found = 1;
            break;
        }
    }
    ConnectionSettings::Ptr settings;
    // If not, we create one here
    if (!found) {
        QString uuid = QUuid::createUuid().toString().mid(1, QUuid::createUuid().toString().length() - 2);
        ConnectionSettings *settingsPtr = new ConnectionSettings(ConnectionSettings::Wireless);
        settings = ConnectionSettings::Ptr(settingsPtr);
        qDebug() << "Creating new connection for" << interfaceName << ":" << settings;
        settings->setUuid(uuid);
        settings->setId(ssidMap[apPath]);
    } else {
        settings = con->settings();
    }

    WirelessSetting::Ptr wirelessSetting = settings->setting(Setting::Wireless).dynamicCast<WirelessSetting>();
    wirelessSetting->setSsid(ssidMap[apPath].toUtf8());
    settings->setInterfaceName(interfaceName);
    settings->setAutoconnect(true);

    WirelessSecuritySetting::Ptr wifiSecurity = settings->setting(Setting::WirelessSecurity).dynamicCast<WirelessSecuritySetting>();
    wifiSecurity->setKeyMgmt(WirelessSecuritySetting::WpaPsk);
    wifiSecurity->setPsk(wiFiConnectOptions["pass"]);
    wifiSecurity->setInitialized(true);
    wirelessSetting->setSecurity("802-11-wireless-security");

    Ipv4Setting::Ptr IPv4Setting = settings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
    // TODO: Get option and set accordingly instead of simply using DHCP
    IPv4Setting->setMethod(NetworkManager::Ipv4Setting::Automatic);

    if (!found) {
        QDBusPendingReply<QDBusObjectPath,QDBusObjectPath> reply = NetworkManager::addAndActivateConnection(settings->toMap(), dev->uni(), nullptr);
        reply.waitForFinished();
        qDebug() << reply.isValid() << reply.error();
    }
}

// Update the menu items for one interface
// Entries strongly depend on device type and connection status
void LcdClient::updateSubMenuEntries(QString interfaceName)
{
    Device::Ptr dev;
    WirelessDevice::Ptr wDev;
    ActiveConnection::Ptr activeCon;
    Connection::Ptr con;
    ConnectionSettings::Ptr settings;
    WirelessSetting::Ptr wSettings;

    // Initialize as NULL
    settings = QSharedPointer<ConnectionSettings>();

    // Add a dummy entry so one is not kicked out of the menu when emptying it
    addMenuItem(interfaceName, QString("%1_dummy").arg(interfaceName), "action \"ERROR\"");
    emptyMenu(interfaceName);

    // Step 1: Get the proper device entry
    dev = findInterfaceByName(interfaceName);

    // Step 2: Find the currently active settings
    //         For Ethernet devices, they will be created if not existing
    //         For WiFi, it can be NULL if not connected
    if (dev->type() == Device::Ethernet) {
        con = getOrCreateEthernetConection(interfaceName);

        if (con.isNull()) {
            qDebug() << "CONNECTION STILL NOT FOUND";
            return;
        }
        settings = con->settings();

    } else if (dev->type() == Device::Wifi) {
        // IF CONNECTED: "SSID", Disconnect action, IPv4Settings for active connection
        // IN ANY CASE: ScanAndConnect -> SSID LIST, Start NEW AP
        wDev = dev.dynamicCast<WirelessDevice>();
        activeCon = wDev->activeConnection();
        qDebug() << "wDev:" << wDev << "ActiveCon:" << activeCon;

        if (!activeCon.isNull()) {
            settings = activeCon->connection()->settings();

            qDebug() << "Settings:" << settings;

            addMenuItem(interfaceName, QString("%1_ssid").arg(interfaceName),
                QString("action \"SSID:%1\"")
                .arg(wDev->activeAccessPoint()->ssid()));

            addMenuItem(interfaceName, QString("%1_disconnect").arg(interfaceName),
                QString("action \"Disconnect\" -menu_result close"));
        }
    }

    // Step 3: If we do have valid ConnectionSettings, add the IPv4 menu entries
    if (!settings.isNull()) {
        Ipv4Setting::Ptr ipv4Setting = settings->setting(Setting::Ipv4).dynamicCast<Ipv4Setting>();
        QString dhcp = "off";
        if (ipv4Setting->method() == Ipv4Setting::Automatic) {
            dhcp = "on";
        }

        addMenuItem(interfaceName, QString("%1_dhcp").arg(interfaceName),
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

            addMenuItem(interfaceName, QString("%1_ip").arg(interfaceName),
                QString("ip \"IP\" -value \"%1\"")
                .arg(ip.toString()));

            addMenuItem(interfaceName, QString("%1_prefix").arg(interfaceName),
                QString("numeric \"PrefixLn\" -minvalue \"1\" -maxvalue \"31\" -value \"%1\"")
                .arg(prefixLength));
        } else {
            Dhcp4Config::Ptr dhcpCfg = dev->dhcp4Config();
            // For info only ...
            if (dhcpCfg->options().contains("ip_address")) {
                addMenuItem(interfaceName, QString("%1_ipDisplay").arg(interfaceName),
                    QString("action \"%1\"")
                    .arg(dhcpCfg->optionValue("ip_address")));
            }
        }
    }

    // Step 4: Special entries only for WiFi interfaces
    if (dev->type() == Device::Wifi) {
        addMenuItem(interfaceName, QString("%1_list").arg(interfaceName),
            QString("menu \"ScanAndConnect\""));

        addMenuItem(QString("%1_list").arg(interfaceName), QString("%1_list_dummy").arg(interfaceName),
            QString("action \"Scanning ...\""));

        addMenuItem(interfaceName, QString("%1_startAP").arg(interfaceName),
            QString("menu \"Start NEW AP\""));

        addMenuItem(QString("%1_startAP").arg(interfaceName), QString("%1_startAP_dummy").arg(interfaceName),
            QString("action \"StartAP\""));
    }

    delMenuItem(interfaceName, QString("%1_dummy").arg(interfaceName));
}

// Remove and re-add the main menu entries (= Network interfaces)
void LcdClient::updateMainMenuEntries()
{
    QStringList currentInterfaces;
    QString interfaceName;
    const Device::List deviceList = NetworkManager::networkInterfaces();
    const Connection::List conList = NetworkManager::listConnections();

    // Add a dummy entry to the menu in order to not
    // have an empty client menu that would kick the user out of it
    addMenuItem("", "_dummy", "action \"No interfaces :(\"");

    // To have the string representation of Device::State
    QMetaEnum metaEnum = QMetaEnum::fromType<Device::State>();

    currentInterfaces.clear();

    // Filter the ones that are of interest here
    // and add them to our client's menu
    for (Device::Ptr dev : deviceList) {
        if (
            ((dev->type() != Device::Wifi) && (dev->type() != Device::Ethernet)) ||
            (dev->state() == Device::Unmanaged)
        ) {
            continue;
        }

        interfaceName = dev->interfaceName();

        // If the entry is already in the menu, simply update it
        if (menuEntries["_"].contains(interfaceName)) {
            currentInterfaces.append(interfaceName);
            lcdSocket.write(QString("menu_set_item \"\" \"%1\" -text \"%1(%2)\"\n")
                .arg(interfaceName)
                .arg(metaEnum.valueToKey(dev->state()))
                .toLatin1());
        } else {
            currentInterfaces.append(interfaceName);
            addMenuItem("", interfaceName, QString("menu \"%1(%2)\"\n")
                .arg(interfaceName)
                .arg(metaEnum.valueToKey(dev->state()))
                .toLatin1());
        }
    }

    // Remove all interfaces from the menu, that are not in currentInterfaces
    // (if an interface dissappeared since the last call to this function)
    QString entry;
    foreach(entry, menuEntries["_"]) {
        if ((entry != "_dummy") && (!currentInterfaces.contains(entry))) {
            delMenuItem("", entry);
        }
    }

    // Remove the DUMMY entry again IF there are any interfaces. Otherwise,
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

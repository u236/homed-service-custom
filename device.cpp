#include "controller.h"
#include "device.h"
#include "expose.h"
#include "logger.h"

DeviceList::DeviceList(QSettings *config, QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
    m_databaseFile.setFileName(config->value("device/database", "/opt/homed-custom/database.json").toString());
    m_propertiesFile.setFileName(config->value("device/properties", "/opt/homed-custom/properties.json").toString());

    m_specialExposes = {"light", "switch", "cover", "lock", "thermostat"};

    connect(m_timer, &QTimer::timeout, this, &DeviceList::writeProperties);
    m_timer->setSingleShot(true);
}

DeviceList::~DeviceList(void)
{
    writeProperties();
}

Device DeviceList::byName(const QString &name)
{
    for (auto it = begin(); it != end(); it++)
        if (it.value()->name() == name)
            return it.value();

    return value(name);
}

void DeviceList::init(void)
{
    QJsonObject json;

    if (!m_databaseFile.open(QFile::ReadOnly))
        return;

    json = QJsonDocument::fromJson(m_databaseFile.readAll()).object();
    unserializeDevices(json.value("devices").toArray());

    m_databaseFile.close();

    if (!m_propertiesFile.open(QFile::ReadOnly))
        return;

    unserializeProperties(QJsonDocument::fromJson(m_propertiesFile.readAll()).object());
    m_propertiesFile.close();
}

void DeviceList::storeDatabase(void)
{
    QJsonArray devices;

    for (auto it = begin(); it != end(); it++)
    {
        QJsonObject json = {{"id", it.value()->id()}};

        if (it.value()->name() != it.value()->id())
            json.insert("name", it.value()->name());

        devices.append(json);
    }

    emit statusUpdated(QJsonObject {{"devices", devices}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}});
}

void DeviceList::storeProperties(void)
{
    m_timer->start(STORE_PROPERTIES_DELAY);
}

void DeviceList::unserializeDevices(const QJsonArray &devices)
{
    quint16 count = 0;

    for (auto it = devices.begin(); it != devices.end(); it++)
    {
        QJsonObject json = it->toObject();
        QJsonArray exposes = json.value("exposes").toArray();

        if (json.contains("id") && !exposes.isEmpty())
        {
            QString id = json.value("id").toString();
            Device device(new DeviceObject(id, json.value("name").toString()));
            Endpoint endpoint(new EndpointObject(DEFAULT_ENDPOINT, device));

            device->setReal(json.value("real").toBool());
            device->options() = json.value("options").toObject().toVariantMap();
            device->endpoints().insert(endpoint->id(), endpoint);

            for (auto it = exposes.begin(); it != exposes.end(); it++)
            {
                QString name = it->toString();
                QMap <QString, QVariant> option = device->options().value(name).toMap();
                Expose expose;
                int type;

                type = QMetaType::type(QString(m_specialExposes.contains(name) ? name : option.value("type").toString()).append("Expose").toUtf8());

                expose = Expose(type ? reinterpret_cast <ExposeObject*> (QMetaType::create(type)) : new ExposeObject(name));
                expose->setName(name);
                expose->setParent(endpoint.data());

                endpoint->exposes().append(expose);
            }

            insert(id, device);
            count++;
        }
    }

    if (count)
        logInfo << count << "devices loaded";
}

void DeviceList::unserializeProperties(const QJsonObject &properties)
{
    bool check = false;

    for (auto it = begin(); it != end(); it++)
    {
        const Endpoint &endpoint = it.value()->endpoints().value(DEFAULT_ENDPOINT);

        if (!properties.contains(it.value()->id()))
            continue;

        endpoint->properties() = properties.value(it.value()->id()).toObject().toVariantMap();
        check = true;
    }

    if (check)
        logInfo << "Properties restored";
}

void DeviceList::writeProperties(void)
{
    QJsonObject json;
    QByteArray data;
    bool check = true;

    for (auto it = begin(); it != end(); it++)
    {
        const Endpoint &endpoint = it.value()->endpoints().value(DEFAULT_ENDPOINT);

        if (endpoint->properties().isEmpty())
            continue;

        json.insert(it.value()->id(), QJsonObject::fromVariantMap(endpoint->properties()));
    }

    data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    if (!m_propertiesFile.open(QFile::WriteOnly))
    {
        logWarning << "Properties not stored, file" << m_propertiesFile.fileName() << "open error:" << m_propertiesFile.errorString();
        return;
    }

    if (m_propertiesFile.write(data) != data.length())
    {
        logWarning << "Properties not stored, file" << m_propertiesFile.fileName() << "write error";
        check = false;
    }

    m_propertiesFile.close();

    if (check)
        system("sync");
}

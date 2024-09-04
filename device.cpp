#include "controller.h"
#include "device.h"
#include "expose.h"
#include "logger.h"

DeviceList::DeviceList(QSettings *config, QObject *parent) : QObject(parent), m_databaseTimer(new QTimer(this)), m_propertiesTimer(new QTimer(this)), m_names(false), m_sync(false)
{
    QFile file(config->value("device/expose", "/usr/share/homed-common/expose.json").toString());

    ExposeObject::registerMetaTypes();

    m_databaseFile.setFileName(config->value("device/database", "/opt/homed-custom/database.json").toString());
    m_propertiesFile.setFileName(config->value("device/properties", "/opt/homed-custom/properties.json").toString());

    if (file.open(QFile::ReadOnly))
    {
        m_exposeOptions = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        file.close();
    }

    m_specialExposes = {"light", "switch", "cover", "lock", "thermostat"};

    connect(m_databaseTimer, &QTimer::timeout, this, &DeviceList::writeDatabase);
    connect(m_propertiesTimer, &QTimer::timeout, this, &DeviceList::writeProperties);

    m_databaseTimer->setSingleShot(true);
    m_propertiesTimer->setSingleShot(true);
}

DeviceList::~DeviceList(void)
{
    m_sync = true;

    writeDatabase();
    writeProperties();
}

Device DeviceList::byName(const QString &name, int *index)
{
    for (int i = 0; i < count(); i++)
    {
        if (at(i)->id() != name && at(i)->name() != name)
            continue;

        if (index)
            *index = i;

        return at(i);
    }

    return Device();
}

Device DeviceList::parse(const QJsonObject &json)
{
    QString id = json.value("id").toString();
    QJsonArray exposes = json.value("exposes").toArray();
    Device device;
    Endpoint endpoint;

    if (!id.isEmpty() && !exposes.isEmpty())
    {
        device = Device(new DeviceObject(id, json.value("name").toString()));
        endpoint = Endpoint(new EndpointObject(DEFAULT_ENDPOINT, device));

        if (json.contains("active"))
            device->setActive(json.value("active").toBool());

        if (json.contains("discovery"))
            device->setDiscovery(json.value("discovery").toBool());

        if (json.contains("cloud"))
            device->setCloud(json.value("cloud").toBool());

        device->setNote(json.value("note").toString());
        device->setReal(json.value("real").toBool());
        device->options() = json.value("options").toObject().toVariantMap();
        device->endpoints().insert(endpoint->id(), endpoint);

        for (auto it = exposes.begin(); it != exposes.end(); it++)
        {
            QString exposeName = it->toString(), itemName = exposeName.split('_').value(0);
            QMap <QString, QVariant> option = m_exposeOptions.value(itemName).toMap();
            Expose expose;
            int type;

            if (device->options().contains(exposeName))
                option.insert(device->options().value(exposeName).toMap());

            if (!option.isEmpty())
                device->options().insert(exposeName, option);

            type = QMetaType::type(QString(m_specialExposes.contains(itemName) ? itemName : option.value("type").toString()).append("Expose").toUtf8());
            expose = Expose(type ? reinterpret_cast <ExposeObject*> (QMetaType::create(type)) : new ExposeObject(exposeName));
            expose->setName(exposeName);
            expose->setParent(endpoint.data());

            endpoint->exposes().append(expose);
        }
    }

    return device;
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

void DeviceList::storeDatabase(bool sync)
{
    m_sync = sync;
    m_databaseTimer->start(STORE_DATABASE_DELAY);
}

void DeviceList::storeProperties(void)
{
    m_propertiesTimer->start(STORE_PROPERTIES_DELAY);
}

void DeviceList::unserializeDevices(const QJsonArray &devices)
{
    quint16 count = 0;

    for (auto it = devices.begin(); it != devices.end(); it++)
    {
        QJsonObject json = it->toObject();
        Device device;

        if (!byName(json.value("id").toString()).isNull() || !byName(json.value("name").toString()).isNull())
            continue;

        device = parse(json);

        if (device.isNull())
            continue;

        append(device);
        count++;
    }

    if (count)
        logInfo << count << "devices loaded";
}

void DeviceList::unserializeProperties(const QJsonObject &properties)
{
    bool check = false;

    for (int i = 0; i < count(); i++)
    {
        const Device &device = at(i);
        const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        if (properties.contains(device->id()))
        {
            endpoint->properties() = properties.value(device->id()).toObject().toVariantMap();
            check = true;
        }
    }

    if (check)
        logInfo << "Properties restored";
}

QJsonArray DeviceList::serializeDevices(void)
{
    QJsonArray array;

    for (int i = 0; i < count(); i++)
    {
        const Device &device = at(i);
        const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);
        QJsonObject json, options;
        QJsonArray exposes;

        for (auto it = device->options().begin(); it != device->options().end(); it++)
        {
            QString expose = it.key().split('_').value(0);
            QMap <QString, QVariant> option, map;

            if (it.value().type() != QVariant::Map)
            {
                options.insert(it.key(), QJsonValue::fromVariant(it.value()));
                continue;
            }

            option = m_exposeOptions.value(expose).toMap();
            map = it.value().toMap();

            for (auto it = option.begin(); it != option.end(); it++)
                if (map.value(it.key()) == it.value())
                    map.remove(it.key());

            if (map.isEmpty())
                continue;

            options.insert(it.key(), QJsonObject::fromVariantMap(map));
        }

        for (int i = 0; i < endpoint->exposes().count(); i++)
            exposes.append(endpoint->exposes().at(i)->name());

        json.insert("id", device->id());
        json.insert("real", device->real());
        json.insert("active", device->active());
        json.insert("discovery", device->discovery());
        json.insert("cloud", device->cloud());

        if (device->name() != device->id())
            json.insert("name", device->name());

        if (!device->note().isEmpty())
            json.insert("note", device->note());

        if (!exposes.isEmpty())
            json.insert("exposes", exposes);

        if (!options.isEmpty())
            json.insert("options", options);

        array.append(json);
    }

    return array;
}

QJsonObject DeviceList::serializeProperties(void)
{
    QJsonObject json;

    for (int i = 0; i < count(); i++)
    {
        const Device &device = at(i);
        const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        if (endpoint->properties().isEmpty())
            continue;

        json.insert(device->id(), QJsonObject::fromVariantMap(endpoint->properties()));
    }

    return json;
}

bool DeviceList::writeFile(QFile &file, const QByteArray &data, bool sync)
{
    bool check = true;

    if (!file.open(QFile::WriteOnly))
    {
        logWarning << "File" << file.fileName() << "open error:" << file.errorString();
        return false;
    }

    if (file.write(data) != data.length())
    {
        logWarning << "File" << file.fileName() << "write error";
        check = false;
    }

    file.close();

    if (check && sync)
        system("sync");

    return check;
}

void DeviceList::writeDatabase(void)
{
    QJsonObject json = {{"devices", serializeDevices()}, {"names", m_names}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}};

    emit statusUpdated(json);

    if (!m_sync)
        return;

    m_sync = false;

    if (writeFile(m_databaseFile, QJsonDocument(json).toJson(QJsonDocument::Compact), true))
        return;

    logWarning << "Database not stored, file" << m_databaseFile.fileName() << "error:" << m_databaseFile.errorString();
}

void DeviceList::writeProperties(void)
{
    QJsonObject json = serializeProperties();

    if (writeFile(m_propertiesFile, QJsonDocument(json).toJson(QJsonDocument::Compact)))
        return;

    logWarning << "Properties not stored, file" << m_propertiesFile.fileName() << "error:" << m_propertiesFile.errorString();
}

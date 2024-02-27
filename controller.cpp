#include <QFile>
#include <QMimeDatabase>
#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_timer(new QTimer(this)), m_devices(new DeviceList(getConfig(), this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_haPrefix = getConfig()->value("homeassistant/prefix", "homeassistant").toString();
    m_haStatus = getConfig()->value("homeassistant/status", "homeassistant/status").toString();
    m_haEnabled = getConfig()->value("homeassistant/enabled", false).toBool();

    connect(m_timer, &QTimer::timeout, this, &Controller::updateProperties);
    connect(m_devices, &DeviceList::statusUpdated, this, &Controller::statusUpdated);

    m_timer->setSingleShot(true);

    m_devices->setNames(getConfig()->value("mqtt/names", false).toBool());
    m_devices->init();
}

void Controller::publishExposes(DeviceObject *device, bool remove)
{
    device->publishExposes(this, device->id(), QString("%1_%2").arg(uniqueId(), device->id()), m_haPrefix, m_haEnabled, m_devices->names(), remove);

    if (remove)
        return;

    if (!device->real() && device->active())
        mqttPublish(mqttTopic("device/custom/%1").arg(m_devices->names() ? device->name() : device->id()), {{"status", "online"}}, true);

    m_timer->start(UPDATE_PROPERTIES_DELAY);
}

void Controller::publishProperties(const Device &device)
{
    const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

    if (endpoint->properties().isEmpty())
        return;

    mqttPublish(mqttTopic("fd/custom/%1").arg(m_devices->names() ? device->name() : device->id()), QJsonObject::fromVariantMap(endpoint->properties()), device->options().value("retain").toBool());
}

void Controller::publishEvent(const QString &name, Event event)
{
    mqttPublish(mqttTopic("event/custom"), {{"device", name}, {"event", m_events.valueToKey(static_cast <int> (event))}});
}

void Controller::deviceEvent(DeviceObject *device, Event event)
{
    bool check = true, remove = false;

    switch (event)
    {
        case Event::aboutToRename:
        case Event::removed:
            mqttPublish(mqttTopic("device/custom/%1").arg(m_devices->names() ? device->name() : device->id()), QJsonObject(), true);
            remove = true;
            break;

        case Event::added:
        case Event::updated:
            break;

        default:
            check = false;
            break;
    }

    if (check)
        publishExposes(device, remove);

    publishEvent(device->name(), event);
}

void Controller::quit(void)
{
    for (int i = 0; i < m_devices->count(); i++)
    {
        const Device &device = m_devices->at(i);

        if (device->real() && !device->active())
            continue;

        mqttPublish(mqttTopic("device/custom/%1").arg(m_devices->names() ? device->name() : device->id()), {{"status", "offline"}}, true);
    }

    HOMEd::quit();
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("command/custom"));
    mqttSubscribe(mqttTopic("fd/custom/#"));
    mqttSubscribe(mqttTopic("td/custom/#"));

    for (int i = 0; i < m_devices->count(); i++)
        publishExposes(m_devices->at(i).data());

    if (m_haEnabled)
    {
        mqttPublishDiscovery("Custom", SERVICE_VERSION, m_haPrefix);
        mqttSubscribe(m_haStatus);
    }

    m_devices->storeDatabase();
    mqttPublishStatus();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == "command/custom")
    {
        switch (static_cast <Command> (m_commands.keyToValue(json.value("action").toString().toUtf8().constData())))
        {
            case Command::restartService:
            {
                logWarning << "Restart request received...";
                mqttPublish(mqttTopic("command/custom"), QJsonObject(), true);
                QCoreApplication::exit(EXIT_RESTART);
                break;
            }

            case Command::updateDevice:
            {
                int index = -1;
                QJsonObject data = json.value("data").toObject();
                QString id = data.value("id").toString().trimmed(), name = data.value("name").toString().trimmed();
                Device device = m_devices->byName(json.value("device").toString(), &index), other = m_devices->byName(id);
                QMap <QString, QVariant> properies;

                if (device != other && !other.isNull())
                {
                    logWarning << "Device" << id << "update failed, identifier already in use";
                    publishEvent(name, Event::idDuplicate);
                    break;
                }

                other = m_devices->byName(name);

                if (device != other && !other.isNull())
                {
                    logWarning << "Device" << name << "update failed, name already in use";
                    publishEvent(name, Event::nameDuplicate);
                    break;
                }

                if (!device.isNull())
                {
                    if (device->id() != id || device->name() != name)
                        deviceEvent(device.data(), Event::aboutToRename);

                    properies = device->endpoints().value(DEFAULT_ENDPOINT)->properties();
                }

                device = m_devices->parse(data);

                if (device.isNull())
                {
                    logWarning << "Device" << name << "update failed, data is incomplete";
                    publishEvent(name, Event::incompleteData);
                    break;
                }

                if (index >= 0)
                {
                    device->endpoints().value(DEFAULT_ENDPOINT)->properties() = properies;
                    m_devices->replace(index, device);
                    logInfo << "Device" << device->name() << "successfully updated";
                    deviceEvent(device.data(), Event::updated);
                }
                else
                {
                    m_devices->append(device);
                    logInfo << "Device" << device->name() << "successfully added";
                    deviceEvent(device.data(), Event::added);
                }

                m_devices->storeDatabase(true);
                m_devices->storeProperties();
                break;
            }

            case Command::removeDevice:
            {
                int index = -1;
                const Device &device = m_devices->byName(json.value("device").toString(), &index);

                if (index >= 0)
                {
                    m_devices->removeAt(index);
                    logInfo << "Device" << device->name() << "removed";
                    deviceEvent(device.data(), Event::removed);
                    m_devices->storeDatabase(true);
                    m_devices->storeProperties();
                }

                break;
            }

            case Command::getProperties:
            {
                Device device = m_devices->byName(json.value("device").toString());

                if (!device.isNull() && device->active())
                    publishProperties(device);

                break;
            }
        }
    }
    else if (subTopic.startsWith("fd/custom/"))
    {
        QList <QString> list = subTopic.split('/');
        Device device = m_devices->byName(list.value(2));
        Endpoint endpoint;

        if (device.isNull() || !device->real() || !device->active())
            return;

        endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        for (auto it = json.begin(); it != json.end(); it++)
        {
            if (it.value().isNull())
            {
                endpoint->properties().remove(it.key());
                continue;
            }

            endpoint->properties().insert(it.key(), it.value().toVariant());
        }

        m_devices->storeProperties();
    }
    else if (subTopic.startsWith("td/custom/"))
    {
        QList <QString> list = subTopic.split('/');
        Device device = m_devices->byName(list.value(2));
        Endpoint endpoint;

        if (device.isNull() || device->real() || !device->active())
            return;

        endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        for (auto it = json.begin(); it != json.end(); it++)
        {
            QList <QString> list = it.key().split('_');

            if (it.value().isNull())
            {
                endpoint->properties().remove(it.key());
                continue;
            }

            if (list.value(0) == "status" && it.value() == "toggle")
            {
                endpoint->properties().insert(it.key(), endpoint->properties().value(it.key()).toString() == "on" ? "off" : "on");
                continue;
            }

            endpoint->properties().insert(it.key(), it.value().toVariant());
        }

        m_devices->storeProperties();
        publishProperties(device);
    }
    else if (topic.name() == m_haStatus)
    {
        if (message != "online")
            return;

        m_timer->start(UPDATE_PROPERTIES_DELAY);
    }
}

void Controller::updateProperties(void)
{
    for (int i = 0; i < m_devices->count(); i++)
    {
        const Device &device = m_devices->at(i);

        if (!device->active())
            continue;

        publishProperties(device);
    }
}

void Controller::statusUpdated(const QJsonObject &json)
{
    mqttPublish(mqttTopic("status/custom"), json, true);
}

#include <QFile>
#include <QMimeDatabase>
#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_timer(new QTimer(this)), m_devices(new DeviceList(getConfig(), this)), m_events(QMetaEnum::fromType <Event> ())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_timer->setSingleShot(true);

    connect(m_timer, &QTimer::timeout, this, &Controller::updateProperties);
    connect(m_devices, &DeviceList::statusUpdated, this, &Controller::statusUpdated);

    m_devices->init();

    m_names = getConfig()->value("mqtt/names", false).toBool();
    m_haStatus = getConfig()->value("homeassistant/status", "homeassistant/status").toString();
}

void Controller::publishExposes(DeviceObject *device, bool remove)
{
    device->publishExposes(this, device->id(), device->id(), remove);

    if (remove)
        return;

    if (device->active() && !device->real())
        mqttPublish(mqttTopic("device/custom/%1").arg(m_names ? device->name() : device->id()), {{"status", "online"}}, true);

    m_timer->start(UPDATE_PROPERTIES_DELAY);
}

void Controller::publishProperties(const Device &device)
{
    const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

    if (endpoint->properties().isEmpty())
        return;

    mqttPublish(mqttTopic("fd/custom/%1").arg(m_names ? device->name() : device->id()), QJsonObject::fromVariantMap(endpoint->properties()), device->options().value("retain").toBool());
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
            mqttPublish(mqttTopic("device/custom/%1").arg(m_names ? device->name() : device->id()), QJsonObject(), true);
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

        if (!device->active() && device->real())
            continue;

        mqttPublish(mqttTopic("device/custom/%1").arg(m_names ? device->name() : device->id()), {{"status", "offline"}}, true);
    }

    HOMEd::quit();
}

void Controller::mqttConnected(void)
{
    logInfo << "MQTT connected";

    mqttSubscribe(mqttTopic("command/custom"));
    mqttSubscribe(mqttTopic("td/custom/#"));

    if (getConfig()->value("homeassistant/enabled", false).toBool())
        mqttSubscribe(m_haStatus);

    for (int i = 0; i < m_devices->count(); i++)
        publishExposes(m_devices->at(i).data());

    m_devices->storeDatabase();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == "command/custom")
    {
        QString action = json.value("action").toString();

        if (action == "getProperties")
        {
            Device device = m_devices->byName(json.value("device").toString());

            if (!device.isNull() && device->active())
                publishProperties(device);

            return;
        }
        else if (action == "updateDevice")
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
                return;
            }

            other = m_devices->byName(name);

            if (device != other && !other.isNull())
            {
                logWarning << "Device" << name << "update failed, name already in use";
                publishEvent(name, Event::nameDuplicate);
                return;
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
                return;
            }

            if (index < 0)
            {
                m_devices->append(device);
                logInfo << "Device" << device->name() << "successfully added";
                deviceEvent(device.data(), Event::added);
            }
            else
            {
                device->endpoints().value(DEFAULT_ENDPOINT)->properties() = properies;
                m_devices->replace(index, device);
                logInfo << "Device" << device->name() << "successfully updated";
                deviceEvent(device.data(), Event::updated);
            }
        }
        else if (action == "removeDevice")
        {
            int index = -1;
            const Device &device = m_devices->byName(json.value("device").toString(), &index);

            if (index < 0)
                return;

            m_devices->removeAt(index);
            logInfo << "Device" << device->name() << "removed";
            deviceEvent(device.data(), Event::removed);
        }

        m_devices->storeDatabase(true);
        m_devices->storeProperties();
    }
    else if (subTopic.startsWith("td/custom/"))
    {
        QList <QString> list = subTopic.split('/');
        Device device = m_devices->byName(list.value(2));
        Endpoint endpoint;

        if (device.isNull() || !device->active() || device->real())
            return;

        endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        for (auto it = json.begin(); it != json.end(); it++)
        {
            if (it.value().isNull())
            {
                endpoint->properties().remove(it.key());
                continue;
            }

            if (it.key() == "status" && it.value() == "toggle")
            {
                endpoint->properties().insert("status", endpoint->properties().value("status").toString() == "on" ? "off" : "on");
                continue;
            }

            endpoint->properties().insert(it.key(), it.value().toVariant());
        }

        publishProperties(device);
        m_devices->storeProperties();
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
    for (int i = 0; i <  m_devices->count(); i++)
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

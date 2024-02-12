#include <QFile>
#include <QMimeDatabase>
#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_timer(new QTimer(this)), m_devices(new DeviceList(getConfig(), this))
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    connect(m_timer, &QTimer::timeout, this, &Controller::updateProperties);
    connect(m_devices, &DeviceList::statusUpdated, this, &Controller::statusUpdated);

    m_devices->init();

    m_names = getConfig()->value("mqtt/names", false).toBool();
    m_haStatus = getConfig()->value("homeassistant/status", "homeassistant/status").toString();
}

void Controller::publishProperties(const Device &device)
{
    const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

    if (endpoint->properties().isEmpty())
        return;

    mqttPublish(mqttTopic("fd/custom/%1").arg(m_names ? device->name() : device->id()), QJsonObject::fromVariantMap(endpoint->properties()), device->options().value("retain").toBool());
}

void Controller::quit(void)
{
    for (auto it = m_devices->begin(); it != m_devices->end(); it++)
        if (!it.value()->real())
            mqttPublish(mqttTopic("device/custom/%1").arg(m_names ? it.value()->name() : it.value()->id()), {{"status", "offline"}}, true);

    HOMEd::quit();
}

void Controller::mqttConnected(void)
{
    logInfo << "MQTT connected";

    mqttSubscribe(mqttTopic("command/custom"));
    mqttSubscribe(mqttTopic("td/custom/#"));

    if (getConfig()->value("homeassistant/enabled", false).toBool())
        mqttSubscribe(m_haStatus);

    for (auto it = m_devices->begin(); it != m_devices->end(); it++)
    {
        if (!it.value()->real())
            mqttPublish(mqttTopic("device/custom/%1").arg(m_names ? it.value()->name() : it.value()->id()), {{"status", "online"}}, true);

        it.value()->publishExposes(this, it.value()->id(), it.value()->id());
    }

    m_devices->storeDatabase();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == "command/custom" && json.value("action").toString() == "getProperties")
    {
        Device device = m_devices->byName(json.value("device").toString());

        if (device.isNull() || device->real())
            return;

        publishProperties(device);
    }
    else if (subTopic.startsWith("td/custom/"))
    {
        QList <QString> list = subTopic.split('/');
        Device device = m_devices->byName(list.value(2));
        Endpoint endpoint;

        if (device.isNull() || device->real())
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
    for (auto it = m_devices->begin(); it != m_devices->end(); it++)
        publishProperties(it.value());
}

void Controller::statusUpdated(const QJsonObject &json)
{
    mqttPublish(mqttTopic("status/custom"), json, true);
}

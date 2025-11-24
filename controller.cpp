#include <QMimeDatabase>
#include "controller.h"
#include "logger.h"
#include "parser.h"

Controller::Controller(const QString &configFile) : HOMEd(SERVICE_VERSION, configFile, true), m_timer(new QTimer(this)), m_devices(new DeviceList(getConfig(), this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ())
{
    m_haPrefix = getConfig()->value("homeassistant/prefix", "homeassistant").toString();
    m_haStatus = getConfig()->value("homeassistant/status", "homeassistant/status").toString();
    m_haEnabled = getConfig()->value("homeassistant/enabled", false).toBool();

    connect(m_timer, &QTimer::timeout, this, &Controller::updateProperties);
    connect(m_devices, &DeviceList::statusUpdated, this, &Controller::statusUpdated);
    connect(m_devices, &DeviceList::devicetUpdated, this, &Controller::devicetUpdated);
    connect(m_devices, &DeviceList::addSubscription, this, &Controller::addSubscription);

    m_timer->setSingleShot(true);

    m_devices->setNames(getConfig()->value("mqtt/names", false).toBool());
    m_devices->init();
}

void Controller::publishExposes(DeviceObject *device, bool remove)
{
    device->publishExposes(this, device->id(), QString("%1_%2").arg(uniqueId(), device->id().remove(':')), m_haPrefix, m_haEnabled, m_devices->names(), remove);

    if (remove)
        return;

    mqttPublish(mqttTopic("device/%1/%2").arg(serviceTopic(), m_devices->names() ? device->name() : device->id()), {{"status", device->active() && (!device->real() || device->availabilityTopic().isEmpty()) ? "online" : "offline"}}, true);
    m_timer->start(UPDATE_PROPERTIES_DELAY);
}

void Controller::publishProperties(DeviceObject *device)
{
    const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

    if (endpoint->properties().isEmpty())
        return;

    mqttPublish(mqttTopic("fd/%1/%2").arg(serviceTopic(), m_devices->names() ? device->name() : device->id()), QJsonObject::fromVariantMap(endpoint->properties()), device->options().value("retain").toBool());
}

void Controller::publishEvent(const QString &name, Event event)
{
    mqttPublish(mqttTopic("event/%1").arg(serviceTopic()), {{"device", name}, {"event", m_events.valueToKey(static_cast <int> (event))}});
}

void Controller::deviceEvent(DeviceObject *device, Event event)
{
    bool check = true, remove = false;

    switch (event)
    {
        case Event::aboutToRename:
        case Event::removed:
            mqttPublish(mqttTopic("device/%1/%2").arg(serviceTopic(), m_devices->names() ? device->name() : device->id()), QJsonObject(), true);
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

QVariant Controller::parsePattern(QString string, const QVariant &data)
{
    QRegExp replace("\\{\\{[^\\{\\}]*\\}\\}"), split("\\s+(?=(?:[^']*['][^']*['])*[^']*$)");
    QList <QString> operatorList = {"is", "==", "!=", ">", ">=", "<", "<="};
    int position;

    if (string.isEmpty())
        return Parser::stringValue(data.toString());

    while ((position = replace.indexIn(string)) != -1)
    {
        QString capture = replace.cap(), value = capture.mid(2, capture.length() - 4).trimmed();
        QList <QString> list = value.split(split, Qt::SkipEmptyParts);
        double number;

        for (int i = 0; i < list.count(); i++)
        {
            QString item = list.at(i);
            QVariant value;
            bool check;

            if (item.startsWith('\'') && item.endsWith('\''))
                item = item.mid(1, item.length() - 2);

            if (item.startsWith('\\'))
                value = item.mid(1);
            else if (item.startsWith("format."))
                value = Parser::formatValue(item.mid(item.indexOf('.') + 1));
            else if (item.startsWith("json."))
                value = Parser::jsonValue(data.toString().toUtf8(), item.mid(item.indexOf('.') + 1));
            else if (item.startsWith("url."))
                value = Parser::urlValue(data.toString().toUtf8(), item.mid(item.indexOf('.') + 1));
            else if (item.startsWith("xml."))
                value = Parser::xmlValue(data.toString().toUtf8(), item.mid(item.indexOf('.') + 1));
            else if (item == "value")
                value = data;
            else
                value = item;

            item = value.type() == QVariant::List ? value.toStringList().join(',') : value.toString();

            if (item == list.at(i))
                continue;

            item.toDouble(&check);
            list.replace(i, check ? item : QString("'%1'").arg(item));
        }

        number = Expression(list.join(0x20)).result();

        if (!isnan(number))
        {
            string.replace(position, capture.length(), QString::number(number, 'f').remove(QRegExp("0+$")).remove(QRegExp("\\.$")));
            continue;
        }

        for (int i = 0; i < list.count(); i++)
        {
            QString item = list.at(i);

            if (!item.startsWith('\'') || !item.endsWith('\''))
                continue;

            list.replace(i, item.mid(1, item.length() - 2));
        }

        while (list.count() >= 7 && list.at(1) == "if" && list.at(5) == "else")
        {
            bool check = false;

            switch (operatorList.indexOf(list.at(3)))
            {
                case 0: check = list.at(4) == "defined" ? !list.at(2).isEmpty() : list.at(4) == "undefined" ? list.at(2).isEmpty() : false; break;
                case 1: check = list.at(2) == list.at(4); break;
                case 2: check = list.at(2) != list.at(4); break;
                case 3: check = list.at(2).toDouble() > list.at(4).toDouble(); break;
                case 4: check = list.at(2).toDouble() >= list.at(4).toDouble(); break;
                case 5: check = list.at(2).toDouble() < list.at(4).toDouble(); break;
                case 6: check = list.at(2).toDouble() <= list.at(4).toDouble(); break;
            }

            list = check ? list.mid(0, 1) : list.mid(6);
        }

        string.replace(position, capture.length(), list.join(0x20));
    }

    return string != "_NULL_" ? Parser::stringValue(string) : QVariant();
}

void Controller::quit(void)
{
    for (int i = 0; i < m_devices->count(); i++)
    {
        const Device &device = m_devices->at(i);
        mqttPublish(mqttTopic("device/%1/%2").arg(serviceTopic(), m_devices->names() ? device->name() : device->id()), {{"status", "offline"}}, true);
    }

    HOMEd::quit();
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("command/%1").arg(serviceTopic()));
    mqttSubscribe(mqttTopic("fd/%1/#").arg(serviceTopic()));
    mqttSubscribe(mqttTopic("td/%1/#").arg(serviceTopic()));

    for (int i = 0; i < m_devices->count(); i++)
        publishExposes(m_devices->at(i).data());

    for (int i = 0; i < m_subscriptions.count(); i++)
    {
        logInfo << "MQTT subscribed to" << m_subscriptions.at(i);
        mqttSubscribe(m_subscriptions.at(i));
    }

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
    QString subTopic = topic.name().replace(0, mqttTopic().length(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (m_subscriptions.contains(topic.name()))
    {
        for (int i = 0; i < m_devices->count(); i++)
        {
            const Device &device = m_devices->at(i);
            const Endpoint &endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

            if (!device->active() || !device->real())
                continue;

            for (auto it = endpoint->bindings().begin(); it != endpoint->bindings().end(); it++)
            {
                QVariant value;

                if (it.value()->inTopic() != topic.name())
                    continue;

                value = parsePattern(it.value()->inPattern(), message);

                if (it.key().split('_').value(0) == "color")
                {
                    QList <QString> list = value.toString().split(',');
                    QJsonArray array;

                    for (int i = 0; i < list.count(); i++)
                        array.append(QJsonValue::fromVariant(Parser::stringValue(list.at(i).trimmed())));

                    value = array;
                }

                if (!value.isValid() || endpoint->properties().value(it.key()) == value)
                    continue;

                endpoint->properties().insert(it.key(), value);
                device->timer()->start(UPDATE_DEVICE_DELAY);
                m_devices->storeProperties();
            }

            if (device->availabilityTopic() != topic.name())
                continue;

            mqttPublish(mqttTopic("device/%1/%2").arg(serviceTopic(), m_devices->names() ? device->name() : device->id()), {{"status", parsePattern(device->availabilityPattern(), message).toString() == "online" ? "online" : "offline"}}, true);
        }
    }

    if (subTopic == QString("command/%1").arg(serviceTopic()))
    {
        switch (static_cast <Command> (m_commands.keyToValue(json.value("action").toString().toUtf8().constData())))
        {
            case Command::restartService:
            {
                logWarning << "Restart request received...";
                mqttPublish(topic.name(), QJsonObject(), true);
                QCoreApplication::exit(EXIT_RESTART);
                break;
            }

            case Command::updateDevice:
            {
                int index = -1;
                QJsonObject data = json.value("data").toObject();
                QString id = mqttSafe(data.value("id").toString()), name = mqttSafe(data.value("name").toString());
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
                    logInfo << device << "successfully updated";
                    deviceEvent(device.data(), Event::updated);
                }
                else
                {
                    m_devices->append(device);
                    logInfo << device << "successfully added";
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
                    logInfo << device << "removed";
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
                    publishProperties(device.data());

                break;
            }
        }
    }
    else if (subTopic.startsWith(QString("fd/%1/").arg(serviceTopic())))
    {
        QList <QString> list = subTopic.remove(QString("fd/%1/").arg(serviceTopic())).split('/');
        Device device = m_devices->byName(list.value(0));
        Endpoint endpoint;

        if (device.isNull() || !device->active() || !device->real())
            return;

        endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        if (!endpoint->bindings().isEmpty())
            return;

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
    else if (subTopic.startsWith(QString("td/%1/").arg(serviceTopic())))
    {
        QList <QString> list = subTopic.remove(QString("td/%1/").arg(serviceTopic())).split('/');
        Device device = m_devices->byName(list.value(0));
        Endpoint endpoint;

        if (device.isNull() || !device->active())
            return;

        endpoint = device->endpoints().value(DEFAULT_ENDPOINT);

        for (auto it = json.begin(); it != json.end(); it++)
        {
            QVariant value = it.value().toVariant();

            if (it.key().split('_').value(0) == "status" && value.toString() == "toggle")
                value = endpoint->properties().value(it.key()).toString() == "on" ? "off" : "on";

            if (device->real())
            {
                const Binding &binding = endpoint->bindings().value(it.key());

                if (!binding.isNull() && !binding->outTopic().isEmpty())
                    mqttPublishString(binding->outTopic(), parsePattern(binding->outPattern(), value).toString(), binding->retain());

                continue;
            }

            if (value.isNull())
            {
                endpoint->properties().remove(it.key());
                continue;
            }

            endpoint->properties().insert(it.key(), value);
        }

        if (device->real())
            return;

        device->timer()->start(UPDATE_DEVICE_DELAY);
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
    for (int i = 0; i < m_devices->count(); i++)
    {
        const Device &device = m_devices->at(i);

        if (!device->active())
            continue;

        publishProperties(device.data());
    }
}

void Controller::statusUpdated(const QJsonObject &json)
{
    mqttPublish(mqttTopic("status/%1").arg(serviceTopic()), json, true);
}

void Controller::devicetUpdated(DeviceObject *device)
{
    publishProperties(device);
}

void Controller::addSubscription(const QString &topic, bool resubscribe)
{
    if (m_subscriptions.contains(topic))
    {
        if (!resubscribe)
            return;

        m_subscriptions.removeAll(topic);
        mqttUnsubscribe(topic);
    }

    m_subscriptions.append(topic);

    if (!mqttStatus())
        return;

    QTimer::singleShot(SUBSCRIPTION_DELAY, this, [this, topic] () { logInfo << "MQTT subscribed to" << topic; mqttSubscribe(topic); });
}

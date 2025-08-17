#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION             "2.1.0"
#define UPDATE_DEVICE_DELAY         100
#define UPDATE_PROPERTIES_DELAY     1000

#include <QMetaEnum>
#include "device.h"
#include "homed.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    enum class Command
    {
        restartService,
        updateDevice,
        removeDevice,
        getProperties
    };

    enum class Event
    {
        idDuplicate,
        nameDuplicate,
        incompleteData,
        aboutToRename,
        added,
        updated,
        removed
    };

    Controller(const QString &configFile);

    Q_ENUM(Command)
    Q_ENUM(Event)

private:

    QTimer *m_timer;
    DeviceList *m_devices;

    QMetaEnum m_commands, m_events;
    QString m_haPrefix, m_haStatus;
    bool m_haEnabled;

    QList <QString> m_subscriptions;

    void publishExposes(DeviceObject *device, bool remove = false);
    void publishProperties(DeviceObject *device);
    void publishEvent(const QString &name, Event event);
    void deviceEvent(DeviceObject *device, Event event);

    QVariant parsePattern(QString string, const QVariant &data);

public slots:

    void quit(void) override;

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void updateProperties(void);

    void statusUpdated(const QJsonObject &json);
    void devicetUpdated(DeviceObject *device);
    void addSubscription(const QString &topic);

};

#endif

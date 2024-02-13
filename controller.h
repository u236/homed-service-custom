#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION             "1.0.2"
#define UPDATE_PROPERTIES_DELAY     1000

#include <QMetaEnum>
#include "device.h"
#include "homed.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

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

    Q_ENUM(Event)

private:

    QTimer *m_timer;
    DeviceList *m_devices;

    QMetaEnum m_events;
    QString m_haStatus;

    void publishExposes(DeviceObject *device, bool remove = false);
    void publishProperties(const Device &device);
    void publishEvent(const QString &name, Event event);
    void deviceEvent(DeviceObject *device, Event event);

public slots:

    void quit(void) override;

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void updateProperties(void);
    void statusUpdated(const QJsonObject &json);

};

#endif

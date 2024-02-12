#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.0.0"
#define UPDATE_PROPERTIES_DELAY     1000

#include "device.h"
#include "homed.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    QTimer *m_timer;
    DeviceList *m_devices;

    bool m_names;
    QString m_haStatus;

    void publishProperties(const Device &device);

public slots:

    void quit(void) override;

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void updateProperties(void);
    void statusUpdated(const QJsonObject &json);

};

#endif

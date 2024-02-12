#ifndef DEVICE_H
#define DEVICE_H

#define DEFAULT_ENDPOINT            0
#define STORE_PROPERTIES_DELAY      1000

#include <QFile>
#include "endpoint.h"

class EndpointObject : public AbstractEndpointObject
{

public:

    EndpointObject(quint8 id, Device device) :
        AbstractEndpointObject(id, device) {}

    inline  QMap <QString, QVariant> &properties(void) { return m_properties; }

private:

    QMap <QString, QVariant> m_properties;

};

class DeviceObject : public AbstractDeviceObject
{

public:

    DeviceObject(const QString &id, const QString name = QString()) :
        AbstractDeviceObject(name.isEmpty() ? id : name), m_id(id), m_real(false) {}

    inline QString id(void) { return m_id; }

    inline bool real(void) { return m_real; }
    inline void setReal(bool value) { m_real = value; }

private:

    QString m_id;
    bool m_real;

};

class DeviceList : public QObject, public QMap <QString, Device>
{
    Q_OBJECT

public:

    DeviceList(QSettings *config, QObject *parent);
    ~DeviceList(void);

    void init(void);
    void storeDatabase(void);
    void storeProperties(void);

    Device byName(const QString &name);

private:

    QTimer *m_timer;

    QFile m_databaseFile, m_propertiesFile;
    QList <QString> m_specialExposes;

    void unserializeDevices(const QJsonArray &devices);
    void unserializeProperties(const QJsonObject &properties);

private slots:

    void writeProperties(void);

signals:

    void statusUpdated(const QJsonObject &json);

};

#endif

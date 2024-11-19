#ifndef DEVICE_H
#define DEVICE_H

#define DEFAULT_ENDPOINT            0
#define STORE_DATABASE_DELAY        20
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
        AbstractDeviceObject(name.isEmpty() ? id : name), m_timer(new QTimer(this)), m_id(id), m_real(false) {}

    inline QTimer *timer(void) { return m_timer; }
    inline QString id(void) { return m_id; }

    inline bool real(void) { return m_real; }
    inline void setReal(bool value) { m_real = value; }

private:

    QTimer *m_timer;
    QString m_id;
    bool m_real;

};

class DeviceList : public QObject, public QList <Device>
{
    Q_OBJECT

public:

    DeviceList(QSettings *config, QObject *parent);
    ~DeviceList(void);

    inline bool names(void) { return m_names; }
    inline void setNames(bool value) { m_names = value; }

    void init(void);
    void storeDatabase(bool sync = false);
    void storeProperties(void);

    Device byName(const QString &name, int *index = nullptr);
    Device parse(const QJsonObject &json);

private:

    QTimer *m_databaseTimer, *m_propertiesTimer;

    QFile m_databaseFile, m_propertiesFile;
    bool m_names, m_sync;

    QMap <QString, QVariant> m_exposeOptions;
    QList <QString> m_specialExposes;

    void unserializeDevices(const QJsonArray &devices);
    void unserializeProperties(const QJsonObject &properties);

    QJsonArray serializeDevices(void);
    QJsonObject serializeProperties(void);

    bool writeFile(QFile &file, const QByteArray &data, bool sync = false);

private slots:

    void writeDatabase(void);
    void writeProperties(void);
    void deviceTimeout(void);

signals:

    void statusUpdated(const QJsonObject &json);
    void devicetUpdated(DeviceObject *device);

};

#endif

#ifndef DEVICE_H
#define DEVICE_H

#define DEFAULT_ENDPOINT            0
#define STORE_DATABASE_DELAY        20
#define STORE_PROPERTIES_DELAY      1000

#include "endpoint.h"

class BindingObject;
typedef QSharedPointer <BindingObject> Binding;

class BindingObject
{

public:

    BindingObject(const QString &inTopic, const QString &inPattern, const QString &outTopic, const QString &outPattern, bool retain) :
        m_inTopic(inTopic), m_inPattern(inPattern), m_outTopic(outTopic), m_outPattern(outPattern), m_retain(retain) {}

    inline QString inTopic(void) { return m_inTopic; }
    inline QString inPattern(void) { return m_inPattern; }
    inline QString outTopic(void) { return m_outTopic; }
    inline QString outPattern(void) { return m_outPattern; }

    inline bool retain(void) { return m_retain; }

private:

    QString m_inTopic, m_inPattern, m_outTopic, m_outPattern;
    bool m_retain;

};

class EndpointObject : public AbstractEndpointObject
{

public:

    EndpointObject(quint8 id, Device device) :
        AbstractEndpointObject(id, device) {}

    inline QMap <QString, Binding> &bindings(void) { return m_bindings; }
    inline QMap <QString, QVariant> &properties(void) { return m_properties; }

private:

    QMap <QString, Binding> m_bindings;
    QMap <QString, QVariant> m_properties;

};

class DeviceObject : public AbstractDeviceObject
{

public:

    DeviceObject(const QString &id, const QString &availabilityTopic, const QString &availabilityPattern, const QString name) :
        AbstractDeviceObject(name.isEmpty() ? id : name), m_timer(new QTimer(this)), m_id(id), m_availabilityTopic(availabilityTopic), m_availabilityPattern(availabilityPattern), m_real(false) {}

    inline QTimer *timer(void) { return m_timer; }

    inline QString id(void) { return m_id; }
    inline QString availabilityTopic(void) { return m_availabilityTopic; }
    inline QString availabilityPattern(void) { return m_availabilityPattern; }

    inline bool real(void) { return m_real; }
    inline void setReal(bool value) { m_real = value; }

private:

    QTimer *m_timer;
    QString m_id, m_availabilityTopic, m_availabilityPattern;
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

    bool writeFile(QFile &file, const QByteArray &data);

private slots:

    void writeDatabase(void);
    void writeProperties(void);
    void deviceTimeout(void);

signals:

    void statusUpdated(const QJsonObject &json);
    void devicetUpdated(DeviceObject *device);
    void addSubscription(const QString &topic, bool resubsctibe = false);

};

inline QDebug operator << (QDebug debug, const Device &device) { return debug << "device" << device->name(); }

#endif

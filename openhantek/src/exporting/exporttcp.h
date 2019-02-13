// SPDX-License-Identifier: GPL-2.0+

#pragma once
#include "exporterinterface.h"

class QTcpServer;
class QNetworkSession;
class QTcpSocket;

class ExporterTcp : public QObject, public ExporterInterface
{
    Q_OBJECT

public:
    ExporterTcp(int port, bool binary);
    virtual void create(ExporterRegistry *registry) override;
    virtual bool show() override;
    virtual QIcon icon() override;
    virtual QString name() override;
    virtual Type type() override;
    virtual bool samples(const std::shared_ptr<PPresult>data) override;
    virtual bool save() override;
    virtual float progress() override;

private slots:
    void onNewConnection();
    void onDisconnect();
    void onNewData(QTcpSocket* connection, QByteArray data);

signals:
    void newData(QTcpSocket* connection, QByteArray data);

private:
    QByteArray writeAsTextByteArray(const std::shared_ptr<PPresult>data);
    QByteArray writeAsBinaryByteArray(const std::shared_ptr<PPresult>data);
    bool changes(const std::shared_ptr<PPresult> data);

private:
    bool binary;
    QTcpServer *tcpServer = nullptr;
    QNetworkSession *networkSession = nullptr;
    QList<QTcpSocket*> connections;

    std::vector<double> lastSampleInterval;
    std::vector<size_t> lastSampleSize;
};

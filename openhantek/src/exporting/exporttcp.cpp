// SPDX-License-Identifier: GPL-2.0+

#include "exporttcp.h"
#include "exporterregistry.h"
#include "post/ppresult.h"
#include "settings.h"

#include <QtNetwork/QTcpServer>
#include <QtNetwork/QNetworkSession>
#include <QtNetwork/QTcpSocket>
#include <QApplication>
#include <QDateTime>

ExporterTcp::ExporterTcp(int port) {
    tcpServer = new QTcpServer();

    if (!tcpServer->listen(QHostAddress::Any, port))
    {
        qCritical() << "Unable to start TCP server!";
        return;
    }

    connect(tcpServer, &QTcpServer::newConnection, this, &ExporterTcp::onNewConnection);
    connect(this, &ExporterTcp::newData, this, &ExporterTcp::onNewData);
}

void ExporterTcp::create(ExporterRegistry *registry) { this->registry = registry; }

bool ExporterTcp::show() { return false; }

QIcon ExporterTcp::icon() { return QIcon(); }

QString ExporterTcp::name() { return QCoreApplication::tr("Export TCP"); }

ExporterInterface::Type ExporterTcp::type() { return Type::SnapshotExport; }

bool ExporterTcp::samples(const std::shared_ptr<PPresult> data) {
    for(QTcpSocket* connection: connections)
    {
        QByteArray block;
        QTextStream out(&block, QIODevice::WriteOnly);

        if (changes(data))
        {
            out << "#timestamp,";

            for (ChannelID channel = 0; channel < data->channelCount(); channel++)
            {
                size_t numChanSamps = data->data(channel)->voltage.sample.size();
                if (!numChanSamps) continue;
                QString chanName = registry->settings->scope.voltage[channel].name;

                out << chanName
                    << " sample rate,"
                    << "<"
                    << numChanSamps
                    << ' '
                    << chanName
                    << " samples>,";
            }

            out << '\n';
        }

        qint64 timestamp_ms = QDateTime::currentDateTime().toMSecsSinceEpoch();
        out << (timestamp_ms / 1000) << '.'
            << QString("%1").arg((timestamp_ms % 1000), 3, 10, QChar('0')) << ',';

        for (ChannelID channel = 0; channel < data->channelCount(); channel++)
        {
            const DataChannel* chanData = data->data(channel);
            size_t numChanSamps = chanData->voltage.sample.size();
            if (!numChanSamps) continue;

            out << (1.0 / chanData->voltage.interval) << ',';
            for (size_t sample = 0; sample < numChanSamps ; sample++)
            {
                out << chanData->voltage.sample[sample] << ',';
            }
        }
        out << '\n';

        out.flush();

        emit newData(connection, block);
        //if (connection->isWritable())
            //connection->write(block);
    }

    return true;
}

void ExporterTcp::onNewData(QTcpSocket* connection, QByteArray data)
{
    if (connection->isWritable())
        connection->write(data);
}

bool ExporterTcp::changes(const std::shared_ptr<PPresult> data)
{
    if (!lastSampleSize.size())
    {
        for (ChannelID channel = 0; channel < data->channelCount(); channel++)
        {
            const DataChannel* chanData = data->data(channel);
            size_t numChanSamps = chanData->voltage.sample.size();
            if (!numChanSamps) continue;

            lastSampleSize.push_back(numChanSamps);
            lastSampleInterval.push_back(chanData->voltage.interval);
        }

        return true;
    }

    bool changes = false;

    for (ChannelID channel = 0; channel < data->channelCount(); channel++)
    {
        const DataChannel* chanData = data->data(channel);

        if (lastSampleSize[channel] != chanData->voltage.sample.size())
        {
            lastSampleSize[channel] = chanData->voltage.sample.size();
            changes = true;
        }
        if (lastSampleInterval[channel] != chanData->voltage.interval)
        {
            lastSampleInterval[channel] = chanData->voltage.interval;
            changes = true;
        }
    }

    return changes;
}


bool ExporterTcp::save() {
    return true;
}

float ExporterTcp::progress() { return 0.5f; }

void ExporterTcp::onNewConnection() {
    QTcpSocket *client = tcpServer->nextPendingConnection();
    connect(client, &QAbstractSocket::disconnected, this, &ExporterTcp::onDisconnect);

    qDebug() << "ExporterTcp: New connection from " << client->peerAddress().toString();

    connections.push_back(client);
}

void ExporterTcp::onDisconnect() {
    QTcpSocket* client = (QTcpSocket*) sender();

    connections.removeAll(client);

    client->deleteLater();
}

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

ExporterTcp::ExporterTcp(int port, bool binary) : binary(binary) {
    tcpServer = new QTcpServer();

    if (!tcpServer->listen(QHostAddress::Any, port)) {
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
    QByteArray block;

    if (binary) {
        block = writeAsBinaryByteArray(data);
    }
    else {
        block = writeAsTextByteArray(data);
    }

    for(QTcpSocket* connection: connections) {
        emit newData(connection, block);
    }

    return true;
}

void ExporterTcp::onNewData(QTcpSocket* connection, QByteArray data)
{
    if (connection->isWritable()) {
        connection->write(data);
    }
}

/* ---------- Binary frame format ----------
 * Timestamp (ms since epoch)   : 64 bit integer
 * Number of channels           : 32 bit integer
 * For each channel:
 *  Sample rate                 : 32 bit float
 *  Number of samples           : 32 bit int
 *  Samples                     : 32 bit floats
 *
 */
QByteArray ExporterTcp::writeAsBinaryByteArray(const std::shared_ptr<PPresult> data)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);

    out.setFloatingPointPrecision(QDataStream::SinglePrecision);

    quint32 nChan = 0;
    for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
        if (data->data(channel)->voltage.sample.size()) {
            nChan++;
        }
    }

    qint64 timestamp_ms = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // Timestamp and number of channels
    out << timestamp_ms << nChan;

    for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
        const DataChannel* chanData = data->data(channel);
        size_t numChanSamps = chanData->voltage.sample.size();
        if (!numChanSamps) continue;

        // Sample rate and number of samples
        out << (1.0 / chanData->voltage.interval) << ((quint32) numChanSamps);

        // Samples
        for (size_t sample = 0; sample < numChanSamps ; sample++) {
            out << chanData->voltage.sample[sample];
        }
    }

    return block;
}

QByteArray ExporterTcp::writeAsTextByteArray(const std::shared_ptr<PPresult> data)
{
    QByteArray block;
    QTextStream out(&block, QIODevice::WriteOnly);

    if (changes(data)) {
        out << "#timestamp,";

        for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
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

    for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
        const DataChannel* chanData = data->data(channel);
        size_t numChanSamps = chanData->voltage.sample.size();
        if (!numChanSamps) continue;

        out << (1.0 / chanData->voltage.interval) << ',';
        for (size_t sample = 0; sample < numChanSamps ; sample++) {
            out << chanData->voltage.sample[sample] << ',';
        }
    }
    out << '\n';

    out.flush();

    return block;
}

bool ExporterTcp::changes(const std::shared_ptr<PPresult> data)
{
    if (!lastSampleSize.size()) {
        for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
            const DataChannel* chanData = data->data(channel);
            size_t numChanSamps = chanData->voltage.sample.size();
            if (!numChanSamps) continue;

            lastSampleSize.push_back(numChanSamps);
            lastSampleInterval.push_back(chanData->voltage.interval);
        }

        return true;
    }

    bool changes = false;

    for (ChannelID channel = 0; channel < data->channelCount(); channel++) {
        const DataChannel* chanData = data->data(channel);

        if (lastSampleSize[channel] != chanData->voltage.sample.size()) {
            lastSampleSize[channel] = chanData->voltage.sample.size();
            changes = true;
        }
        if (lastSampleInterval[channel] != chanData->voltage.interval) {
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

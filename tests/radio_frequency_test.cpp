#include "radio_frequency_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QThread>
#include <functional>

bool until(const std::function<bool()> &predicate)
{
	QElapsedTimer timer;
	timer.start();
	while (!predicate() && timer.elapsed() < 2000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return predicate();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTcpServer server;
	if (!server.listen(QHostAddress::LocalHost))
		return 1;
	RadioFrequencyClient client;
	client.configure(false, QStringLiteral("127.0.0.1"),
			 server.serverPort());
	client.publish(435500000, true);
	if (client.state() != QAbstractSocket::UnconnectedState)
		return 2;
	client.configure(true, QStringLiteral("127.0.0.1"),
			 server.serverPort());
	client.publish(435500000, true);
	if (!until([&] {
		    return server.hasPendingConnections() &&
			   client.state() == QAbstractSocket::ConnectedState;
	    }))
		return 3;
	auto *peer = server.nextPendingConnection();
	// Connecting does not replay an old frequency. Only a fresh tick sends
	// F.
	if (peer->bytesAvailable())
		return 4;
	client.publish(435501234, true);
	if (!until([&] { return peer->canReadLine(); }) ||
	    peer->readLine() != "F 435501234\n")
		return 5;
	peer->write("RPRT 0\n");
	peer->flush();
	QCoreApplication::processEvents();
	client.publish(435501999, false);
	if (client.state() != QAbstractSocket::UnconnectedState)
		return 6;
	delete peer;
	client.publish(435502000, true);
	if (!until([&] {
		    return server.hasPendingConnections() &&
			   client.state() == QAbstractSocket::ConnectedState;
	    }))
		return 7;
	peer = server.nextPendingConnection();
	client.publish(435502123, true);
	if (!until([&] { return peer->canReadLine(); }) ||
	    peer->readLine() != "F 435502123\n")
		return 8;
	client.configure(false, QStringLiteral("127.0.0.1"),
			 server.serverPort());
	if (client.state() != QAbstractSocket::UnconnectedState)
		return 9;
	return 0;
}

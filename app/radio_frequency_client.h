#pragma once
#include <QTcpSocket>
#include <QString>

// Physical radios use their own rigctld endpoint. No VFO or PTT commands.
// The caller supplies a fresh, valid Doppler frequency on each tracking tick.
class RadioFrequencyClient : public QTcpSocket
{
      public:
	explicit RadioFrequencyClient(QObject *parent = nullptr)
	    : QTcpSocket(parent)
	{
		setReadBufferSize(4096);
		connect(this, &QTcpSocket::readyRead, this,
			[this] { readAll(); });
	}
	void configure(bool enabled, const QString &host, quint16 port)
	{
		abort();
		enabled_ = enabled;
		host_ = host;
		port_ = port;
	}
	void publish(qint64 frequency, bool valid)
	{
		if (!enabled_ || !valid || frequency <= 0) {
			abort();
			return;
		}
		if (state() == UnconnectedState) {
			connectToHost(host_, port_);
			return;
		}
		// Never accumulate old Doppler commands behind a stalled
		// connection.
		if (state() == ConnectedState && bytesToWrite() == 0)
			write(QByteArray("F ") + QByteArray::number(frequency) +
			      '\n');
	}

      private:
	bool enabled_ = false;
	QString host_ = QStringLiteral("127.0.0.1");
	quint16 port_ = 4534;
};

#ifndef REMOTECONNECTOR_P_H
#define REMOTECONNECTOR_P_H

#include <chrono>

#include <QtCore/QObject>
#include <QtCore/QUuid>
#include <QtCore/QTimer>

#include <QtWebSockets/QWebSocket>

#include "qtdatasync_global.h"
#include "controller_p.h"
#include "defaults.h"
#include "cryptocontroller_p.h"

#include "errormessage_p.h"
#include "identifymessage_p.h"
#include "accountmessage_p.h"
#include "welcomemessage_p.h"

#include "connectorstatemachine.h"

namespace QtDataSync {

class Q_DATASYNC_EXPORT RemoteConnector : public Controller
{
	Q_OBJECT

public:
	static const QString keyRemoteEnabled;
	static const QString keyRemoteUrl;
	static const QString keyAccessKey;
	static const QString keyHeaders;
	static const QString keyKeepaliveTimeout;
	static const QString keyDeviceId;
	static const QString keyDeviceName;

	enum RemoteEvent {
		RemoteDisconnected,
		RemoteReconnecting,
		RemoteReady,
		RemoteSending
	};
	Q_ENUM(RemoteEvent)

	explicit RemoteConnector(const Defaults &defaults, QObject *parent = nullptr);

	void initialize() final;
	void finalize() final;

public Q_SLOTS:
	void reconnect();
	void resync();

Q_SIGNALS:
	void removeEvent(RemoteEvent event);

private Q_SLOTS:
	void connected();
	void disconnected();
	void binaryMessageReceived(const QByteArray &message);
	void error(QAbstractSocket::SocketError error);
	void sslErrors(const QList<QSslError> &errors);
	void ping();

private:
	enum ConnectAction {
		UnexpectedDisconnect,
		PlannedDisconnect,
		PlannedReconnect,
		PlannedRetry
	};

	enum ConnectionState {
		DisconnectedState,
		ReconnectingState,
		ConnectedState,
		RegisteringState,
		LoggingInState,
		IdleState
	};

	static const QVector<std::chrono::seconds> Timeouts;

	CryptoController *_cryptoController;

	QWebSocket *_socket;

	QTimer *_pingTimer;
	bool _awaitingPing;

	ConnectorStateMachine *_stateMachine;
	ConnectAction _connectAction;
	ConnectionState _state;
	int _retryIndex;

	QUuid _deviceId;

	bool checkCanSync(QUrl &remoteUrl);
	bool loadIdentity();
	void tryClose();
	std::chrono::seconds retry();

	QVariant sValue(const QString &key) const;
	void upState(ConnectionState state);

	void onError(const ErrorMessage &message);
	void onIdentify(const IdentifyMessage &message);
	void onAccount(const AccountMessage &message);
	void onWelcome(const WelcomeMessage &message);
};

}

#endif // REMOTECONNECTOR_P_H

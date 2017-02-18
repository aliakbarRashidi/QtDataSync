#include "databasecontroller.h"
#include "app.h"
#include <QtSql>
#include <QtConcurrent>

DatabaseController::DatabaseController(QObject *parent) :
	QObject(parent),
	multiThreaded(false),
	threadStore()
{
	QtConcurrent::run(this, &DatabaseController::initDatabase);
}

QUuid DatabaseController::createIdentity(const QUuid &deviceId)
{
	auto db = threadStore.localData().database();
	if(!db.transaction()) {
		qCritical() << "Failed to create transaction with error:"
					<< qPrintable(db.lastError().text());
		return {};
	}

	auto identity = QUuid::createUuid();

	QSqlQuery createIdentityQuery(db);
	createIdentityQuery.prepare(QStringLiteral("INSERT INTO users (identity) VALUES(?)"));
	createIdentityQuery.addBindValue(identity);
	if(!createIdentityQuery.exec()) {
		qCritical() << "Failed to add new user identity with error:"
					<< qPrintable(createIdentityQuery.lastError().text());
		db.rollback();
		return {};
	}

	QSqlQuery createDeviceQuery(db);
	createDeviceQuery.prepare(QStringLiteral("INSERT INTO devices (deviceid, userid) VALUES(?, ?)"));
	createDeviceQuery.addBindValue(deviceId);
	createDeviceQuery.addBindValue(identity);
	if(!createDeviceQuery.exec()) {
		qCritical() << "Failed to add new device with error:"
					<< qPrintable(createDeviceQuery.lastError().text());
		db.rollback();
		return {};
	}

	if(db.commit())
		return identity;
	else {
		qCritical() << "Failed to commit transaction with error:"
					<< qPrintable(db.lastError().text());
		return {};
	}
}

bool DatabaseController::identify(const QUuid &identity, const QUuid &deviceId)
{
	auto db = threadStore.localData().database();

	QSqlQuery identifyQuery(db);
	identifyQuery.prepare(QStringLiteral("SELECT EXISTS(SELECT identity FROM users WHERE identity = ?) AS exists"));
	identifyQuery.addBindValue(identity);
	if(!identifyQuery.exec() || !identifyQuery.first()) {
		qCritical() << "Failed to identify user with error:"
					<< qPrintable(identifyQuery.lastError().text());
		return false;
	}

	if(identifyQuery.value(0).toBool()) {
		QSqlQuery createDeviceQuery(db);
		createDeviceQuery.prepare(QStringLiteral("INSERT INTO devices (deviceid, userid) VALUES(?, ?) "
												 "ON CONFLICT DO NOTHING"));
		createDeviceQuery.addBindValue(deviceId);
		createDeviceQuery.addBindValue(identity);
		if(!createDeviceQuery.exec()) {
			qCritical() << "Failed to add (new) device with error:"
						<< qPrintable(createDeviceQuery.lastError().text());
			return false;
		} else
			return true;
	} else
		return false;
}

bool DatabaseController::save(const QUuid &userId, const QUuid &deviceId, const QString &type, const QString &key, const QJsonObject &object)
{
	auto db = threadStore.localData().database();
	if(!db.transaction()) {
		qCritical() << "Failed to create transaction with error:"
					<< qPrintable(db.lastError().text());
		return false;
	}

	//check if key exists
	QSqlQuery getIdQuery(db);
	getIdQuery.prepare(QStringLiteral("SELECT index FROM data WHERE userid = ? AND type = ? AND key = ?"));
	getIdQuery.addBindValue(userId);
	getIdQuery.addBindValue(type);
	getIdQuery.addBindValue(key);
	if(!getIdQuery.exec()) {
		qCritical() << "Failed to check if data exists with error:"
					<< qPrintable(getIdQuery.lastError().text());
		db.rollback();
		return false;
	}

	quint64 index = 0;
	if(getIdQuery.first()) {// if exists -> update
		index = getIdQuery.value(0).toULongLong();
		QSqlQuery updateQuery(db);
		updateQuery.prepare(QStringLiteral("UPDATE data SET data = ? WHERE index = ?"));
		updateQuery.addBindValue(jsonToString(object));
		updateQuery.addBindValue(index);
		if(!updateQuery.exec()) {
			qCritical() << "Failed to update data with error:"
						<< qPrintable(updateQuery.lastError().text());
			db.rollback();
			return false;
		}
	} else {// if not exists -> insert
		QSqlQuery insertQuery(db);
		insertQuery.prepare(QStringLiteral("INSERT INTO data (userid, type, key, data) VALUES(?, ?, ?, ?) RETURNING index"));
		insertQuery.addBindValue(userId);
		insertQuery.addBindValue(type);
		insertQuery.addBindValue(key);
		insertQuery.addBindValue(jsonToString(object));
		if(!insertQuery.exec() || !insertQuery.first()) {
			qCritical() << "Failed to insert data with error:"
						<< qPrintable(insertQuery.lastError().text());
			db.rollback();
			return false;
		}
		index = insertQuery.value(0).toULongLong();
	}

	//update the change state
	QSqlQuery updateStateQuery(db);
	updateStateQuery.prepare(QStringLiteral("INSERT INTO states (dataindex, deviceid) "
											"SELECT ?, id FROM devices "
											"WHERE userid = ? AND deviceid != ? "
											"ON CONFLICT DO NOTHING"));
	updateStateQuery.addBindValue(index);
	updateStateQuery.addBindValue(userId);
	updateStateQuery.addBindValue(deviceId);
	if(!updateStateQuery.exec()) {
		qCritical() << "Failed to update device states with error:"
					<< qPrintable(updateStateQuery.lastError().text());
		db.rollback();
		return false;
	}

	//notify all connected devices

	if(db.commit())
		return true;
	else {
		qCritical() << "Failed to commit transaction with error:"
					<< qPrintable(db.lastError().text());
		return false;
	}
}

void DatabaseController::initDatabase()
{
	auto db = threadStore.localData().database();

	if(!db.tables().contains("users")) {
		QSqlQuery createUsers(db);
		if(!createUsers.exec(QStringLiteral("CREATE TABLE users ( "
											"	identity	UUID PRIMARY KEY NOT NULL UNIQUE "
											")"))) {
			qCritical() << "Failed to create users table with error:"
						<< qPrintable(createUsers.lastError().text());
			return;
		}
	}
	if(!db.tables().contains("devices")) {
		QSqlQuery createDevices(db);
		if(!createDevices.exec(QStringLiteral("CREATE TABLE devices ( "
											  "		id			SERIAL PRIMARY KEY NOT NULL, "
											  "		deviceid	UUID NOT NULL, "
											  "		userid		UUID NOT NULL REFERENCES users(identity), "
											  "		CONSTRAINT device_id UNIQUE (deviceid, userid)"
											  ")"))) {
			qCritical() << "Failed to create devices table with error:"
						<< qPrintable(createDevices.lastError().text());
			return;
		}
	}
	if(!db.tables().contains("data")) {
		QSqlQuery createData(db);
		if(!createData.exec(QStringLiteral("CREATE TABLE data ( "
										   "	index	SERIAL PRIMARY KEY NOT NULL, "
										   "	userid	UUID NOT NULL REFERENCES users(identity), "
										   "	type	TEXT NOT NULL, "
										   "	key		TEXT NOT NULL, "
										   "	data	JSONB, "
										   "	CONSTRAINT data_id UNIQUE (userid, type, key)"
										   ")"))) {
			qCritical() << "Failed to create data table with error:"
						<< qPrintable(createData.lastError().text());
			return;
		}
	}
	if(!db.tables().contains("states")) {
		QSqlQuery createStates(db);
		if(!createStates.exec(QStringLiteral("CREATE TABLE states ( "
											 "	dataindex	INTEGER NOT NULL REFERENCES data(index), "
											 "	deviceid	INTEGER NOT NULL REFERENCES devices(id), "
											 "	PRIMARY KEY (dataindex, deviceid)"
											 ")"))) {
			qCritical() << "Failed to create states table with error:"
						<< qPrintable(createStates.lastError().text());
			return;
		}
	}
}

QString DatabaseController::jsonToString(const QJsonObject &object) const
{
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}



DatabaseController::DatabaseWrapper::DatabaseWrapper() :
	dbName(QUuid::createUuid().toString())
{
	auto config = qApp->configuration();
	auto db = QSqlDatabase::addDatabase(config->value("database/driver", "QPSQL").toString(), dbName);
	db.setDatabaseName(config->value("database/name", "QtDataSync").toString());
	db.setHostName(config->value("database/host").toString());
	db.setPort(config->value("database/port").toInt());
	db.setUserName(config->value("database/username").toString());
	db.setPassword(config->value("database/password").toString());
	db.setConnectOptions(config->value("database/options").toString());

	if(!db.open()) {
		qCritical() << "Failed to open database with error:"
					<< qPrintable(db.lastError().text());
	} else
		qInfo() << "DB connected for thread" << QThread::currentThreadId();
}

DatabaseController::DatabaseWrapper::~DatabaseWrapper()
{
	QSqlDatabase::database(dbName).close();
	QSqlDatabase::removeDatabase(dbName);
	qInfo() << "DB disconnected for thread" << QThread::currentThreadId();
}

QSqlDatabase DatabaseController::DatabaseWrapper::database() const
{
	return QSqlDatabase::database(dbName);
}

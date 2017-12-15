#include "cryptocontroller_p.h"
#include "logger.h"
#include "message_p.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>

#include <qiodevicesink.h>
#include <qiodevicesource.h>
#include <qpluginfactory.h>

using namespace QtDataSync;
using namespace CryptoPP;
using Exception = QtDataSync::Exception;

typedef QPluginObjectFactory<KeyStorePlugin, KeyStore> Factory;
Q_GLOBAL_STATIC_WITH_ARGS(Factory, factory, (QLatin1String("keystores")))

#define QTDATASYNC_LOG QTDATASYNC_LOG_CONTROLLER

const QString CryptoController::keySignScheme(QStringLiteral("scheme/signing"));
const QString CryptoController::keyCryptScheme(QStringLiteral("scheme/encryption"));
const QString CryptoController::keySignTemplate(QStringLiteral("device/%1/sign-key"));
const QString CryptoController::keyCryptTemplate(QStringLiteral("device/%1/crypt-key"));

CryptoController::CryptoController(const Defaults &defaults, QObject *parent) :
	Controller("crypto", defaults, parent),
	_keyStore(nullptr),
	_crypto(nullptr)
{}

void CryptoController::initialize()
{
	auto provider = defaults().property(Defaults::KeyStoreProvider).toString();
	_keyStore = factory->createInstance(provider, defaults(), this);
	if(!_keyStore) {
		logCritical() << "Failed to load keystore"
					  << provider
					  << "- synchronization will be temporarily disabled";
	}

	_crypto = new ClientCrypto(this);
}

void CryptoController::finalize()
{
	if(_keyStore)
		_keyStore->closeStore();
}

ClientCrypto *CryptoController::crypto() const
{
	return _crypto;
}

QByteArray CryptoController::fingerprint() const
{
	if(_fingerprint.isEmpty()) {
		try {
			QCryptographicHash hash(QCryptographicHash::Sha3_256);
			hash.addData(_crypto->signatureScheme());
			hash.addData(_crypto->writeSignKey());
			hash.addData(_crypto->encryptionScheme());
			hash.addData(_crypto->writeCryptKey());
			_fingerprint = hash.result();
		} catch(CryptoPP::Exception &e) {
			throw CryptoException(defaults(),
								  QStringLiteral("Failed to generate device fingerprint"),
								  e);
		}
	}

	return _fingerprint;
}

bool CryptoController::canAccessStore() const
{
	try {
		ensureStoreAccess();
		return true;
	} catch(std::exception &e) {
		logCritical() << "Failed to load keystore with error:" << e.what();
		_keyStore->deleteLater();
		return false;
	}
}

void CryptoController::loadKeyMaterial(const QUuid &deviceId)
{
	try {
		ensureStoreAccess();
		_fingerprint.clear();

		auto signScheme = settings()->value(keySignScheme).toByteArray();
		auto signKey = _keyStore->loadPrivateKey(keySignTemplate.arg(deviceId.toString()));
		if(signKey.isNull()) {
			throw KeyStoreException(defaults(),
									QString(), //TODO real name
									QStringLiteral("Unable to load private signing key from keystore"));
		}

		auto cryptScheme = settings()->value(keyCryptScheme).toByteArray();
		auto cryptKey = _keyStore->loadPrivateKey(keyCryptTemplate.arg(deviceId.toString()));
		if(cryptKey.isNull()) {
			throw KeyStoreException(defaults(),
									QString(), //TODO real name
									QStringLiteral("Unable to load private encryption key from keystore"));
		}

		_crypto->load(signScheme, signKey, cryptScheme, cryptKey);
		logDebug() << "Loaded private keys for" << deviceId;

		//NOTE load and decrypt shared secret
	} catch(CryptoPP::Exception &e) {
		throw CryptoException(defaults(),
							  QStringLiteral("Failed to import private key"),
							  e);
	}
}

void CryptoController::createPrivateKeys(const QByteArray &nonce)
{
	try {
		_fingerprint.clear();

		if(_crypto->rng().CanIncorporateEntropy())
			_crypto->rng().IncorporateEntropy((const byte*)nonce.constData(), nonce.size());

		_crypto->generate((Setup::SignatureScheme)defaults().property(Defaults::SignScheme).toInt(),
						  defaults().property(Defaults::SignKeyParam),
						  (Setup::EncryptionScheme)defaults().property(Defaults::CryptScheme).toInt(),
						  defaults().property(Defaults::CryptKeyParam));

#ifndef QT_NO_DEBUG
		logDebug().noquote() << "Generated new private keys. Fingerprint:"
							 << fingerprint().toHex();
#else
		logDebug() << "Generated new private keys";
#endif
	} catch(CryptoPP::Exception &e) {
		throw CryptoException(defaults(),
							  QStringLiteral("Failed to generate private key"),
							  e);
	}
}

void CryptoController::storePrivateKeys(const QUuid &deviceId)
{
	try {
		ensureStoreAccess();

		settings()->setValue(keySignScheme, _crypto->signatureScheme());
		_keyStore->storePrivateKey(keySignTemplate.arg(deviceId.toString()),
								   _crypto->savePrivateSignKey());

		settings()->setValue(keyCryptScheme, _crypto->encryptionScheme());
		_keyStore->storePrivateKey(keyCryptTemplate.arg(deviceId.toString()),
								   _crypto->savePrivateCryptKey());
		logDebug() << "Stored private keys for" << deviceId;
	} catch(CryptoPP::Exception &e) {
		throw CryptoException(defaults(),
							  QStringLiteral("Failed to generate private key"),
							  e);
	}
}

void CryptoController::ensureStoreAccess() const
{
	if(_keyStore)
		_keyStore->loadStore();
	else
		throw KeyStoreException(defaults(), QString(), QStringLiteral("No keystore available"));
}

QStringList CryptoController::allKeystoreKeys()
{
	return factory->allKeys();
}

// ------------- KeyScheme class definitions -------------

template <typename TScheme>
class RsaKeyScheme : public ClientCrypto::KeyScheme
{
public:
	QByteArray name() const override;
	void createPrivateKey(RandomNumberGenerator &rng, const QVariant &keyParam) override;
	PKCS8PrivateKey &privateKeyRef() override;
	QSharedPointer<X509PublicKey> createPublicKey() const override;

private:
	typename TScheme::PrivateKey _key;
};

template <typename TScheme>
class EccKeyScheme : public ClientCrypto::KeyScheme
{
public:
	QByteArray name() const override;
	void createPrivateKey(RandomNumberGenerator &rng, const QVariant &keyParam) override;
	PKCS8PrivateKey &privateKeyRef() override;
	QSharedPointer<X509PublicKey> createPublicKey() const override;

private:
	typename TScheme::PrivateKey _key;
};

// ------------- ClientCrypto Implementation -------------

ClientCrypto::ClientCrypto(QObject *parent) :
	AsymmetricCrypto(parent),
	_rng(true),
	_signKey(nullptr),
	_cryptKey(nullptr)
{}

void ClientCrypto::generate(Setup::SignatureScheme signScheme, const QVariant &signKeyParam, Setup::EncryptionScheme cryptScheme, const QVariant &cryptKeyParam)
{
	//first: clean all
	resetSchemes();
	_signKey.reset();
	_cryptKey.reset();

	//load all schemes
	setSignatureKey(signScheme);
	setSignatureScheme(_signKey->name());
	setEncryptionKey(cryptScheme);
	setEncryptionScheme(_cryptKey->name());

	if(_signKey->name() != signatureScheme())
		throw CryptoPP::Exception(CryptoPP::Exception::OTHER_ERROR, "Signing key scheme does not match signature scheme");
	if(_cryptKey->name() != encryptionScheme())
		throw CryptoPP::Exception(CryptoPP::Exception::OTHER_ERROR, "Crypting key scheme does not match encryption scheme");

	_signKey->createPrivateKey(_rng, signKeyParam);
	if(!_signKey->privateKeyRef().Validate(_rng, 3))
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_DATA_FORMAT, "Signature key failed validation");
	_cryptKey->createPrivateKey(_rng, cryptKeyParam);
	if(!_cryptKey->privateKeyRef().Validate(_rng, 3))
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_DATA_FORMAT, "Signature key failed validation");
}

void ClientCrypto::load(const QByteArray &signScheme, const QByteArray &signKey, const QByteArray &cryptScheme, const QByteArray &cryptKey)
{
	//first: clean all
	resetSchemes();
	_signKey.reset();
	_cryptKey.reset();

	//load all schemes
	setSignatureKey(signScheme);
	setSignatureScheme(signScheme);
	setEncryptionKey(cryptScheme);
	setEncryptionScheme(cryptScheme);

	if(_signKey->name() != signatureScheme())
		throw CryptoPP::Exception(CryptoPP::Exception::OTHER_ERROR, "Signing key scheme does not match signature scheme");
	if(_cryptKey->name() != encryptionScheme())
		throw CryptoPP::Exception(CryptoPP::Exception::OTHER_ERROR, "Crypting key scheme does not match encryption scheme");

	loadKey(_signKey->privateKeyRef(), signKey);
	if(!_signKey->privateKeyRef().Validate(_rng, 3))
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_DATA_FORMAT, "Signature key failed validation");
	loadKey(_cryptKey->privateKeyRef(), cryptKey);
	if(!_cryptKey->privateKeyRef().Validate(_rng, 3))
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_DATA_FORMAT, "Signature key failed validation");
}

RandomNumberGenerator &ClientCrypto::rng()
{
	return _rng;
}

QSharedPointer<X509PublicKey> ClientCrypto::readKey(bool signKey, const QByteArray &data)
{
	return AsymmetricCrypto::readKey(signKey, _rng, data);
}

QSharedPointer<X509PublicKey> ClientCrypto::signKey() const
{
	return _signKey->createPublicKey();
}

QByteArray ClientCrypto::writeSignKey() const
{
	return writeKey(_signKey->createPublicKey());
}

QSharedPointer<X509PublicKey> ClientCrypto::cryptKey() const
{
	return _cryptKey->createPublicKey();
}

QByteArray ClientCrypto::writeCryptKey() const
{
	return writeKey(_cryptKey->createPublicKey());
}

const PKCS8PrivateKey &ClientCrypto::privateSignKey() const
{
	return _signKey->privateKeyRef();
}

QByteArray ClientCrypto::savePrivateSignKey() const
{
	return saveKey(_signKey->privateKeyRef());
}

const PKCS8PrivateKey &ClientCrypto::privateCryptKey() const
{
	return _cryptKey->privateKeyRef();
}

QByteArray ClientCrypto::savePrivateCryptKey() const
{
	return saveKey(_cryptKey->privateKeyRef());
}

QByteArray ClientCrypto::sign(const QByteArray &message)
{
	return AsymmetricCrypto::sign(_signKey->privateKeyRef(), _rng, message);
}

QByteArray ClientCrypto::encrypt(const X509PublicKey &key, const QByteArray &message)
{
	return AsymmetricCrypto::encrypt(key, _rng, message);
}

QByteArray ClientCrypto::decrypt(const QByteArray &message)
{
	return AsymmetricCrypto::decrypt(_cryptKey->privateKeyRef(), _rng, message);
}

#define CC_CURVE(curve) case Setup::curve: return CryptoPP::ASN1::curve()

OID ClientCrypto::curveId(Setup::EllipticCurve curve)
{
	switch (curve) {
		CC_CURVE(secp112r1);
		CC_CURVE(secp128r1);
		CC_CURVE(secp160r1);
		CC_CURVE(secp192r1);
		CC_CURVE(secp224r1);
		CC_CURVE(secp256r1);
		CC_CURVE(secp384r1);
		CC_CURVE(secp521r1);

		CC_CURVE(brainpoolP160r1);
		CC_CURVE(brainpoolP192r1);
		CC_CURVE(brainpoolP224r1);
		CC_CURVE(brainpoolP256r1);
		CC_CURVE(brainpoolP320r1);
		CC_CURVE(brainpoolP384r1);
		CC_CURVE(brainpoolP512r1);

		CC_CURVE(secp112r2);
		CC_CURVE(secp128r2);
		CC_CURVE(secp160r2);
		CC_CURVE(secp160k1);
		CC_CURVE(secp192k1);
		CC_CURVE(secp224k1);
		CC_CURVE(secp256k1);

	default:
		Q_UNREACHABLE();
		break;
	}
}

#undef CC_CURVE

void ClientCrypto::setSignatureKey(const QByteArray &name)
{
	auto stdStr = name.toStdString();
	if(stdStr == RsassScheme::StaticAlgorithmName())
		_signKey.reset(new RsaKeyScheme<RsassScheme>());
	else if(stdStr == EcdsaScheme::StaticAlgorithmName())
		_signKey.reset(new EccKeyScheme<EcdsaScheme>());
	else if(stdStr == EcnrScheme::StaticAlgorithmName())
		_signKey.reset(new EccKeyScheme<EcnrScheme>());
	else
		throw CryptoPP::Exception(CryptoPP::Exception::NOT_IMPLEMENTED, "Signature Scheme \"" + stdStr + "\" not supported");
}

void ClientCrypto::setSignatureKey(Setup::SignatureScheme scheme)
{
	switch (scheme) {
	case Setup::RSA_PSS_SHA3_512:
		setSignatureKey(QByteArray::fromStdString(RsassScheme::StaticAlgorithmName()));
		break;
	case Setup::ECDSA_ECP_SHA3_512:
		setSignatureKey(QByteArray::fromStdString(EcdsaScheme::StaticAlgorithmName()));
		break;
	case Setup::ECNR_ECP_SHA3_512:
		setSignatureKey(QByteArray::fromStdString(EcnrScheme::StaticAlgorithmName()));
		break;
	default:
		Q_UNREACHABLE();
		break;
	}
}

void ClientCrypto::setEncryptionKey(const QByteArray &name)
{
	auto stdStr = name.toStdString();
	if(stdStr == RsaesScheme::StaticAlgorithmName())
		_cryptKey.reset(new RsaKeyScheme<RsaesScheme>());
	else
		throw CryptoPP::Exception(CryptoPP::Exception::NOT_IMPLEMENTED, "Encryption Scheme \"" + stdStr + "\" not supported");
}

void ClientCrypto::setEncryptionKey(Setup::EncryptionScheme scheme)
{
	switch (scheme) {
	case Setup::RSA_OAEP_SHA3_512:
		setEncryptionKey(QByteArray::fromStdString(RsaesScheme::StaticAlgorithmName()));
		break;
	default:
		Q_UNREACHABLE();
		break;
	}
}

void ClientCrypto::loadKey(PKCS8PrivateKey &key, const QByteArray &data)
{
	QByteArraySource source(data, true);
	key.Load(source);
}

QByteArray ClientCrypto::saveKey(const PKCS8PrivateKey &key) const
{
	QByteArray data;
	QByteArraySink sink(data);
	key.Save(sink);
	return data;
}

// ------------- Generic KeyScheme Implementation -------------

template <typename TScheme>
QByteArray RsaKeyScheme<TScheme>::name() const
{
	return QByteArray::fromStdString(TScheme::StaticAlgorithmName());
}

template <typename TScheme>
void RsaKeyScheme<TScheme>::createPrivateKey(RandomNumberGenerator &rng, const QVariant &keyParam)
{
	if(keyParam.type() != QVariant::Int)
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_ARGUMENT, "keyParam must be an unsigned integer");
	_key.GenerateRandomWithKeySize(rng, keyParam.toUInt());
}

template <typename TScheme>
PKCS8PrivateKey &RsaKeyScheme<TScheme>::privateKeyRef()
{
	return _key;
}

template <typename TScheme>
QSharedPointer<X509PublicKey> RsaKeyScheme<TScheme>::createPublicKey() const
{
	return QSharedPointer<typename TScheme::PublicKey>::create(_key);
}



template <typename TScheme>
QByteArray EccKeyScheme<TScheme>::name() const
{
	return QByteArray::fromStdString(TScheme::StaticAlgorithmName());
}

template <typename TScheme>
void EccKeyScheme<TScheme>::createPrivateKey(RandomNumberGenerator &rng, const QVariant &keyParam)
{
	if(keyParam.type() != QVariant::Int)
		throw CryptoPP::Exception(CryptoPP::Exception::INVALID_ARGUMENT, "keyParam must be a Setup::EllipticCurve");
	auto curve = ClientCrypto::curveId((Setup::EllipticCurve)keyParam.toInt());
	_key.Initialize(rng, curve);
}

template <typename TScheme>
PKCS8PrivateKey &EccKeyScheme<TScheme>::privateKeyRef()
{
	return _key;
}

template <typename TScheme>
QSharedPointer<X509PublicKey> EccKeyScheme<TScheme>::createPublicKey() const
{
	auto key = QSharedPointer<typename TScheme::PublicKey>::create();
	_key.MakePublicKey(*key);
	return key;
}

// ------------- Exceptions Implementation -------------

CryptoException::CryptoException(const Defaults &defaults, const QString &message, const CryptoPP::Exception &cExcept) :
	Exception(defaults, message),
	_exception(cExcept)
{}

CryptoPP::Exception CryptoException::cryptoPPException() const
{
	return _exception;
}

QString CryptoException::error() const
{
	return QString::fromStdString(_exception.GetWhat());
}

CryptoPP::Exception::ErrorType CryptoException::type() const
{
	return _exception.GetErrorType();
}

CryptoException::CryptoException(const CryptoException * const other) :
	Exception(other),
	_exception(other->_exception)
{}

QByteArray CryptoException::className() const noexcept
{
	return QTDATASYNC_EXCEPTION_NAME(CryptoException);
}

QString CryptoException::qWhat() const
{
	return Exception::qWhat() +
			QStringLiteral("\n\tCryptoPP::Error: %1"
						   "\n\tCryptoPP::Type: %2")
			.arg(error())
			.arg((int)type());
}

void CryptoException::raise() const
{
	throw (*this);
}

QException *CryptoException::clone() const
{
	return new CryptoException(this);
}
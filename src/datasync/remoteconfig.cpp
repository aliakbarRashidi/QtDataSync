#include "remoteconfig.h"
#include "remoteconfig_p.h"
#include "message_p.h"
using namespace QtDataSync;

RemoteConfig::RemoteConfig(const QUrl &url, const QString &accessKey, const HeaderHash &headers, int keepaliveTimeout) :
	d{new RemoteConfigPrivate(url, accessKey, headers, keepaliveTimeout)}
{}

RemoteConfig::RemoteConfig(const RemoteConfig &other) = default;

RemoteConfig::RemoteConfig(RemoteConfig &&other) noexcept = default;

RemoteConfig::~RemoteConfig() = default;

RemoteConfig &RemoteConfig::operator=(const RemoteConfig &other) = default;

RemoteConfig &RemoteConfig::operator=(RemoteConfig &&other) noexcept = default;

bool RemoteConfig::operator==(const RemoteConfig &other) const
{
	return d == other.d || (
				d->url == other.d->url &&
				d->accessKey == other.d->accessKey &&
				d->headers == other.d->headers &&
				d->keepaliveTimeout == other.d->keepaliveTimeout);
}

bool RemoteConfig::operator!=(const RemoteConfig &other) const
{
	return d != other.d && (
				d->url != other.d->url ||
				d->accessKey != other.d->accessKey ||
				d->headers != other.d->headers ||
				d->keepaliveTimeout != other.d->keepaliveTimeout);
}


QUrl RemoteConfig::url() const
{
	return d->url;
}

QString RemoteConfig::accessKey() const
{
	return d->accessKey;
}

RemoteConfig::HeaderHash RemoteConfig::headers() const
{
	return d->headers;
}

int RemoteConfig::keepaliveTimeout() const
{
	return d->keepaliveTimeout;
}

void RemoteConfig::setUrl(QUrl url)
{
	d->url = std::move(url);
}

void RemoteConfig::setAccessKey(QString accessKey)
{
	d->accessKey = std::move(accessKey);
}

void RemoteConfig::setHeaders(RemoteConfig::HeaderHash headers)
{
	d->headers = std::move(headers);
}

void RemoteConfig::setKeepaliveTimeout(int keepaliveTimeout)
{
	d->keepaliveTimeout = keepaliveTimeout;
}

QVariantMap RemoteConfig::storedHeaders() const
{
	QVariantMap map;
	for(auto it = d->headers.constBegin(); it != d->headers.constEnd(); ++it)
		map.insert(QString::fromUtf8(it.key()), QString::fromUtf8(it.value()));
	return map;
}

void RemoteConfig::setStoredHeaders(const QVariantMap &map)
{
	d->headers.clear();
	for(auto it = map.constBegin(); it != map.constEnd(); ++it)
		d->headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());
}



QDataStream &QtDataSync::operator<<(QDataStream &stream, const RemoteConfig &deviceInfo)
{
	stream << deviceInfo.d->url
		   << static_cast<Utf8String>(deviceInfo.d->accessKey)
		   << deviceInfo.d->headers
		   << deviceInfo.d->keepaliveTimeout;
	return stream;
}

QDataStream &QtDataSync::operator>>(QDataStream &stream, RemoteConfig &deviceInfo)
{
	Utf8String accessKey;
	stream >> deviceInfo.d->url
		   >> accessKey
		   >> deviceInfo.d->headers
		   >> deviceInfo.d->keepaliveTimeout;
	deviceInfo.d->accessKey = accessKey;
	return stream;
}



RemoteConfigPrivate::RemoteConfigPrivate(QUrl url, QString accessKey, RemoteConfig::HeaderHash headers, int keepaliveTimeout) :
	QSharedData{},
	url{std::move(url)},
	accessKey{std::move(accessKey)},
	headers{std::move(headers)},
	keepaliveTimeout{keepaliveTimeout}
{}

RemoteConfigPrivate::RemoteConfigPrivate(const RemoteConfigPrivate &other) = default;

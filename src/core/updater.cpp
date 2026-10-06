#include "core/updater.hpp"

#include "core/lang.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

namespace {

/// Latest published release (drafts and prereleases are not returned).
constexpr auto kLatestUrl = "https://api.github.com/repos/Larzt/tagoror/releases/latest";

/// GitHub rejects requests without a User-Agent.
constexpr auto kUserAgent = "Tagoror-update-check";

constexpr int kTimeoutMs = 10000;

}  // namespace

Updater::Updater(QObject *parent) : QObject(parent) {
    m_net = new QNetworkAccessManager(this);
}

QString Updater::current() {
    QString v = QCoreApplication::applicationVersion();
    if (v.startsWith('v')) v.remove(0, 1);
    return v;
}

int Updater::compare(const QString &a, const QString &b) {
    auto trozos = [](const QString &s) {
        QString v = s.trimmed();
        if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
        // Cut at the first dash: "2.1.0-beta2" compares as 2.1.0.
        v = v.section('-', 0, 0);

        QList<int> out;
        for (const QString &parte : v.split('.')) {
            bool ok = false;
            const int n = parte.toInt(&ok);
            if (!ok) break;   // stop at the first non-number
            out.append(n);
        }
        return out;
    };

    const QList<int> va = trozos(a), vb = trozos(b);
    for (int i = 0; i < qMax(va.size(), vb.size()); ++i) {
        const int na = va.value(i, 0), nb = vb.value(i, 0);
        if (na != nb) return na < nb ? -1 : 1;
    }
    return 0;
}

void Updater::check() {
    if (m_reply) return;   // already in flight

    QNetworkRequest req{QUrl(QString::fromLatin1(kLatestUrl))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", kUserAgent);
    // Without a timeout an unresponsive network hangs the check forever.
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &Updater::onReply);
}

void Updater::onReply() {
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;            // before emitting: a listener may start another check
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit finished(QString(), QString(), L("No se pudo comprobar"));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = root["tag_name"].toString();
    if (tag.isEmpty()) {
        // Something unexpected (a rate limit, an error page): better to say the check
        // failed than to invent a version from an empty object.
        emit finished(QString(), QString(), L("No se pudo comprobar"));
        return;
    }

    QString version = tag;
    if (version.startsWith('v') || version.startsWith('V')) version.remove(0, 1);
    emit finished(version, root["html_url"].toString(), QString());
}

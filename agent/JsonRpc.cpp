#include "agent/JsonRpc.h"

namespace {

QJsonObject envelope()
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}};
}

} // namespace

RpcMessage readRpc(const QJsonObject &message)
{
    RpcMessage rpc;
    rpc.id = message.value(QLatin1String("id"));
    rpc.method = message.value(QLatin1String("method")).toString();
    rpc.params = message.value(QLatin1String("params")).toObject();
    rpc.result = message.value(QLatin1String("result"));
    rpc.error = message.value(QLatin1String("error")).toObject();
    if (rpc.method.isEmpty())
        rpc.kind = RpcMessage::Kind::Response;
    else if (rpc.id.isUndefined() || rpc.id.isNull())
        rpc.kind = RpcMessage::Kind::Notification;
    else
        rpc.kind = RpcMessage::Kind::Request;
    return rpc;
}

QJsonObject rpcRequest(int id, const QString &method, const QJsonObject &params)
{
    QJsonObject message = envelope();
    message.insert(QLatin1String("id"), id);
    message.insert(QLatin1String("method"), method);
    message.insert(QLatin1String("params"), params);
    return message;
}

QJsonObject rpcNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject message = envelope();
    message.insert(QLatin1String("method"), method);
    message.insert(QLatin1String("params"), params);
    return message;
}

QJsonObject rpcResult(const QJsonValue &id, const QJsonValue &result)
{
    QJsonObject message = envelope();
    message.insert(QLatin1String("id"), id);
    message.insert(QLatin1String("result"), result);
    return message;
}

QJsonObject rpcError(const QJsonValue &id, int code, const QString &text)
{
    QJsonObject message = envelope();
    message.insert(QLatin1String("id"), id);
    message.insert(QLatin1String("error"),
                   QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), text}});
    return message;
}

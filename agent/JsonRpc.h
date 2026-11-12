#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

// One JSON-RPC 2.0 message. The agent protocol is JSON-RPC with one object
// per line. See docs/agent.md.
struct RpcMessage
{
    enum class Kind { Request, Notification, Response };

    Kind kind = Kind::Notification;
    // A request's id, echoed back unchanged in its response.
    QJsonValue id;
    QString method;
    QJsonObject params;
    QJsonValue result;
    // A failed response. Empty when the call succeeded.
    QJsonObject error;
};

RpcMessage readRpc(const QJsonObject &message);

QJsonObject rpcRequest(int id, const QString &method, const QJsonObject &params);
QJsonObject rpcNotification(const QString &method, const QJsonObject &params);
QJsonObject rpcResult(const QJsonValue &id, const QJsonValue &result);
QJsonObject rpcError(const QJsonValue &id, int code, const QString &message);

namespace RpcCode {
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int Internal = -32603;
// ACP's resource_not_found: a file that is neither open nor on disk.
constexpr int NotFound = -32002;
// The user, or the project's permissions, turned the request down.
constexpr int Rejected = -32001;
} // namespace RpcCode

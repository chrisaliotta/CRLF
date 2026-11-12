// Confirms the parts of the agent protocol that decide what the editor does
// without a process: AgentPolicy, which is the one place an agent's request
// is checked, and the reading of JSON-RPC messages and session updates the
// Agent panel draws from. See docs/agent.md.

#include "agent/AcpTypes.h"
#include "agent/AgentPolicy.h"
#include "agent/JsonRpc.h"

#include <QJsonDocument>
#include <QTest>

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

} // namespace

class AgentTests : public QObject
{
    Q_OBJECT

private slots:
    // A model can name any path. Inside the project the .crlf setting
    // applies; a path that climbs out with .. is outside, and a read there
    // asks instead of being allowed.
    void policyKeepsThePathsInsideTheProject()
    {
        const AgentPolicy policy(QStringLiteral("C:/work/app"), AgentPermissions());
        QVERIFY(policy.inside(QStringLiteral("C:/work/app/src/main.cpp")));
        QVERIFY(policy.inside(QStringLiteral("src/main.cpp")));
        QVERIFY(policy.inside(QStringLiteral("C:\\work\\app\\README.md")));
        QVERIFY(!policy.inside(QStringLiteral("C:/work/app/../secrets.txt")));
        QVERIFY(!policy.inside(QStringLiteral("C:/work/application/x.txt")));
        QCOMPARE(policy.resolve(QStringLiteral("src/../lib/a.h")), QStringLiteral("C:/work/app/lib/a.h"));
        QCOMPARE(policy.decide(AgentAction::Read, QStringLiteral("C:/work/app/a.txt")), AgentAccess::Allow);
        QCOMPARE(policy.decide(AgentAction::Read, QStringLiteral("C:/work/app/../b.txt")), AgentAccess::Ask);
#ifdef Q_OS_WIN
        QVERIFY(policy.inside(QStringLiteral("c:/WORK/App/a.txt")));
#endif
    }

    // Writes are reviewed and commands ask unless .crlf says otherwise.
    // "Always allow" lifts the question inside the project only, and a
    // denied action stays denied.
    void policyDefaultsAndGrants()
    {
        AgentPermissions permissions;
        permissions.read = AgentAccess::Deny;
        AgentPolicy policy(QStringLiteral("/home/me/app"), permissions);
        QCOMPARE(policy.decide(AgentAction::Write, QStringLiteral("/home/me/app/a.txt")), AgentAccess::Review);
        QCOMPARE(policy.decide(AgentAction::Terminal, QStringLiteral("/home/me/app")), AgentAccess::Ask);

        policy.grant(AgentAction::Terminal);
        policy.grant(AgentAction::Read);
        QCOMPARE(policy.decide(AgentAction::Terminal, QStringLiteral("/home/me/app/build")), AgentAccess::Allow);
        QCOMPARE(policy.decide(AgentAction::Terminal, QStringLiteral("/tmp")), AgentAccess::Ask);
        QCOMPARE(policy.decide(AgentAction::Read, QStringLiteral("/home/me/app/a.txt")), AgentAccess::Deny);

        QCOMPARE(accessNamed(QStringLiteral("allow"), AgentAccess::Ask), AgentAccess::Allow);
        QCOMPARE(accessNamed(QStringLiteral("sometimes"), AgentAccess::Ask), AgentAccess::Ask);
    }

    // A response has no method; a notification has no id. Responses are
    // matched to the editor's own requests, so the id must survive.
    void readsJsonRpcKinds()
    {
        QCOMPARE(readRpc(json(R"({"jsonrpc":"2.0","id":4,"result":{"ok":true}})")).kind, RpcMessage::Kind::Response);
        const RpcMessage request = readRpc(json(R"({"jsonrpc":"2.0","id":"r1","method":"fs/read_text_file","params":{"path":"/a"}})"));
        QCOMPARE(request.kind, RpcMessage::Kind::Request);
        QCOMPARE(request.id.toString(), QStringLiteral("r1"));
        QCOMPARE(request.params.value(QLatin1String("path")).toString(), QStringLiteral("/a"));
        QCOMPARE(readRpc(json(R"({"jsonrpc":"2.0","method":"session/update","params":{}})")).kind,
                 RpcMessage::Kind::Notification);
        const QJsonObject error = rpcError(7, RpcCode::Rejected, QStringLiteral("no"));
        QCOMPARE(error.value(QLatin1String("error")).toObject().value(QLatin1String("code")).toInt(), RpcCode::Rejected);
    }

    // The transcript is built from these: message chunks, tool calls with
    // their status, text, and file locations, and the plan.
    void readsSessionUpdates()
    {
        AgentUpdate chunk = readUpdate(json(R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Hi"}})"));
        QCOMPARE(chunk.kind, AgentUpdateKind::AgentText);
        QCOMPARE(chunk.text, QStringLiteral("Hi"));

        AgentUpdate tool = readUpdate(json(R"({"sessionUpdate":"tool_call","toolCallId":"c1","title":"Edit a.cpp",
            "kind":"edit","status":"in_progress","locations":[{"path":"/src/a.cpp","line":12}],
            "content":[{"type":"content","content":{"type":"text","text":"done"}},
                       {"type":"diff","path":"/src/a.cpp","oldText":"x","newText":"y"}]})"));
        QCOMPARE(tool.kind, AgentUpdateKind::ToolCall);
        QCOMPARE(tool.toolCallId, QStringLiteral("c1"));
        QCOMPARE(tool.status, QStringLiteral("in_progress"));
        QCOMPARE(tool.text, QStringLiteral("done\nEdited /src/a.cpp"));
        QCOMPARE(tool.locations.size(), 1);
        QCOMPARE(tool.locations.first().line, 12);

        AgentUpdate plan = readUpdate(json(R"({"sessionUpdate":"plan","entries":[{"content":"Read","status":"completed","priority":"high"},
            {"content":"Fix","status":"pending","priority":"high"}]})"));
        QCOMPARE(plan.kind, AgentUpdateKind::Plan);
        QCOMPARE(plan.plan.size(), 2);
        QCOMPARE(plan.plan.at(0).status, QStringLiteral("completed"));

        QCOMPARE(readUpdate(json(R"({"sessionUpdate":"available_commands_update"})")).kind, AgentUpdateKind::Ignored);
    }

    // The panel draws one control per option from this list and knows
    // nothing else about it: a flat select, a grouped select flattened in
    // order, and a boolean. A type the editor cannot draw is left out, and
    // the agent's own update carries the same list.
    void readsConfigOptions()
    {
        const QJsonObject update = json(R"({"sessionUpdate":"config_option_update","configOptions":[
            {"id":"model","name":"Model","category":"model","type":"select","currentValue":"b",
             "options":[{"value":"a","name":"A","description":"fast"},{"value":"b","name":"B"}]},
            {"id":"effort","name":"Effort","type":"select","currentValue":"high",
             "options":[{"group":"g1","name":"Usual","options":[{"value":"low","name":"Low"}]},
                        {"group":"g2","name":"More","options":[{"value":"high","name":"High"}]}]},
            {"id":"brave","name":"Brave","type":"boolean","currentValue":true},
            {"id":"slider","name":"Slider","type":"range","currentValue":3}]})");
        const AgentUpdate read = readUpdate(update);
        QCOMPARE(read.kind, AgentUpdateKind::ConfigOptions);
        const QList<AgentConfigOption> options = read.configOptions;
        QCOMPARE(options.size(), 3);
        QCOMPARE(options.at(0).id, QStringLiteral("model"));
        QCOMPARE(options.at(0).category, QStringLiteral("model"));
        QCOMPARE(options.at(0).currentValue.toString(), QStringLiteral("b"));
        QCOMPARE(options.at(0).choices.size(), 2);
        QCOMPARE(options.at(0).choices.at(0).description, QStringLiteral("fast"));
        QCOMPARE(options.at(1).choices.size(), 2);
        QCOMPARE(options.at(1).choices.at(1).value, QStringLiteral("high"));
        QCOMPARE(options.at(1).choices.at(1).group, QStringLiteral("More"));
        QCOMPARE(options.at(2).type, QStringLiteral("boolean"));
        QCOMPARE(options.at(2).currentValue.typeId(), int(QMetaType::Bool));
        QVERIFY(options.at(2).currentValue.toBool());
    }
};

QTEST_GUILESS_MAIN(AgentTests)

#include "AgentTests.moc"

// Tests for the LSP server: JSON-RPC framing, lifecycle, diagnostics,
// completion, hover, definition, document symbols and formatting.
#include <gtest/gtest.h>

#include "Json.h"
#include "LspServer.h"

#include <sstream>
#include <string>
#include <vector>

using namespace Script;

namespace {

// Build a Content-Length framed JSON-RPC message.
std::string frame(const std::string &json) {
  return "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
}

// Feed `requests` to a server; parse all framed responses it writes.
std::vector<Json> runSession(const std::vector<std::string> &requests) {
  std::string in;
  for (const auto &r : requests) {
    in += frame(r);
  }
  std::istringstream instream(in);
  std::ostringstream outstream;
  runLanguageServer(instream, outstream);

  std::vector<Json> out;
  const std::string &raw = outstream.str();
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t hdr = raw.find("\r\n\r\n", pos);
    if (hdr == std::string::npos) {
      break;
    }
    size_t len = 0;
    size_t cl = raw.find("Content-Length:", pos);
    if (cl != std::string::npos && cl < hdr) {
      len = static_cast<size_t>(std::strtoull(raw.c_str() + cl + 15, nullptr, 10));
    }
    size_t bodyStart = hdr + 4;
    if (bodyStart + len > raw.size()) {
      break;
    }
    out.push_back(Json::parse(raw.substr(bodyStart, len)));
    pos = bodyStart + len;
  }
  return out;
}

std::string initReq(int id) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
         ",\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";
}

std::string didOpen(const std::string &uri, const std::string &text) {
  Json escaped;
  (void)escaped;
  // Use Json serialization for safe string embedding.
  Json msg = Json::Object{
      {"jsonrpc", "2.0"},
      {"method", "textDocument/didOpen"},
      {"params",
       Json::Object{{"textDocument",
                     Json::Object{{"uri", uri},
                                  {"languageId", "cxxscript"},
                                  {"version", 1},
                                  {"text", text}}}}}};
  return msg.dump();
}

const Json *responseFor(const std::vector<Json> &msgs, int64_t id) {
  for (const auto &m : msgs) {
    const Json *mid = m.find("id");
    if (mid && mid->isInt() && mid->asInt() == id) {
      return &m;
    }
  }
  return nullptr;
}

std::vector<const Json *> notificationsFor(const std::vector<Json> &msgs,
                                           const std::string &method) {
  std::vector<const Json *> out;
  for (const auto &m : msgs) {
    const Json *mm = m.find("method");
    if (mm && mm->isString() && mm->asString() == method) {
      out.push_back(&m);
    }
  }
  return out;
}

const std::string kUri = "file:///tmp/lsp_test.script";

} // namespace

TEST(JsonTest, ParseAndDumpRoundTrip) {
  Json v = Json::parse("{\"a\":[1,2.5,true,null,\"x\\n\"],\"b\":{\"c\":-3}}");
  ASSERT_TRUE(v.isObject());
  const Json &a = v.at("a");
  ASSERT_TRUE(a.isArray());
  EXPECT_EQ(a[0].asInt(), 5 - 4);
  EXPECT_DOUBLE_EQ(a[1].asDouble(), 2.5);
  EXPECT_TRUE(a[2].asBool());
  EXPECT_TRUE(a[3].isNull());
  EXPECT_EQ(a[4].asString(), "x\n");
  EXPECT_EQ(v.at("b").at("c").asInt(), -3);

  // Round-trip
  Json reparsed = Json::parse(v.dump());
  EXPECT_EQ(reparsed.at("b").at("c").asInt(), -3);
  EXPECT_EQ(reparsed.at("a")[4].asString(), "x\n");
  // Pretty printing stays valid JSON
  EXPECT_EQ(Json::parse(v.dump(2)).at("a")[0].asInt(), 1);
}

TEST(JsonTest, ParseErrors) {
  EXPECT_THROW(Json::parse("{"), JsonError);
  EXPECT_THROW(Json::parse("[1,]"), JsonError);
  EXPECT_THROW(Json::parse("\"abc"), JsonError);
  EXPECT_THROW(Json::parse("01"), JsonError);
  EXPECT_THROW(Json::parse("1 2"), JsonError);
  EXPECT_THROW(Json::parse("nul"), JsonError);
}

TEST(JsonTest, EscapesAndUtf8) {
  Json v = Json::parse("\"a\\u0041\\u00e9\\ud83d\\ude00\"");
  EXPECT_EQ(v.asString(), "aA\xc3\xa9\xf0\x9f\x98\x80");
  Json s = Json(std::string("q\"\\\t"));
  EXPECT_EQ(s.dump(), "\"q\\\"\\\\\\t\"");
  EXPECT_EQ(Json::parse(s.dump()).asString(), "q\"\\\t");
}

TEST(JsonTest, Numbers) {
  EXPECT_TRUE(Json::parse("42").isInt());
  EXPECT_TRUE(Json::parse("-7").isInt());
  EXPECT_TRUE(Json::parse("3.14").isDouble());
  EXPECT_TRUE(Json::parse("1e3").isDouble());
  EXPECT_TRUE(Json::parse("9007199254740993").isInt());
  // double round-trip
  Json d = Json(0.1);
  EXPECT_DOUBLE_EQ(Json::parse(d.dump()).asDouble(), 0.1);
}

TEST(LspTest, InitializeCapabilities) {
  auto msgs = runSession({initReq(1),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 1);
  ASSERT_NE(resp, nullptr);
  const Json &caps = resp->at("result").at("capabilities");
  EXPECT_EQ(caps.intOr("textDocumentSync"), 1);
  EXPECT_TRUE(caps.find("completionProvider") != nullptr);
  EXPECT_TRUE(caps.boolOr("hoverProvider"));
  EXPECT_TRUE(caps.boolOr("definitionProvider"));
  EXPECT_TRUE(caps.boolOr("documentSymbolProvider"));
  EXPECT_TRUE(caps.boolOr("documentFormattingProvider"));
}

TEST(LspTest, DiagnosticsOnOpen) {
  std::string src = "int32 main() {\n  int32 x = ;\n  return 0;\n}\n";
  auto msgs = runSession({initReq(1), didOpen(kUri, src),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  auto diags =
      notificationsFor(msgs, "textDocument/publishDiagnostics");
  ASSERT_FALSE(diags.empty());
  const Json &params = diags.back()->at("params");
  EXPECT_EQ(params.stringOr("uri"), kUri);
  const Json &list = params.at("diagnostics");
  ASSERT_GE(list.size(), 1u);
  EXPECT_EQ(list[0].intOr("severity"), 1);
  // Error is on line 1 (0-based) where `= ;` appears.
  EXPECT_EQ(list[0].at("range").at("start").intOr("line"), 1);
}

TEST(LspTest, CleanDocNoDiagnostics) {
  std::string src = "int32 main() { return 0; }\n";
  auto msgs = runSession({didOpen(kUri, src),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  auto diags = notificationsFor(msgs, "textDocument/publishDiagnostics");
  ASSERT_FALSE(diags.empty());
  EXPECT_EQ(diags.back()->at("params").at("diagnostics").size(), 0u);
}

TEST(LspTest, DiagnosticsUpdateOnChange) {
  std::string good = "int32 main() { return 0; }\n";
  std::string bad = "int32 main() { return }\n";
  Json change = Json::Object{
      {"jsonrpc", "2.0"},
      {"method", "textDocument/didChange"},
      {"params",
       Json::Object{
           {"textDocument", Json::Object{{"uri", kUri}, {"version", 2}}},
           {"contentChanges",
            Json::Array{Json::Object{{"text", bad}}}}}}};
  auto msgs =
      runSession({didOpen(kUri, good), change.dump(),
                  "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  auto diags = notificationsFor(msgs, "textDocument/publishDiagnostics");
  ASSERT_GE(diags.size(), 2u);
  EXPECT_EQ(diags[0]->at("params").at("diagnostics").size(), 0u);
  EXPECT_GE(diags.back()->at("params").at("diagnostics").size(), 1u);
}

TEST(LspTest, CompletionItems) {
  std::string src =
      "struct Point { int32 x; int32 y; }\n"
      "int32 helper(int32 a) { return a; }\n"
      "int32 main() { return 0; }\n";
  Json comp = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 7},
      {"method", "textDocument/completion"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"position",
                     Json::Object{{"line", 0}, {"character", 0}}}}}};
  auto msgs = runSession({didOpen(kUri, src), comp.dump(),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 7);
  ASSERT_NE(resp, nullptr);
  const Json &items = resp->at("result");
  ASSERT_TRUE(items.isArray());
  std::set<std::string> labels;
  for (const auto &it : items.asArray()) {
    labels.insert(it.stringOr("label"));
  }
  EXPECT_TRUE(labels.count("if"));
  EXPECT_TRUE(labels.count("while"));
  EXPECT_TRUE(labels.count("len"));     // builtin
  EXPECT_TRUE(labels.count("helper"));  // doc proc
  EXPECT_TRUE(labels.count("Point"));   // doc struct
  EXPECT_TRUE(labels.count("main"));
}

TEST(LspTest, MemberCompletion) {
  std::string src =
      "struct Point { int32 x; int32 y; int32 norm() { return 0; } }\n"
      "int32 main() { Point p; return p.\n";
  Json comp = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 8},
      {"method", "textDocument/completion"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"position",
                     // line 1: `int32 main() { Point p; return p.` col = len
                     Json::Object{{"line", 1}, {"character", 43}}}}}};
  auto msgs = runSession({didOpen(kUri, src), comp.dump(),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 8);
  ASSERT_NE(resp, nullptr);
  std::set<std::string> labels;
  for (const auto &it : resp->at("result").asArray()) {
    labels.insert(it.stringOr("label"));
  }
  EXPECT_TRUE(labels.count("x"));
  EXPECT_TRUE(labels.count("y"));
  EXPECT_TRUE(labels.count("norm"));
  EXPECT_FALSE(labels.count("helper"));
}

TEST(LspTest, HoverProcAndBuiltin) {
  std::string src = "int32 helper(int32 a) { return len(\"x\") + a; }\n";
  Json hover = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 9},
      {"method", "textDocument/hover"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"position",
                     // over `helper` (col 8)
                     Json::Object{{"line", 0}, {"character", 8}}}}}};
  auto msgs = runSession({didOpen(kUri, src), hover.dump(),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 9);
  ASSERT_NE(resp, nullptr);
  std::string val =
      resp->at("result").at("contents").stringOr("value");
  EXPECT_NE(val.find("helper"), std::string::npos);
  EXPECT_NE(val.find("int32"), std::string::npos);

  // Hover over builtin `len`
  Json hover2 = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 10},
      {"method", "textDocument/hover"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"position",
                     // over `len` (cols 31-33)
                     Json::Object{{"line", 0}, {"character", 32}}}}}};
  auto msgs2 = runSession({didOpen(kUri, src), hover2.dump(),
                           "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp2 = responseFor(msgs2, 10);
  ASSERT_NE(resp2, nullptr);
  EXPECT_NE(resp2->at("result").at("contents").stringOr("value").find("len"),
            std::string::npos);
}

TEST(LspTest, DefinitionAndSymbols) {
  std::string src =
      "struct Point { int32 x; }\n"
      "int32 helper() { return 1; }\n"
      "int32 main() { return helper(); }\n";
  Json defs = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 11},
      {"method", "textDocument/definition"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"position",
                     // `helper()` call on line 2, cols 22-27
                     Json::Object{{"line", 2}, {"character", 24}}}}}};
  Json syms = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 12},
      {"method", "textDocument/documentSymbol"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}}}}};
  auto msgs = runSession({didOpen(kUri, src), defs.dump(), syms.dump(),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *dresp = responseFor(msgs, 11);
  ASSERT_NE(dresp, nullptr);
  // helper defined on line 1
  EXPECT_EQ(dresp->at("result").at("range").at("start").intOr("line"), 1);

  const Json *sresp = responseFor(msgs, 12);
  ASSERT_NE(sresp, nullptr);
  std::set<std::string> names;
  for (const auto &s : sresp->at("result").asArray()) {
    names.insert(s.stringOr("name"));
  }
  EXPECT_TRUE(names.count("Point"));
  EXPECT_TRUE(names.count("helper"));
  EXPECT_TRUE(names.count("main"));
}

TEST(LspTest, FormattingEdit) {
  std::string src = "int32  main(){int32 x=1;return x;}\n";
  Json fmt = Json::Object{
      {"jsonrpc", "2.0"},
      {"id", 13},
      {"method", "textDocument/formatting"},
      {"params",
       Json::Object{{"textDocument", Json::Object{{"uri", kUri}}},
                    {"options", Json::Object{}}}}};
  auto msgs = runSession({didOpen(kUri, src), fmt.dump(),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 13);
  ASSERT_NE(resp, nullptr);
  const Json &edits = resp->at("result");
  ASSERT_TRUE(edits.isArray());
  if (edits.size() == 1) {
    std::string nt = edits[0].stringOr("newText");
    EXPECT_NE(nt.find("int32 x = 1"), std::string::npos);
  }
}

TEST(LspTest, ShutdownThenExit) {
  auto msgs = runSession(
      {initReq(1), "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\"}",
       "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}"});
  const Json *resp = responseFor(msgs, 2);
  ASSERT_NE(resp, nullptr);
  EXPECT_TRUE(resp->find("result") != nullptr);
}

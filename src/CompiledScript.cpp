// Compiled-script artifacts (.scriptc): a portable binary serialization of
// a script file's bytecode-compiled procedures, structs (with methods),
// and enums. Produced by ScriptManager::saveCompiled, consumed by
// ScriptManager::loadCompiled. Artifacts contain bytecode only — they
// require the VM to run and carry no AST or source text.
//
// Format (little-endian, all integers fixed width):
//   magic u32 'CXSC' | version u32 | source filename string
//   u32 structCount  | structs:   name, fields(name+TypeInfo), methods
//   u32 enumCount    | enums:     name, (member name, i64 value)+
//   u32 procCount    | callables: signature + VMFunction bytecode
//
// A "callable" is: name, file, params(name + TypeInfo + has-default flag),
// return TypeInfo, hasDeclaredRetType flag, bodyStart, defaultEntries, and
// a BytecodeChunk (code, constants, names, nested functions, types,
// handlers, call-site count, slot count). Call-site inline caches are
// runtime state — only their count is stored.
#include "ScriptManager.h"
#include "VM.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace Script {
namespace {

constexpr uint32_t kMagic = 0x43585343; // "CXSC"
constexpr uint32_t kVersion = 1;

// --- Writer -----------------------------------------------------------------

struct Writer {
  std::string out;
  bool ok = true;

  void u8(uint8_t v) { out.push_back(static_cast<char>(v)); }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v));
    u8(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      u8(static_cast<uint8_t>(v >> (8 * i)));
    }
  }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      u8(static_cast<uint8_t>(v >> (8 * i)));
    }
  }
  void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
  void f64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    u64(bits);
  }
  void f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(bits);
  }
  void str(const std::string &s) {
    u32(static_cast<uint32_t>(s.size()));
    out.append(s);
  }
};

// --- Reader -----------------------------------------------------------------

struct Reader {
  const char *p;
  const char *end;
  bool ok = true;
  std::string error;

  bool need(size_t n) {
    if (static_cast<size_t>(end - p) < n) {
      ok = false;
      error = "truncated artifact";
      return false;
    }
    return true;
  }
  uint8_t u8() {
    if (!need(1)) {
      return 0;
    }
    return static_cast<uint8_t>(*p++);
  }
  uint16_t u16() {
    uint16_t v = u8();
    return static_cast<uint16_t>(v | (u8() << 8));
  }
  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v |= static_cast<uint32_t>(u8()) << (8 * i);
    }
    return v;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  uint64_t u64() {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
      v |= static_cast<uint64_t>(u8()) << (8 * i);
    }
    return v;
  }
  int64_t i64() { return static_cast<int64_t>(u64()); }
  double f64() {
    uint64_t bits = u64();
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  }
  float f32() {
    uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  }
  std::string str() {
    uint32_t n = u32();
    if (!need(n)) {
      return "";
    }
    std::string s(p, n);
    p += n;
    return s;
  }
};

// --- TypeInfo ---------------------------------------------------------------

void writeTypeInfo(Writer &w, const TypeInfo &t) {
  w.u8(static_cast<uint8_t>(t.baseType));
  uint8_t flags = 0;
  if (t.isArray) flags |= 1;
  if (t.isMap) flags |= 2;
  if (t.isStruct) flags |= 4;
  if (t.isFunction) flags |= 8;
  if (t.isAuto) flags |= 16;
  if (t.fnOpaque) flags |= 32;
  w.u8(flags);
  w.u8(static_cast<uint8_t>(t.keyType));
  w.str(t.structName);
  w.u8(t.mapValueType ? 1 : 0);
  if (t.mapValueType) {
    writeTypeInfo(w, *t.mapValueType);
  }
  w.u8(t.arrayElem ? 1 : 0);
  if (t.arrayElem) {
    writeTypeInfo(w, *t.arrayElem);
  }
  w.u32(static_cast<uint32_t>(t.paramTypes.size()));
  for (const auto &pt : t.paramTypes) {
    writeTypeInfo(w, pt);
  }
  w.u8(t.retType ? 1 : 0);
  if (t.retType) {
    writeTypeInfo(w, *t.retType);
  }
}

TypeInfo readTypeInfo(Reader &r) {
  TypeInfo t;
  t.baseType = static_cast<DataType>(r.u8());
  uint8_t flags = r.u8();
  t.isArray = flags & 1;
  t.isMap = flags & 2;
  t.isStruct = flags & 4;
  t.isFunction = flags & 8;
  t.isAuto = flags & 16;
  t.fnOpaque = flags & 32;
  t.keyType = static_cast<DataType>(r.u8());
  t.structName = r.str();
  if (r.u8()) {
    t.mapValueType = std::make_shared<TypeInfo>(readTypeInfo(r));
  }
  if (r.u8()) {
    t.arrayElem = std::make_shared<TypeInfo>(readTypeInfo(r));
  }
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok; ++i) {
    t.paramTypes.push_back(readTypeInfo(r));
  }
  if (r.u8()) {
    t.retType = std::make_shared<TypeInfo>(readTypeInfo(r));
  }
  return t;
}

// --- Value (constants pool) -------------------------------------------------

enum class ValueTag : uint8_t {
  NIL = 0,
  BOOL = 1,
  CHAR = 2,
  I8 = 3,
  U8 = 4,
  I16 = 5,
  U16 = 6,
  I32 = 7,
  U32 = 8,
  I64 = 9,
  U64 = 10,
  F32 = 11,
  F64 = 12,
  STR = 13,
  ARRAY = 14,
  MAP = 15,
};

bool writeValue(Writer &w, const Value &v, std::string &err);

bool writeArray(Writer &w, const ArrayValue &a, std::string &err) {
  writeTypeInfo(w, a.elementType);
  w.u32(static_cast<uint32_t>(a.elements.size()));
  for (const auto &e : a.elements) {
    if (!writeValue(w, e, err)) {
      return false;
    }
  }
  return true;
}

bool writeMap(Writer &w, const MapValue &m, std::string &err) {
  writeTypeInfo(w, m.keyType);
  writeTypeInfo(w, m.valueType);
  w.u32(static_cast<uint32_t>(m.entries.size()));
  for (const auto &kv : m.entries) {
    if (!writeValue(w, kv.first, err) || !writeValue(w, kv.second, err)) {
      return false;
    }
  }
  return true;
}

bool writeValue(Writer &w, const Value &v, std::string &err) {
  switch (v.index()) {
  case 0: w.u8(static_cast<uint8_t>(ValueTag::CHAR)); w.u8(static_cast<uint8_t>(std::get<char>(v))); return true;
  case 1: w.u8(static_cast<uint8_t>(ValueTag::I8)); w.u8(static_cast<uint8_t>(std::get<int8_t>(v))); return true;
  case 2: w.u8(static_cast<uint8_t>(ValueTag::U8)); w.u8(std::get<uint8_t>(v)); return true;
  case 3: w.u8(static_cast<uint8_t>(ValueTag::I16)); w.u16(static_cast<uint16_t>(std::get<int16_t>(v))); return true;
  case 4: w.u8(static_cast<uint8_t>(ValueTag::U16)); w.u16(std::get<uint16_t>(v)); return true;
  case 5: w.u8(static_cast<uint8_t>(ValueTag::I32)); w.i32(std::get<int32_t>(v)); return true;
  case 6: w.u8(static_cast<uint8_t>(ValueTag::U32)); w.u32(std::get<uint32_t>(v)); return true;
  case 7: w.u8(static_cast<uint8_t>(ValueTag::I64)); w.i64(std::get<int64_t>(v)); return true;
  case 8: w.u8(static_cast<uint8_t>(ValueTag::U64)); w.u64(std::get<uint64_t>(v)); return true;
  case 9: w.u8(static_cast<uint8_t>(ValueTag::F32)); w.f32(std::get<float>(v)); return true;
  case 10: w.u8(static_cast<uint8_t>(ValueTag::F64)); w.f64(std::get<double>(v)); return true;
  case 11: w.u8(static_cast<uint8_t>(ValueTag::STR)); w.str(std::get<std::string>(v)); return true;
  case 12: w.u8(static_cast<uint8_t>(ValueTag::BOOL)); w.u8(std::get<bool>(v) ? 1 : 0); return true;
  case 13: w.u8(static_cast<uint8_t>(ValueTag::ARRAY)); return writeArray(w, *std::get<ArrayPtr>(v), err);
  case 14: w.u8(static_cast<uint8_t>(ValueTag::MAP)); return writeMap(w, *std::get<MapPtr>(v), err);
  case 17: w.u8(static_cast<uint8_t>(ValueTag::NIL)); return true;
  default:
    err = "constant value type is not serializable (struct/function)";
    return false;
  }
}

Value readValue(Reader &r) {
  auto tag = static_cast<ValueTag>(r.u8());
  switch (tag) {
  case ValueTag::NIL: return Value(NullValue{});
  case ValueTag::BOOL: return Value(r.u8() != 0);
  case ValueTag::CHAR: return Value(static_cast<char>(r.u8()));
  case ValueTag::I8: return Value(static_cast<int8_t>(r.u8()));
  case ValueTag::U8: return Value(r.u8());
  case ValueTag::I16: return Value(static_cast<int16_t>(r.u16()));
  case ValueTag::U16: return Value(r.u16());
  case ValueTag::I32: return Value(r.i32());
  case ValueTag::U32: return Value(r.u32());
  case ValueTag::I64: return Value(r.i64());
  case ValueTag::U64: return Value(r.u64());
  case ValueTag::F32: return Value(r.f32());
  case ValueTag::F64: return Value(r.f64());
  case ValueTag::STR: return Value(r.str());
  case ValueTag::ARRAY: {
    auto arr = std::make_shared<ArrayValue>();
    arr->elementType = readTypeInfo(r);
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; ++i) {
      arr->elements.push_back(readValue(r));
    }
    return Value(arr);
  }
  case ValueTag::MAP: {
    auto m = std::make_shared<MapValue>();
    m->keyType = readTypeInfo(r);
    m->valueType = readTypeInfo(r);
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; ++i) {
      Value k = readValue(r);
      Value val = readValue(r);
      m->entries.emplace(std::move(k), std::move(val));
    }
    return Value(m);
  }
  }
  r.ok = false;
  r.error = "corrupt artifact: bad value tag";
  return Value(NullValue{});
}

// --- VMFunction / BytecodeChunk ----------------------------------------------

void writeParams(Writer &w, const std::vector<Parameter> &params) {
  w.u32(static_cast<uint32_t>(params.size()));
  for (const auto &p : params) {
    w.str(p.name);
    writeTypeInfo(w, p.type);
    w.u8(p.defaultValue ? 1 : 0);
  }
}

std::vector<Parameter> readParams(Reader &r) {
  std::vector<Parameter> params;
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok; ++i) {
    Parameter p;
    p.name = r.str();
    p.type = readTypeInfo(r);
    if (r.u8()) {
      // Presence marker only: compiled defaults live in defaultEntries, so
      // the expression itself is never evaluated. A placeholder keeps
      // arity checks (requiredParamCount) working.
      p.defaultValue =
          std::make_shared<LiteralExpr>(Value(int32_t(0)),
                                        TypeInfo(DataType::INT32), 0, 0);
    }
    params.push_back(std::move(p));
  }
  return params;
}

void writeChunk(Writer &w, const BytecodeChunk &c, std::string &err);
bool writeFunction(Writer &w, const VMFunction &fn, std::string &err);

void writeChunk(Writer &w, const BytecodeChunk &c, std::string &err) {
  w.u32(static_cast<uint32_t>(c.code.size()));
  for (const auto &in : c.code) {
    w.u8(static_cast<uint8_t>(in.op));
    w.i32(in.a);
    w.i32(in.b);
    w.i32(in.c);
    w.i32(in.line);
    w.i32(in.column);
  }
  w.u32(static_cast<uint32_t>(c.constants.size()));
  for (const auto &v : c.constants) {
    if (!writeValue(w, v, err)) {
      return;
    }
  }
  w.u32(static_cast<uint32_t>(c.names.size()));
  for (const auto &n : c.names) {
    w.str(n);
  }
  w.u32(static_cast<uint32_t>(c.functions.size()));
  for (const auto &f : c.functions) {
    if (!writeFunction(w, *f, err)) {
      return;
    }
  }
  w.u32(static_cast<uint32_t>(c.types.size()));
  for (const auto &t : c.types) {
    writeTypeInfo(w, t);
  }
  w.u32(static_cast<uint32_t>(c.handlers.size()));
  for (const auto &h : c.handlers) {
    w.u32(h.tryBegin);
    w.u32(h.catchPC);
    w.u32(h.finallyPC);
    w.u32(h.endPC);
    w.u32(h.regionEnd);
    w.i32(h.catchSlot);
    w.i32(h.catchNameIdx);
    w.i32(h.catchTypeIdx);
    w.i32(h.line);
    w.i32(h.column);
  }
  w.u32(static_cast<uint32_t>(c.callSites.size()));
  w.u16(c.numSlots);
}

bool writeFunction(Writer &w, const VMFunction &fn, std::string &err) {
  w.str(fn.name);
  w.str(fn.file);
  writeParams(w, fn.params);
  writeTypeInfo(w, fn.retType);
  w.u8(fn.hasDeclaredRetType ? 1 : 0);
  w.u32(fn.bodyStart);
  w.u32(static_cast<uint32_t>(fn.defaultEntries.size()));
  for (uint32_t e : fn.defaultEntries) {
    w.u32(e);
  }
  writeChunk(w, fn.chunk, err);
  return w.ok;
}

VMFunctionPtr readFunction(Reader &r);

BytecodeChunk readChunk(Reader &r) {
  BytecodeChunk c;
  uint32_t nCode = r.u32();
  c.code.reserve(nCode);
  for (uint32_t i = 0; i < nCode && r.ok; ++i) {
    Instruction in;
    in.op = static_cast<Op>(r.u8());
    in.a = r.i32();
    in.b = r.i32();
    in.c = r.i32();
    in.line = r.i32();
    in.column = r.i32();
    c.code.push_back(in);
  }
  uint32_t nConst = r.u32();
  for (uint32_t i = 0; i < nConst && r.ok; ++i) {
    c.constants.push_back(readValue(r));
  }
  uint32_t nNames = r.u32();
  for (uint32_t i = 0; i < nNames && r.ok; ++i) {
    c.names.push_back(r.str());
  }
  uint32_t nFns = r.u32();
  for (uint32_t i = 0; i < nFns && r.ok; ++i) {
    c.functions.push_back(readFunction(r));
  }
  uint32_t nTypes = r.u32();
  for (uint32_t i = 0; i < nTypes && r.ok; ++i) {
    c.types.push_back(readTypeInfo(r));
  }
  uint32_t nHandlers = r.u32();
  for (uint32_t i = 0; i < nHandlers && r.ok; ++i) {
    HandlerInfo h;
    h.tryBegin = r.u32();
    h.catchPC = r.u32();
    h.finallyPC = r.u32();
    h.endPC = r.u32();
    h.regionEnd = r.u32();
    h.catchSlot = r.i32();
    h.catchNameIdx = r.i32();
    h.catchTypeIdx = r.i32();
    h.line = r.i32();
    h.column = r.i32();
    c.handlers.push_back(h);
  }
  uint32_t nSites = r.u32();
  c.callSites.resize(nSites); // fresh inline caches
  c.numSlots = r.u16();
  return c;
}

VMFunctionPtr readFunction(Reader &r) {
  auto fn = std::make_shared<VMFunction>();
  fn->name = r.str();
  fn->file = r.str();
  fn->params = readParams(r);
  fn->retType = readTypeInfo(r);
  fn->hasDeclaredRetType = r.u8() != 0;
  fn->bodyStart = r.u32();
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok; ++i) {
    fn->defaultEntries.push_back(r.u32());
  }
  fn->chunk = readChunk(r);
  if (!r.ok) {
    return nullptr;
  }
  return fn;
}

// Rebuild a ProcedureDecl (signature + compiled body, no AST) around a
// deserialized VMFunction.
ProcedureDeclPtr declFor(const VMFunctionPtr &fn) {
  auto proc = std::make_shared<ProcedureDecl>(fn->retType, fn->name,
                                              fn->params, nullptr);
  proc->vmFunc = fn;
  fn->decl = proc;
  return proc;
}

// Writes a callable's signature + bytecode. `name`/`file` come from the
// VMFunction; `params`/`retType` describe the declaration.
void writeCallable(Writer &w, const VMFunction &fn, std::string &err) {
  writeFunction(w, fn, err);
}

// Collect the declaration names belonging to `sourceFile`.
template <typename Map>
std::vector<std::string> namesInFile(const Map &fileMap,
                                     const std::string &sourceFile) {
  std::vector<std::string> names;
  for (const auto &kv : fileMap) {
    if (kv.second == sourceFile) {
      names.push_back(kv.first);
    }
  }
  return names;
}

} // namespace

// --- ScriptManager entry points ----------------------------------------------

bool ScriptManager::saveCompiled(const std::string &sourceFile,
                                 const std::string &outPath,
                                 std::vector<CompilationError> &errors) {
  auto fail = [&](const std::string &msg) {
    errors.push_back(CompilationError(msg, outPath, "", 0, 0));
    return false;
  };

  VM *vm = _interpreter->vm();

  // Compile every procedure (and struct method) declared by sourceFile.
  std::vector<ProcedureDeclPtr> procs;
  for (const auto &name : namesInFile(_procedureFiles, sourceFile)) {
    ProcedureDeclPtr p = _interpreter->getProcedure(name);
    if (!p) {
      continue;
    }
    if (!p->vmFunc && !vm->compileProcedure(p)) {
      return fail("cannot compile procedure '" + name +
                  "' for serialization");
    }
    procs.push_back(p);
  }
  std::vector<StructDeclPtr> structs;
  for (const auto &name : namesInFile(_structFiles, sourceFile)) {
    StructDeclPtr s = _interpreter->getStruct(name);
    if (!s) {
      continue;
    }
    for (const auto &m : s->methods) {
      if (!m->vmFunc && !vm->compileProcedure(m)) {
        return fail("cannot compile method '" + s->name + "." + m->name +
                    "' for serialization");
      }
    }
    structs.push_back(s);
  }
  std::vector<EnumDeclPtr> enums;
  for (const auto &name : namesInFile(_enumFiles, sourceFile)) {
    if (EnumDeclPtr e = _interpreter->getEnum(name)) {
      enums.push_back(e);
    }
  }
  if (procs.empty() && structs.empty() && enums.empty()) {
    return fail("no declarations found for '" + sourceFile + "'");
  }

  Writer w;
  w.u32(kMagic);
  w.u32(kVersion);
  w.str(sourceFile);

  std::string err;
  w.u32(static_cast<uint32_t>(structs.size()));
  for (const auto &s : structs) {
    w.str(s->name);
    w.u32(static_cast<uint32_t>(s->fields.size()));
    for (const auto &f : s->fields) {
      w.str(f.name);
      writeTypeInfo(w, f.type);
      w.u8(f.defaultValue ? 1 : 0);
    }
    w.u32(static_cast<uint32_t>(s->methods.size()));
    for (const auto &m : s->methods) {
      writeCallable(w, *m->vmFunc, err);
    }
  }
  w.u32(static_cast<uint32_t>(enums.size()));
  for (const auto &e : enums) {
    w.str(e->name);
    w.u32(static_cast<uint32_t>(e->members.size()));
    for (const auto &m : e->members) {
      w.str(m.first);
      w.i64(m.second);
    }
  }
  w.u32(static_cast<uint32_t>(procs.size()));
  for (const auto &p : procs) {
    writeCallable(w, *p->vmFunc, err);
  }

  if (!err.empty()) {
    return fail(err);
  }
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    return fail("cannot open '" + outPath + "' for writing");
  }
  out.write(w.out.data(), static_cast<std::streamsize>(w.out.size()));
  if (!out) {
    return fail("failed writing '" + outPath + "'");
  }
  return true;
}

bool ScriptManager::loadCompiled(const std::string &path,
                                 std::vector<CompilationError> &errors) {
  auto fail = [&](const std::string &msg) {
    errors.push_back(CompilationError(msg, path, "", 0, 0));
    return false;
  };

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fail("cannot open '" + path + "'");
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string bytes = ss.str();

  Reader r{bytes.data(), bytes.data() + bytes.size(), true, ""};
  if (r.u32() != kMagic) {
    return fail("not a CxxScript compiled artifact");
  }
  uint32_t version = r.u32();
  if (version != kVersion) {
    return fail("unsupported artifact version " + std::to_string(version));
  }
  std::string sourceFile = r.str();

  auto script = std::make_shared<Script>(path);

  uint32_t nStructs = r.u32();
  for (uint32_t i = 0; i < nStructs && r.ok; ++i) {
    auto s = std::make_shared<StructDecl>(r.str(), std::vector<Parameter>{});
    uint32_t nFields = r.u32();
    for (uint32_t j = 0; j < nFields && r.ok; ++j) {
      Parameter f;
      f.name = r.str();
      f.type = readTypeInfo(r);
      if (r.u8()) {
        f.defaultValue = std::make_shared<LiteralExpr>(
            Value(int32_t(0)), TypeInfo(DataType::INT32), 0, 0);
      }
      s->fields.push_back(std::move(f));
    }
    uint32_t nMethods = r.u32();
    for (uint32_t j = 0; j < nMethods && r.ok; ++j) {
      if (VMFunctionPtr fn = readFunction(r)) {
        s->methods.push_back(declFor(fn));
      }
    }
    script->structs.push_back(s);
  }
  uint32_t nEnums = r.u32();
  for (uint32_t i = 0; i < nEnums && r.ok; ++i) {
    auto e = std::make_shared<EnumDecl>(
        r.str(), std::vector<std::pair<std::string, int64_t>>{});
    uint32_t n = r.u32();
    for (uint32_t j = 0; j < n && r.ok; ++j) {
      std::string member = r.str();
      int64_t memberVal = r.i64();
      e->members.emplace_back(std::move(member), memberVal);
    }
    script->enums.push_back(e);
  }
  uint32_t nProcs = r.u32();
  for (uint32_t i = 0; i < nProcs && r.ok; ++i) {
    if (VMFunctionPtr fn = readFunction(r)) {
      script->procedures.push_back(declFor(fn));
    }
  }

  if (!r.ok || r.p != r.end) {
    return fail(r.error.empty() ? "corrupt artifact" : r.error);
  }

  // Compiled bodies have no AST — they require the bytecode VM.
  _interpreter->setVMEnabled(true);
  _interpreter->loadScript(script);
  for (const auto &p : script->procedures) {
    _procedureFiles[p->name] = path;
  }
  for (const auto &s : script->structs) {
    _structFiles[s->name] = path;
  }
  for (const auto &e : script->enums) {
    _enumFiles[e->name] = path;
  }
  (void)sourceFile;
  return true;
}

} // namespace Script

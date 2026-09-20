#pragma once

#include "AST.h"
#include "DataTypes.h"
#include <string>
#include <unordered_map>

namespace Script {

class Interpreter;

// Built-in functions callable from scripts (len, substr, abs, print, ...).
// Implemented as a friend of Interpreter so handlers can evaluate argument
// expressions and report located runtime errors.
class Builtins {
public:
  static bool isBuiltin(const std::string &name);
  static Value call(Interpreter &interp, const std::string &name,
                    CallExpr *expr);

private:
  using Handler = Value (*)(Interpreter &, CallExpr *);
  static const std::unordered_map<std::string, Handler> &table();

  // Argument helpers
  static void arity(Interpreter &in, CallExpr *e, const char *name, size_t min,
                    size_t max);
  static Value arg(Interpreter &in, CallExpr *e, size_t i);
  static std::string str(Interpreter &in, CallExpr *e, size_t i);
  static int64_t integer(Interpreter &in, CallExpr *e, size_t i);
  static double real(Interpreter &in, CallExpr *e, size_t i);
  static bool boolean(Interpreter &in, CallExpr *e, size_t i);
  static ArrayPtr array(Interpreter &in, CallExpr *e, size_t i);

  // Handlers
  static Value bLen(Interpreter &, CallExpr *);
  static Value bPush(Interpreter &, CallExpr *);
  static Value bPop(Interpreter &, CallExpr *);
  static Value bInsert(Interpreter &, CallExpr *);
  static Value bRemoveAt(Interpreter &, CallExpr *);
  static Value bClear(Interpreter &, CallExpr *);
  static Value bHas(Interpreter &, CallExpr *);
  static Value bRemove(Interpreter &, CallExpr *);
  static Value bKeys(Interpreter &, CallExpr *);
  static Value bValues(Interpreter &, CallExpr *);
  static Value bSize(Interpreter &, CallExpr *);
  static Value bIsMap(Interpreter &, CallExpr *);
  static Value bSubstr(Interpreter &, CallExpr *);
  static Value bCharAt(Interpreter &, CallExpr *);
  static Value bIndexOf(Interpreter &, CallExpr *);
  static Value bContains(Interpreter &, CallExpr *);
  static Value bStartsWith(Interpreter &, CallExpr *);
  static Value bEndsWith(Interpreter &, CallExpr *);
  static Value bToUpper(Interpreter &, CallExpr *);
  static Value bToLower(Interpreter &, CallExpr *);
  static Value bTrim(Interpreter &, CallExpr *);
  static Value bReplace(Interpreter &, CallExpr *);
  static Value bSplit(Interpreter &, CallExpr *);
  static Value bJoin(Interpreter &, CallExpr *);
  static Value bRepeat(Interpreter &, CallExpr *);
  static Value bReverse(Interpreter &, CallExpr *);
  static Value bFormat(Interpreter &, CallExpr *);
  static Value bAbs(Interpreter &, CallExpr *);
  static Value bMin(Interpreter &, CallExpr *);
  static Value bMax(Interpreter &, CallExpr *);
  static Value bClamp(Interpreter &, CallExpr *);
  static Value bPow(Interpreter &, CallExpr *);
  static Value bSqrt(Interpreter &, CallExpr *);
  static Value bFloor(Interpreter &, CallExpr *);
  static Value bCeil(Interpreter &, CallExpr *);
  static Value bRound(Interpreter &, CallExpr *);
  static Value bTrunc(Interpreter &, CallExpr *);
  static Value bFmod(Interpreter &, CallExpr *);
  static Value bSin(Interpreter &, CallExpr *);
  static Value bCos(Interpreter &, CallExpr *);
  static Value bTan(Interpreter &, CallExpr *);
  static Value bAsin(Interpreter &, CallExpr *);
  static Value bAcos(Interpreter &, CallExpr *);
  static Value bAtan(Interpreter &, CallExpr *);
  static Value bAtan2(Interpreter &, CallExpr *);
  static Value bExp(Interpreter &, CallExpr *);
  static Value bLog(Interpreter &, CallExpr *);
  static Value bLog10(Interpreter &, CallExpr *);
  static Value bRandom(Interpreter &, CallExpr *);
  static Value bRandInt(Interpreter &, CallExpr *);
  static Value bSrand(Interpreter &, CallExpr *);
  static Value bPi(Interpreter &, CallExpr *);
  static Value bToInt(Interpreter &, CallExpr *);
  static Value bToUInt(Interpreter &, CallExpr *);
  static Value bToDouble(Interpreter &, CallExpr *);
  static Value bToFloat(Interpreter &, CallExpr *);
  static Value bToString(Interpreter &, CallExpr *);
  static Value bToBool(Interpreter &, CallExpr *);
  static Value bToChar(Interpreter &, CallExpr *);
  static Value bParseInt(Interpreter &, CallExpr *);
  static Value bParseDouble(Interpreter &, CallExpr *);
  static Value bTypeof(Interpreter &, CallExpr *);
  static Value bIsArray(Interpreter &, CallExpr *);
  static Value bPrint(Interpreter &, CallExpr *);
  static Value bPrintln(Interpreter &, CallExpr *);
  static Value bError(Interpreter &, CallExpr *);
  static Value bAssert(Interpreter &, CallExpr *);
};

} // namespace Script

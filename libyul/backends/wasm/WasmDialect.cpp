/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Dialects for Wasm.
 */

#include <libyul/backends/wasm/WasmDialect.h>

#include <libyul/AsmData.h>
#include <libyul/Exceptions.h>

using namespace std;
using namespace solidity::yul;

std::string const WasmDialect::i64{"i64"};
std::string const WasmDialect::i32{"i32"};
std::string const WasmDialect::i32ptr{"i32"}; // Uses "i32" on purpose.

WasmDialect::WasmDialect()
{
	YulString i64_type = "i64"_yulstring;
	YulString i32_type = "i32"_yulstring;
	defaultType = i64_type;
	boolType = i32_type;
	types = {i64_type, i32_type};

	for (auto t: types)
		for (auto const& name: {
			"add",
			"sub",
			"mul",
			// TODO: div_s
			"div_u",
			// TODO: rem_s
			"rem_u",
			"and",
			"or",
			"xor",
			"shl",
			// TODO: shr_s
			"shr_u",
			// TODO: rotl
			// TODO: rotr
		})
			addFunction(t.str() + "." + name, {t, t}, {t});

	for (auto t: types)
		for (auto const& name: {
			"eq",
			"ne",
			// TODO: lt_s
			"lt_u",
			// TODO: gt_s
			"gt_u",
			// TODO: le_s
			"le_u",
			// TODO: ge_s
			"ge_u"
		})
			addFunction(t.str() + "." + name, {t, t}, {i32_type});

	addFunction("i32.eqz", {i32_type}, {i32_type});
	addFunction("i64.eqz", {i64_type}, {i32_type});

	for (auto t: types)
		for (auto const& name: {
			"clz",
			"ctz",
			"popcnt",
		})
			addFunction(t.str() + "." + name, {t}, {t});

	addFunction("i32.wrap_i64", {i64_type}, {i32_type});

	addFunction("i64.extend_i32_u", {i32_type}, {i64_type});

	addFunction("i32.store", {i32_type, i32_type}, {}, false);
	m_functions["i32.store"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i32.store"_yulstring].sideEffects.otherState = SideEffects::None;
	addFunction("i64.store", {i32_type, i64_type}, {}, false);
	// TODO: add i32.store16, i64.store8, i64.store16, i64.store32
	m_functions["i64.store"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i64.store"_yulstring].sideEffects.otherState = SideEffects::None;

	addFunction("i32.store8", {i32_type, i32_type}, {}, false);
	m_functions["i32.store8"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i32.store8"_yulstring].sideEffects.otherState = SideEffects::None;

	addFunction("i64.store8", {i32_type, i64_type}, {}, false);
	m_functions["i64.store8"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i64.store8"_yulstring].sideEffects.otherState = SideEffects::None;

	addFunction("i32.load", {i32_type}, {i32_type}, false);
	m_functions["i32.load"_yulstring].sideEffects.canBeRemoved = true;
	m_functions["i32.load"_yulstring].sideEffects.canBeRemovedIfNoMSize = true;
	m_functions["i32.load"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i32.load"_yulstring].sideEffects.memory = SideEffects::Read;
	m_functions["i32.load"_yulstring].sideEffects.otherState = SideEffects::None;
	addFunction("i64.load", {i32_type}, {i64_type}, false);
	// TODO: add i32.load8, i32.load16, i64.load8, i64.load16, i64.load32
	m_functions["i64.load"_yulstring].sideEffects.canBeRemoved = true;
	m_functions["i64.load"_yulstring].sideEffects.canBeRemovedIfNoMSize = true;
	m_functions["i64.load"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["i64.load"_yulstring].sideEffects.memory = SideEffects::Read;
	m_functions["i64.load"_yulstring].sideEffects.otherState = SideEffects::None;

	// Drop is actually overloaded for all types, but Yul does not support that.
	// Because of that, we introduce "i32.drop" and "i64.drop".
	addFunction("i32.drop", {i32_type}, {});
	addFunction("i64.drop", {i64_type}, {});

	addFunction("nop", {}, {});
	addFunction("unreachable", {}, {}, false);
	m_functions["unreachable"_yulstring].sideEffects.storage = SideEffects::None;
	m_functions["unreachable"_yulstring].sideEffects.memory = SideEffects::None;
	m_functions["unreachable"_yulstring].sideEffects.otherState = SideEffects::None;
	m_functions["unreachable"_yulstring].controlFlowSideEffects.terminates = true;
	m_functions["unreachable"_yulstring].controlFlowSideEffects.reverts = true;

	addFunction("datasize", {i64_type}, {i64_type}, true, {LiteralKind::String});
	addFunction("dataoffset", {i64_type}, {i64_type}, true, {LiteralKind::String});

	addEthereumExternals();
	addDebugExternals();
}

BuiltinFunction const* WasmDialect::builtin(YulString _name) const
{
	auto it = m_functions.find(_name);
	if (it != m_functions.end())
		return &it->second;
	else
		return nullptr;
}

BuiltinFunction const* WasmDialect::discardFunction(YulString _type) const
{
	if (_type == "i32"_yulstring)
		return builtin("i32.drop"_yulstring);
	yulAssert(_type == "i64"_yulstring, "");
	return builtin("i64.drop"_yulstring);
}

BuiltinFunction const* WasmDialect::equalityFunction(YulString _type) const
{
	if (_type == "i32"_yulstring)
		return builtin("i32.eq"_yulstring);
	yulAssert(_type == "i64"_yulstring, "");
	return builtin("i64.eq"_yulstring);
}

WasmDialect const& WasmDialect::instance()
{
	static std::unique_ptr<WasmDialect> dialect;
	static YulStringRepository::ResetCallback callback{[&] { dialect.reset(); }};
	if (!dialect)
		dialect = make_unique<WasmDialect>();
	return *dialect;
}

void WasmDialect::addEthereumExternals()
{
	static vector<External> externals{
		{"getAddress", {i32ptr}, {}},
		{"getExternalBalance", {i32ptr, i32ptr}, {}},
		{"getBlockHash", {i64, i32ptr}, {i32}},
		{"call", {i64, i32ptr, i32ptr, i32ptr, i32}, {i32}},
		{"callDataCopy", {i32ptr, i32, i32}, {}},
		{"getCallDataSize", {}, {i32}},
		{"callCode", {i64, i32ptr, i32ptr, i32ptr, i32}, {i32}},
		{"callDelegate", {i64, i32ptr, i32ptr, i32}, {i32}},
		{"callStatic", {i64, i32ptr, i32ptr, i32}, {i32}},
		{"storageStore", {i32ptr, i32ptr}, {}},
		{"storageLoad", {i32ptr, i32ptr}, {}},
		{"getCaller", {i32ptr}, {}},
		{"getCallValue", {i32ptr}, {}},
		{"codeCopy", {i32ptr, i32, i32}, {}},
		{"getCodeSize", {}, {i32}},
		{"getBlockCoinbase", {i32ptr}, {}},
		{"create", {i32ptr, i32ptr, i32, i32ptr}, {i32}},
		{"getBlockDifficulty", {i32ptr}, {}},
		{"externalCodeCopy", {i32ptr, i32ptr, i32, i32}, {}},
		{"getExternalCodeSize", {i32ptr}, {i32}},
		{"getGasLeft", {}, {i64}},
		{"getBlockGasLimit", {}, {i64}},
		{"getTxGasPrice", {i32ptr}, {}},
		{"log", {i32ptr, i32, i32, i32ptr, i32ptr, i32ptr, i32ptr}, {}},
		{"getBlockNumber", {}, {i64}},
		{"getTxOrigin", {i32ptr}, {}},
		{"finish", {i32ptr, i32}, {}, ControlFlowSideEffects{true, false}},
		{"revert", {i32ptr, i32}, {}, ControlFlowSideEffects{true, true}},
		{"getReturnDataSize", {}, {i32}},
		{"returnDataCopy", {i32ptr, i32, i32}, {}},
		{"selfDestruct", {i32ptr}, {}, ControlFlowSideEffects{true, false}},
		{"getBlockTimestamp", {}, {i64}}};
	for (External const& ext: externals)
	{
		YulString name{"eth." + ext.name};
		BuiltinFunction& f = m_functions[name];
		f.name = name;
		for (string const& p: ext.parameters)
			f.parameters.emplace_back(YulString(p));
		for (string const& p: ext.returns)
			f.returns.emplace_back(YulString(p));
		// TODO some of them are side effect free.
		f.sideEffects = SideEffects::worst();
		f.controlFlowSideEffects = ext.controlFlowSideEffects;
		f.isMSize = false;
		f.literalArguments.clear();
	}
}

void WasmDialect::addDebugExternals()
{
	static vector<External> debugExternals {
		{"print32", {i32}, {}},
		{"print64", {i64}, {}},
		{"printMem", {i32, i32}, {}},
		{"printMemHex", {i32, i32}, {}},
		{"printStorage", {i32}, {}},
		{"printStorageHex", {i32}, {}},
	};
	for (External const& ext: debugExternals)
	{
		YulString name{"debug." + ext.name};
		BuiltinFunction& f = m_functions[name];
		f.name = name;
		for (string const& p: ext.parameters)
			f.parameters.emplace_back(YulString(p));
		for (string const& p: ext.returns)
			f.returns.emplace_back(YulString(p));
		// TODO some of them are side effect free.
		f.sideEffects = SideEffects::worst();
		f.controlFlowSideEffects = ext.controlFlowSideEffects;
		f.isMSize = false;
		f.literalArguments.clear();
	}
}

void WasmDialect::addFunction(
	string _name,
	vector<YulString> _params,
	vector<YulString> _returns,
	bool _movable,
	vector<optional<LiteralKind>> _literalArguments
)
{
	YulString name{move(_name)};
	BuiltinFunction& f = m_functions[name];
	f.name = name;
	f.parameters = std::move(_params);
	yulAssert(_returns.size() <= 1, "The Wasm 1.0 specification only allows up to 1 return value.");
	f.returns = std::move(_returns);
	f.sideEffects = _movable ? SideEffects{} : SideEffects::worst();
	f.isMSize = false;
	f.literalArguments = std::move(_literalArguments);
}

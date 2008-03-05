/*
 * Copyright (c) 2005-2008  cielacanth <cielacanth AT s60.xrea.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "precomp.h"
#include "lldebug.h"
#include "configfile.h"
#include "net/remoteengine.h"
#include "context/codeconv.h"
#include "context/context.h"
#include "context/luautils.h"
#include "context/luaiterate.h"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <fstream>
#include <sstream>

/// Dummy function name for eval.
#define DUMMY_FUNCNAME "__LLDEBUG_DUMMY_FUNCTION__"

namespace lldebug {
namespace context {

/**
 * @brief Convert lua_State* to 'Context' used by Context::Find and others.
 */
class Context::ContextManager {
public:
	explicit ContextManager() {
	}

	~ContextManager() {
	}

	/// Get weather this object is empty.
	bool IsEmpty() {
		scoped_lock lock(m_mutex);

		return m_map.empty();
	}

	/// Add the pair of (L, ctx).
	void Add(shared_ptr<Context> ctx, lua_State *L) {
		scoped_lock lock(m_mutex);

		if (ctx == NULL || L == NULL) {
			return;
		}

		m_map.insert(std::make_pair(L, ctx));
	}

	/// Erase (not delete) the 'ctx' and corresponding lua_State objects.
	void Erase(shared_ptr<Context> ctx) {
		scoped_lock lock(m_mutex);

		if (ctx == NULL) {
			return;
		}

		for (Map::iterator it = m_map.begin(); it != m_map.end(); ) {
			shared_ptr<Context> p = (*it).second;
 
			if (p == ctx) {
				m_map.erase(it++);
			}
			else {
				++it;
			}
		}
	}

	/// Find the Context object from a lua_State object.
	shared_ptr<Context> Find(lua_State *L) {
		scoped_lock lock(m_mutex);

		if (L == NULL) {
			return shared_ptr<Context>();
		}

		Map::iterator it = m_map.find(L);
		if (it == m_map.end()) {
			return shared_ptr<Context>();
		}

		return (*it).second;
	}

private:
	typedef std::map<lua_State *, shared_ptr<Context> > Map;
	Map m_map;
	mutex m_mutex;
};


/*-----------------------------------------------------------------*/
shared_ptr<Context::ContextManager> Context::ms_manager;
int Context::ms_idCounter = 0;

Context::Context()
	: m_id(0), m_lua(NULL), m_state(STATE_INITIAL)
	, m_isEnabled(true), m_updateCount(0)
	, m_waitUpdateCount(0), m_isMustUpdate(false)
	, m_engine(new RemoteEngine)
	, m_sourceManager(m_engine), m_breakpoints(m_engine) {

	m_engine->SetOnRemoteCommand(
		boost::bind1st(
			boost::mem_fn(&Context::OnRemoteCommand),
			this));
	m_id = ms_idCounter++;
}

int Context::Initialize() {
	scoped_lock lock(m_mutex);

	// It may call the dll function, so the error check is necessary.
	lua_State *L = lua_open();
	if (L == NULL) {
		return -1;
	}

	if (LuaInitialize(L) != 0) {
		lua_close(L);
		return -1;
	}

	SetHook(L);
	m_lua = L;
	m_state = STATE_STEPINTO; //NORMAL;
	m_coroutines.push_back(CoroutineInfo(L));

	// Add this to manager.
	if (ms_manager == NULL) {
		ms_manager.reset(new ContextManager);
	}
	ms_manager->Add(shared_from_this(), L);

	// After the all initialization was done,
	// we create a new frame for this context.
	if (CreateDebuggerFrame() != 0) {
		return -1;
	}

	return 0;
}

/// Create the new debug frame.
int Context::CreateDebuggerFrame() {
	scoped_lock lock(m_mutex);

	/*HINSTANCE inst = ::ShellExecuteA(
		NULL, NULL,
		"..\\debug\\lldebug_frame.exe",
		"localhost 51123",
		"",
		SW_SHOWNORMAL);
	if ((unsigned long int)inst <= 32) {
		return -1;
	}*/

	const char *hostName_, *serviceName_;
	lldebug_getremoteaddress(&hostName_, &serviceName_);
	std::string hostName = hostName_;
	std::string serviceName = serviceName_;

	// Try to connect the debug frame (At least, wait for 5 seconds).
	if (m_engine->StartContext(hostName, serviceName, 10) != 0) {
		std::cerr
			<< "lldebug doesn't work correctly, "
			   "because the frame was not found."
			<< std::endl
			<< "Now this program starts without debugging."
			<< std::endl
			<< "(If you want to debug visually, "
			<< "please excute 'lldebug_frame.exe' first.)"
			<< std::endl;
		SetDebugEnable(false);
	}

	return 0;
}

Context::~Context() {
	scoped_lock lock(m_mutex);

	SaveConfig();
	m_engine.reset();

	if (m_lua != NULL) {
		lua_close(m_lua);
		m_lua = NULL;
	}
}

void Context::Delete() {
	if (ms_manager == NULL) {
		return;
	}

	// Erase this from the context manager.
	ms_manager->Erase(shared_from_this());

	if (ms_manager->IsEmpty()) {
		ms_manager.reset();
	}
}

int Context::LoadConfig() {
	scoped_lock lock(m_mutex);

	if (m_rootFileKey.empty()) {
		return -1;
	}

	try {
		// Detect filenames.
		std::string filename = EncodeToFilename(m_rootFileKey);
		std::string filepath = GetConfigFileName(filename + ".xml");

		// Open the config file.
		scoped_locale sloc(std::locale(""));
		std::ifstream ifs(filepath.c_str());
		if (!ifs.is_open()) {
			return -1;
		}
		
		boost::archive::xml_iarchive ar(ifs);
		BreakpointList bps(m_engine);
		ar >> BOOST_SERIALIZATION_NVP(bps);

		// Set values.
		m_breakpoints = bps;
		m_engine->ChangedBreakpointList(m_breakpoints);
	}
	catch (std::exception &) {
		OutputError("Couldn't open the config file.");
	}

	return 0;
}

int Context::SaveConfig() {
	scoped_lock lock(m_mutex);

	if (m_rootFileKey.empty()) {
		return -1;
	}

	try {
		// Detect filenames.
		std::string filename = EncodeToFilename(m_rootFileKey);
		std::string filepath = GetConfigFileName(filename + ".xml");

		// Open the config file.
		scoped_locale sloc(std::locale(""));
		safe_ofstream sfs;
		if (!sfs.open(filepath, std::ios_base::out)) {
			return -1;
		}
		
		boost::archive::xml_oarchive ar(sfs.stream());
		ar << LLDEBUG_MEMBER_NVP(breakpoints);
		sfs.close();
	}
	catch (std::exception &ex) {
		OutputError(std::string("Couldn't save the config file (") + ex.what() + ").");
		throw;
	}

	return 0;
}

shared_ptr<Context> Context::Find(lua_State *L) {
	if (ms_manager == NULL) {
		return shared_ptr<Context>();
	}

	return ms_manager->Find(L);
}

void Context::Quit() {
	scoped_lock lock(m_mutex);

	SetState(STATE_QUIT);
}

/// Parse the lua error that forat is like 'FILENAME:LINE:str...'.
Context::LuaErrorData Context::ParseLuaError(const std::string &str) {
	scoped_lock lock(m_mutex);

	// FILENAME:LINE:str...
	bool found = false;
	int line;
	std::string::size_type pos = 0, prevPos = 0;
	while ((prevPos = str.find(':', prevPos)) != str.npos) {
		pos = prevPos;

		// parse line number
		line = 0;
		while (++pos < str.length()) {
			char c = str[pos];

			if (c < '0' || '9' < c) {
				break;
			}
			line = line * 10 + (c - '0');
		}

		// If there is ':', parse is success.
		if (pos < str.length() && str[pos] == ':') {
			found = true;
			break;
		}

		// str[prevPos] is ':', so advance prevPos.
		++prevPos;
	}

	// source is file, key:line: str...
	// source is string, [string "***"]:line: str...
	//               ,or [string "***..."]:line: str...
	// If found is true, str[prevPos] is first ':', str[pos] is second ':'.
	if (found) {
		std::string filename = str.substr(0, prevPos);
		const char *beginStr = "[string \"";
		const char *endStr = "\"]";

		// msg is what is removed filename and line from str.
		std::string msg =
			str.substr((std::min)(pos + 2, str.length())); // skip ": "

		// If source is string, key is manipulated and may be shorten.
		if (filename.find(beginStr) == 0 &&
			filename.rfind(endStr) == filename.length() - strlen(endStr)) {
			filename = filename.substr(
				strlen(beginStr),
				filename.length() - strlen(beginStr) - strlen(endStr));

			// If key is "***...", "..." must be removed.
			if (filename.rfind("...") == filename.length() - 3) {
				filename = filename.substr(0, filename.length() - 3);
			}
 
			// Replace key if any.
			const Source *source = m_sourceManager.GetString(filename);
			if (source != NULL) {
				return LuaErrorData(msg, source->GetKey(), line);
			}
			else if (filename == DUMMY_FUNCNAME) {
				return LuaErrorData(msg, "", -1);
			}
		}
		else {
			return LuaErrorData(msg, std::string("@") + filename, line);
		}
	}

	// Come here when str can't be parsed or filename is invalid.
	return LuaErrorData(str, "", -1);
}

void Context::OutputLog(LogType type, const std::string &str) {
	scoped_lock lock(m_mutex);

	LuaErrorData data = ParseLuaError(str);
	m_engine->OutputLog(type, data.message, data.filekey, data.line);
}

void Context::OutputLuaError(const char *str) {
	scoped_lock lock(m_mutex);

	if (str == NULL) {
		return;
	}

	OutputLog(LOGTYPE_ERROR, str);
}

void Context::OutputLog(const std::string &str) {
	scoped_lock lock(m_mutex);

	OutputLog(LOGTYPE_MESSAGE, str);
}

void Context::OutputError(const std::string &str) {
	scoped_lock lock(m_mutex);

	OutputLog(LOGTYPE_ERROR, str);
}

void Context::BeginCoroutine(lua_State *L) {
	scoped_lock lock(m_mutex);

	CoroutineInfo info(L);
	m_coroutines.push_back(info);
}

void Context::EndCoroutine(lua_State *L) {
	scoped_lock lock(m_mutex);

	if (m_coroutines.empty() || m_coroutines.back().L != L) {
		assert(0 && "Couldn't end coroutine.");
		return;
	}

	// When it goes through the coroutine set break mark,
	// we force to break.
	if (m_state == STATE_STEPOVER || m_state == STATE_STEPRETURN) {
		if (m_stepinfo.L == L) {
			SetState(STATE_BREAK);
		}
	}

	m_coroutines.pop_back();
}

void Context::SetHook(lua_State *L) {
	int mask = LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET;
	lua_sethook(L, Context::s_HookCallback, mask, 0);
}

void Context::s_HookCallback(lua_State *L, lua_Debug *ar) {
	shared_ptr<Context> ctx = Context::Find(L);

	if (ctx != NULL) {
		ctx->HookCallback(L, ar);
	}
}

void Context::SetState(State state) {
	assert(state != STATE_INITIAL);
	scoped_lock lock(m_mutex);

	if (state == m_state) {
		return;
	}

	if (state == STATE_QUIT) {
		m_state = state;
		return;
	}

	switch (m_state) {
	case STATE_INITIAL:
		m_state = state;
		break;
	case STATE_NORMAL: // running or stable
		switch (state) {
		case STATE_BREAK:
			m_state = state;
			m_engine->ChangedState(true);
			break;
		case STATE_STEPOVER:
		case STATE_STEPINTO:
		case STATE_STEPRETURN:
			/* ignore */
			break;
		default:
			/* error */
			assert(false && "Value of 'state' is illegal.");
			return;
		}
		break;
	case STATE_BREAK:
		if (m_waitUpdateCount > 0) {
			return; // ignore
		}
		switch (state) {
		case STATE_NORMAL:
			m_state = state;
			m_engine->ChangedState(false);
			break;
		case STATE_STEPOVER:
		case STATE_STEPINTO:
		case STATE_STEPRETURN:
			m_state = state;
			m_engine->ChangedState(false);
			break;
		default:
			/* error */
			assert(false && "Value of 'state' is illegal.");
			return;
		}
		break;
	case STATE_STEPOVER:
	case STATE_STEPINTO:
	case STATE_STEPRETURN:
		switch (state) {
		case STATE_NORMAL:
		case STATE_STEPOVER:
		case STATE_STEPINTO:
		case STATE_STEPRETURN:
			/* ignore */
			break;
		case STATE_BREAK:
			m_engine->ChangedState(true);
			m_state = state;
			break;
		default:
			/* error */
			assert(false && "Value of 'state' is illegal.");
			return;
		}
		break;
	case STATE_QUIT:
		/* ignore */
		break;
	default:
		/* error */
		assert(false && "Value of 'm_state' is illegal.");
		return;
	}

	// Set stepinfo that has lua_State* and call count.
	if (m_state == STATE_STEPOVER || m_state == STATE_STEPRETURN) {
		m_stepinfo = m_coroutines.back();
	}
}

void Context::OnRemoteCommand(const Command &command) {
	m_readCommands.push(command);
	m_commandCond.notify_all();
}

int Context::HandleCommand() {
	scoped_lock lock(m_mutex);

	// Process the command.
	while (!m_readCommands.empty()) {
		Command command = m_readCommands.front();
		m_readCommands.pop();

		if (command.IsResponse()) {
			command.CallResponse();
			continue;
		}

		switch (command.GetType()) {
		case REMOTECOMMANDTYPE_END_CONNECTION:
			Quit();
			return -1;
		case REMOTECOMMANDTYPE_BREAK:
			SetState(STATE_BREAK);
			break;
		case REMOTECOMMANDTYPE_RESUME:
			SetState(STATE_NORMAL);
			break;
		case REMOTECOMMANDTYPE_STEPINTO:
			SetState(STATE_STEPINTO);
			break;
		case REMOTECOMMANDTYPE_STEPOVER:
			SetState(STATE_STEPOVER);
			break;
		case REMOTECOMMANDTYPE_STEPRETURN:
			SetState(STATE_STEPRETURN);
			break;

		case REMOTECOMMANDTYPE_FORCE_UPDATESOURCE:
			m_isMustUpdate = true;
			break;

		case REMOTECOMMANDTYPE_SAVE_SOURCE:
			{
				std::string key;
				string_array sources;
				command.GetData().Get_SaveSource(key, sources);
				m_sourceManager.Save(key, sources);
			}
			break;

		case REMOTECOMMANDTYPE_SET_UPDATECOUNT:
			{
				int count;
				command.GetData().Get_SetUpdateCount(count);
				if (count > m_updateCount) {
					m_updateCount = count;
				}
			}
			break;

		case REMOTECOMMANDTYPE_EVALS_TO_VARLIST:
			{
				string_array evals;
				LuaStackFrame stackFrame;
				command.GetData().Get_EvalsToVarList(evals, stackFrame);
				m_engine->ResponseVarList(command, LuaEvalsToVarList(evals, stackFrame, true));
			}
			break;
		case REMOTECOMMANDTYPE_EVAL_TO_MULTIVAR:
			{
				std::string eval;
				LuaStackFrame stackFrame;
				command.GetData().Get_EvalToMultiVar(eval, stackFrame);
				m_engine->ResponseVarList(command, LuaEvalToMultiVar(eval, stackFrame, true));
			}
			break;
		case REMOTECOMMANDTYPE_EVAL_TO_VAR:
			{
				std::string eval;
				LuaStackFrame stackFrame;
				command.GetData().Get_EvalToVar(eval, stackFrame);
				m_engine->ResponseVar(command, LuaEvalToVar(eval, stackFrame, true));
			}
			break;
		

		case REMOTECOMMANDTYPE_SET_BREAKPOINT:
			{
				Breakpoint bp;
				command.GetData().Get_SetBreakpoint(bp);
				m_breakpoints.Set(bp);
			}
			break;
		case REMOTECOMMANDTYPE_REMOVE_BREAKPOINT:
			{
				Breakpoint bp;
				command.GetData().Get_RemoveBreakpoint(bp);
				m_breakpoints.Remove(bp);
			}
			break;

		case REMOTECOMMANDTYPE_REQUEST_FIELDSVARLIST:
			{
				LuaVar var;
				command.GetData().Get_RequestFieldVarList(var);
				m_engine->ResponseVarList(command, LuaGetFields(var));
			}
			break;
		case REMOTECOMMANDTYPE_REQUEST_LOCALVARLIST:
			{
				LuaStackFrame stackFrame;
				bool checkLocal, checkUpvalue, checkEnviron;
				command.GetData().Get_RequestLocalVarList(
					stackFrame, checkLocal, checkUpvalue, checkEnviron);
				m_engine->ResponseVarList(command, LuaGetLocals(stackFrame,
					checkLocal, checkUpvalue, checkEnviron));
			}
			break;
		case REMOTECOMMANDTYPE_REQUEST_GLOBALVARLIST:
			m_engine->ResponseVarList(command, LuaGetFields(TABLETYPE_GLOBAL));
			break;
		case REMOTECOMMANDTYPE_REQUEST_REGISTRYVARLIST:
			m_engine->ResponseVarList(command, LuaGetFields(TABLETYPE_REGISTRY));
			break;
		case REMOTECOMMANDTYPE_REQUEST_STACKLIST:
			m_engine->ResponseVarList(command, LuaGetStack());
			break;
		case REMOTECOMMANDTYPE_REQUEST_SOURCE:
			{
				std::string key;
				command.GetData().Get_RequestSource(key);
				const Source *source = m_sourceManager.Get(key);
				m_engine->ResponseSource(command, (source != NULL ? *source : Source()));
			}
			break;
		case REMOTECOMMANDTYPE_REQUEST_BACKTRACELIST:
			m_engine->ResponseBacktraceList(command, LuaGetBacktrace());
			break;

		case REMOTECOMMANDTYPE_SUCCESSED:
		case REMOTECOMMANDTYPE_FAILED:
		case REMOTECOMMANDTYPE_START_CONNECTION:
		case REMOTECOMMANDTYPE_CHANGED_STATE:
		case REMOTECOMMANDTYPE_UPDATE_SOURCE:
		case REMOTECOMMANDTYPE_ADDED_SOURCE:
		case REMOTECOMMANDTYPE_CHANGED_BREAKPOINTLIST:
		case REMOTECOMMANDTYPE_OUTPUT_LOG:
		case REMOTECOMMANDTYPE_VALUE_STRING:
		case REMOTECOMMANDTYPE_VALUE_SOURCE:
		case REMOTECOMMANDTYPE_VALUE_BREAKPOINTLIST:
		case REMOTECOMMANDTYPE_VALUE_VAR:
		case REMOTECOMMANDTYPE_VALUE_VARLIST:
		case REMOTECOMMANDTYPE_VALUE_BACKTRACELIST:
			break;
		}
	}

	return 0;
}

/**
 * @brief Waiter for the callback of 'UpdateSource'.
 */
struct UpdateResponseWaiter {
	explicit UpdateResponseWaiter(int *count)
		: m_count(count) {
		++*count;
	}

	int operator()(const Command &command) {
		--*m_count;
		return 0;
	}

	private:
	int *m_count;
	};

void Context::HookCallback(lua_State *L, lua_Debug *ar) {
	if (!m_isEnabled) {
		return;
	}

	scoped_lock lock(m_mutex);
	assert(m_state != STATE_INITIAL && "Not initialized !!!");

	if (false) {
		lua_getinfo(L, "nSl", ar);
		switch (ar->event) {
		case LUA_HOOKCALL:
			printf("OnCall: %s\n", llutil_makefuncname(ar).c_str());
			break;
		case LUA_HOOKRET:
			printf("OnReturn: %s\n", llutil_makefuncname(ar).c_str());
			break;
		case LUA_HOOKTAILRET:
			printf("OnTailReturn: %s\n", llutil_makefuncname(ar).c_str());
			break;
		}
	}

	switch (ar->event) {
	case LUA_HOOKCALL:
		++m_coroutines.back().call;
		return;
	case LUA_HOOKRET:
	case LUA_HOOKTAILRET:
		if (m_state == STATE_STEPRETURN) {
			const CoroutineInfo &info = m_coroutines.back();
			if (m_stepinfo.L == info.L  && info.call <= m_stepinfo.call) {
				SetState(STATE_BREAK);
			}
		}
		--m_coroutines.back().call;
		return;
	default:
		break;
	}

	// Stop running if need.
	switch (m_state) {
	case STATE_QUIT:
		luaL_error(L, "quited");
		return;
	case STATE_STEPOVER: {
		const CoroutineInfo &info = m_coroutines.back();
		if (m_stepinfo.L == info.L  && info.call <= m_stepinfo.call) {
			SetState(STATE_BREAK);
		}
		}
		break;
	case STATE_STEPINTO:
		SetState(STATE_BREAK);
		break;
	case STATE_NORMAL:
	case STATE_STEPRETURN:
	case STATE_BREAK:
		break;
	case STATE_INITIAL:
		/* error */
		assert(false && "Value of 'state' is illegal.");
		break;
	} 

	// Get the infomation of the current function.
	lua_getinfo(L, "nSl", ar);

	// Break and stop program, if any.
	Breakpoint bp = m_breakpoints.Find(ar->source, ar->currentline - 1);
	if (bp.IsOk()) {
		SetState(STATE_BREAK);
	}

	// Update the frame.
	State prevState = STATE_NORMAL;
	for (;;) {
		// handle event and message queue
		if (HandleCommand() != 0 || !m_engine->IsConnecting()) {
			luaL_error(L, "quited");
			return;
		}

		// Break this loop if the state isn't STATE_BREAK.
		if (m_state != STATE_BREAK) {
			break;
		}
		else {
			if (m_isMustUpdate || prevState != STATE_BREAK) {
				if (m_sourceManager.Get(ar->source) == NULL) {
					if (m_sourceManager.Add(ar->source, ar->short_src) != 0) {
						OutputError(std::string("Couldn't open the '") + ar->short_src + "'.");
					}
				}
				m_isMustUpdate = false;

				m_engine->UpdateSource(
					ar->source, ar->currentline,
					++m_updateCount, (prevState == STATE_BREAK),
					UpdateResponseWaiter(&m_waitUpdateCount));
			}
			prevState = m_state;

			if (m_readCommands.empty()) {
				boost::xtime xt;
				boost::xtime_get(&xt, boost::TIME_UTC);
				xt.sec += 1;
				m_commandCond.timed_wait(lock, xt);
			}
		}
	}
}

/**
 * @brief Implementation of lua functions that uses private methods of Context.
 */
class Context::LuaImpl {
public:
	static int atpanic(lua_State *L) {
		printf("on atpanic: %s\n", lua_tostring(L, -1));
		return -1;
	}

	static int cocreate(lua_State *L) {
		lua_State *NL = lua_newthread(L);
		luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
			"Lua function expected");
		lua_pushvalue(L, 1);  /* move function to top */
		lua_xmove(L, NL, 1);  /* move function from L to NL */

		lua_atpanic(NL, atpanic);
		Context::SetHook(NL);
		Context::ms_manager->Add(Context::Find(L), NL);
		return 1;
	}

	static int auxresume(lua_State *L, lua_State *co, int narg) {
		shared_ptr<Context> ctx = Context::Find(L);

		if (!lua_checkstack(co, narg))
			luaL_error(L, "too many arguments to resume");
		lua_xmove(L, co, narg);

		ctx->BeginCoroutine(co);
		int status = lua_resume(co, narg);
		ctx->EndCoroutine(co);

		if (status == 0 || status == LUA_YIELD) {
			int nres = lua_gettop(co);
			if (!lua_checkstack(L, nres))
				luaL_error(L, "too many results to resume");
			lua_xmove(co, L, nres);  /* move yielded values */
			return nres;
		}
		else {
			lua_xmove(co, L, 1);  /* move error message */
			return -1;  /* error flag */
		}
	}

	static int coresume(lua_State *L) {
		lua_State *co = lua_tothread(L, 1);

		luaL_argcheck(L, co, 1, "coroutine expected");
		int r = auxresume(L, co, lua_gettop(L) - 1);
		if (r < 0) {
			lua_pushboolean(L, 0);
			lua_insert(L, -2);
			return 2;  /* return false + error message */
		}
		else {
			lua_pushboolean(L, 1);
			lua_insert(L, -(r + 1));
			return (r + 1);  /* return true + `resume' returns */
		}
	}

	static void override_baselib(lua_State *L) {
		const luaL_reg s_coregs[] = {
			{"create", cocreate},
			{"resume", coresume},
			{NULL, NULL}
		};

		luaL_openlib(L, LUA_COLIBNAME, s_coregs, 0);
	}
};

int Context::LuaInitialize(lua_State *L) {
	lua_atpanic(L, LuaImpl::atpanic);
//	lua_register(L, "lldebug_atpanic", LuaImpl::atpanic);
	luaopen_lldebug(L);
	return 0;
}

int Context::LoadFile(const char *filename) {
	scoped_lock lock(m_mutex);

	if (luaL_loadfile(m_lua, filename) != 0) {
		if (filename != NULL) {
			m_sourceManager.Add(std::string("@") + filename, filename);
		}
		OutputLuaError(lua_tostring(m_lua, -1));
		return -1;
	}

	// Save the first key.
	if (m_rootFileKey.empty()) {
		m_rootFileKey = "@";
		m_rootFileKey += filename;
		LoadConfig();
	}

	m_sourceManager.Add(std::string("@") + filename, filename);
	return 0;
}

int Context::LoadString(const char *str) {
	scoped_lock lock(m_mutex);
	
	if (luaL_loadbuffer(m_lua, str, strlen(str), str) != 0) {
		if (str != NULL) {
			m_sourceManager.Add(str, str);
		}
		OutputLuaError(lua_tostring(m_lua, -1));
		return -1;
	}

	// Save the first key.
	if (m_rootFileKey.empty()) {
		m_rootFileKey = str;
		LoadConfig();
	}

	m_sourceManager.Add(str, str);
	return 0;
}

int Context::LuaOpenBase(lua_State *L) {
	scoped_lock lock(m_mutex);

	if (luaopen_base(L) == 0) {
		return 0;
	}

	LuaImpl::override_baselib(L);
	return 2;
}

void Context::LuaOpenLibs(lua_State *L) {
	scoped_lock lock(m_mutex);
	scoped_lua scoped(this, L, false);

	static const luaL_reg libs[] = {
#ifdef LUA_COLIBNAME
		{LUA_COLIBNAME, luaopen_base},
#endif
#ifdef LUA_TABLIBNAME
		{LUA_TABLIBNAME, luaopen_table},
#endif
#ifdef LUA_IOLIBNAME
		{LUA_IOLIBNAME, luaopen_io},
#endif
#ifdef LUA_OSLIBNAME
		{LUA_OSLIBNAME, luaopen_os},
#endif
#ifdef LUA_STRLIBNAME
		{LUA_STRLIBNAME, luaopen_string},
#endif
#ifdef LUA_MATHLIBNAME
		{LUA_MATHLIBNAME, luaopen_math},
#endif
#ifdef LUA_DBLIBNAME
		{LUA_DBLIBNAME, luaopen_debug},
#endif
#ifdef LUA_LOADLIBNAME
		{LUA_LOADLIBNAME, luaopen_package},
#endif
		{NULL, NULL}
	};

	// Open the libs.
	for (const luaL_reg *reg = libs; reg->func != NULL; ++reg) {
		lua_pushcfunction(L, reg->func);
		lua_pushstring(L, reg->name);
		lua_call(L, 1, 0);
	}

	LuaImpl::override_baselib(L);
}


/*-----------------------------------------------------------------*/
LuaVarList Context::LuaGetFields(TableType type) {
	scoped_lock lock(m_mutex);
	int rootType = 0;

	// Detect table type.
	switch (type) {
	case TABLETYPE_GLOBAL:
		rootType = LUA_GLOBALSINDEX;
		break;
	case TABLETYPE_REGISTRY:
		rootType = LUA_REGISTRYINDEX;
		break;
	default:
		assert(0 && "type [:TableType] is invalid");
		return LuaVarList();
	}

	// Get the fields of the table.
	varlist_maker callback;
	if (iterate_fields(callback, GetLua(), rootType) != 0) {
		return LuaVarList();
	}

	return callback.get_result();
}

LuaVarList Context::LuaGetFields(const LuaVar &var) {
	scoped_lock lock(m_mutex);

	// Get the fields of var.
	varlist_maker callback;
	if (iterate_var(callback, var) != 0) {
		return LuaVarList();
	}

	return callback.get_result();
}

LuaVarList Context::LuaGetLocals(const LuaStackFrame &stackFrame,
								 bool checkLocal, bool checkUpvalue,
								 bool checkEnviron) {
	scoped_lock lock(m_mutex);
	lua_State *L = stackFrame.GetLua().GetState();
	
	varlist_maker callback;
	if (iterate_locals(
		callback,
		(L != NULL ? L : GetLua()),
		stackFrame.GetLevel(),
		checkLocal, checkUpvalue, checkEnviron) != 0) {
		return LuaVarList();
	}

	return callback.get_result();
}

LuaVarList Context::LuaGetStack() {
	scoped_lock lock(m_mutex);

	varlist_maker callback;
	if (iterate_stacks(callback, GetLua()) != 0) {
		return LuaVarList();
	}

	return callback.get_result();
}

LuaBacktraceList Context::LuaGetBacktrace() {
	const int LEVEL_MAX = 1024;	// maximum size of the stack
	scoped_lock lock(m_mutex);
	LuaBacktraceList array;
	
	CoroutineList::reverse_iterator it;
	for (it = m_coroutines.rbegin(); it != m_coroutines.rend(); ++it) {
		lua_State *L1 = it->L;
		scoped_lua scoped(this, L1);
		lua_Debug ar;

		// level 0 may be this own function
		for (int level = 0; lua_getstack(L1, level, &ar); ++level) {
			if (level > LEVEL_MAX) {
				assert(false && "stack size is too many");
				scoped.check(0);
				return array;
			}

			lua_getinfo(L1, "Snl", &ar);

			// Source title is also set,
			// because it is always used when backtrace is shown.
			std::string sourceTitle;
			const Source *source = m_sourceManager.Get(ar.source);
			if (source != NULL) {
				sourceTitle = source->GetTitle();
			}

			std::string name = llutil_makefuncname(&ar);
			array.push_back(LuaBacktrace(
				L1, name, ar.source, sourceTitle,
				ar.currentline, level));
		}

		scoped.check(0);
	}

	return array;
}

static int index_for_eval(lua_State *L) {
	scoped_lua scoped(L);
	std::string target(luaL_checkstring(L, 2));
	int level = (int)lua_tonumber(L, lua_upvalueindex(1));

	// level 0: this C function
	// level 1: __lldebug_dummy__ function
	// level 2: loaded string in LuaEval (includes __lldebug_dummy__)
	// level 3: current running function
	if (find_localvalue(L, 1, target, true, true, false) == 0) {
		return scoped.check(1);
	}

	if (find_localvalue(L, level + 3, target, true, true, true) == 0) {
		return scoped.check(1);
	}

	lua_pushnil(L);
	return scoped.check(1); // return nil
}

static int newindex_for_eval(lua_State *L) {
	scoped_lua scoped(L);
	std::string target(luaL_checkstring(L, 2));
	int level = (int)lua_tonumber(L, lua_upvalueindex(1));
	
	// level 0: this C function
	// level 1: __lldebug_dummy__ function
	// level 2: loaded string in LuaEval (includes __lldebug_dummy__)
	// level 3: current running function
	if (set_localvalue(L, 1, target, 3, true, true, false, false) == 0) {
		return scoped.check(0);
	}

	if (set_localvalue(L, level + 3, target, 3, true, true, true, false) == 0) {
		return scoped.check(0);
	}

	luaL_error(L, "Variable '%s' doesn't exist here.", lua_tostring(L, 2));
	return scoped.check(0);
}

static int setmetatable_for_eval(lua_State *L) {
	scoped_lua scoped(L);

	// level 0: this C function
	// level 1: __lldebug_dummy__ function
	// level 2: loaded string in LuaEval (includes __lldebug_dummy__)
	// level 3: current running function
	int level = (int)luaL_checkinteger(L, lua_upvalueindex(1));

	// table = {}, meta = {}
	lua_newtable(L);
	lua_newtable(L);
	int meta = lua_gettop(L);

	// meta.__index = func1; meta.__newindex = func2
	lua_pushliteral(L, "__index");
	lua_pushnumber(L, (lua_Number)level);
	lua_pushcclosure(L, index_for_eval, 1);
	lua_rawset(L, meta);
	lua_pushliteral(L, "__newindex");
	lua_pushnumber(L, (lua_Number)level);
	lua_pushcclosure(L, newindex_for_eval, 1);
	lua_rawset(L, meta);

	// setfenv(1, setmetatable({}, meta))
	lua_setmetatable(L, -2);
	llutil_setfenv(L, 1, -1);
	lua_pop(L, 1);
	return scoped.check(0);
}

static int getlocals_for_eval(lua_State *L) {
	scoped_lua scoped(L);
	int level = (int)luaL_checkinteger(L, lua_upvalueindex(1));
	int n = lua_gettop(L);
	bool checkLocal =
		(n >= 1 && lua_isboolean(L, 1) ? (lua_toboolean(L, 1) != 0) : true);
	bool checkUpvalue =
		(n >= 2 && lua_isboolean(L, 2) ? (lua_toboolean(L, 2) != 0) : true);
	bool checkEnv =
		(n >= 3 && lua_isboolean(L, 3) ? (lua_toboolean(L, 3) != 0) : false);

	return llutil_getlocals(L, level + 3, checkLocal, checkUpvalue, checkEnv);
}

/**
 * @brief string reader for LuaEval.
 */
struct eval_string_reader {
	std::string str;
	const char *beginning;
	const char *ending;
	int state;

	explicit eval_string_reader(const std::string &str_,
								const char *beginning_, const char *ending_)
		: str(str_), beginning(beginning_), ending(ending_), state(0) {
	}

	static const char *exec(lua_State *L, void *data, size_t *size) {
		eval_string_reader *reader = (eval_string_reader *)data;
		
		switch (reader->state) {
		case 0: // beginning function part
			++reader->state;
			if (reader->beginning != NULL) {
				*size = strlen(reader->beginning);
				return reader->beginning;
			}
			// fall through
		case 1: // content part
			++reader->state;
			if (!reader->str.empty()) {
				*size = reader->str.length();
				return reader->str.c_str();
			}
			// fall through
		case 2: // ending function part
			++reader->state;
			if (reader->ending != NULL) {
				*size = strlen(reader->ending);
				return reader->ending;
			}
			// fall through
		}

		return NULL;
	}
	};

int Context::LuaEval(lua_State *L, int level, const std::string &str, bool withDebug) {
	scoped_lock lock(m_mutex);
	scoped_lua scoped(this, L, withDebug);

	if (str.empty()) {
		return 0;
	}

	const char *beginning = NULL;
	const char *ending = NULL;
	if (level >= 0) {
		beginning =
			"return (function()\n"
			"  local lldebug = lldebug\n"
			"  local getlocals = __lldebug_getlocals__\n"
			"  __lldebug_setmetatable__()\n";
		ending =
			"\nend)()";

		// Export functions used here because of preparation for error state
		// like that all basic functions are unusable.

		// globals[__lldebug_setmetatable__] = function
		lua_pushliteral(L, "__lldebug_setmetatable__");
		lua_pushnumber(L, (lua_Number)level);
		lua_pushcclosure(L, setmetatable_for_eval, 1);
		lua_rawset(L, LUA_GLOBALSINDEX);

		lua_pushliteral(L, "__lldebug_getlocals__");
		lua_pushnumber(L, (lua_Number)level);
		lua_pushcclosure(L, getlocals_for_eval, 1);
		lua_rawset(L, LUA_GLOBALSINDEX);
	}

	// on exit: globals[__lldebug_setmetatable__] = nil
	struct call_on_exit {
		lua_State *L;
		call_on_exit(lua_State *L_) : L(L_) {}
		~call_on_exit() {
			lua_pushliteral(L, "__lldebug_setmetatable__");
			lua_pushnil(L);
			lua_rawset(L, LUA_GLOBALSINDEX);
			lua_pushliteral(L, "__lldebug_getlocals__");
			lua_pushnil(L);
			lua_rawset(L, LUA_GLOBALSINDEX);
		}
	} exit_obj(L);

	// Load string (use lua_load).
	eval_string_reader reader(str, beginning, ending);
	if (lua_load(L, eval_string_reader::exec, &reader, DUMMY_FUNCNAME) != 0) {
		scoped.check(1);
		return -1;
	}

	// Do execute !
	if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
		scoped.check(1);
		return -1;
	}

	return 0;
}

LuaVarList Context::LuaEvalsToVarList(const string_array &evals,
									  const LuaStackFrame &stackFrame,
									  bool withDebug) {
	lua_State *L = stackFrame.GetLua().GetState();
	if (L == NULL) L = GetLua();
	scoped_lock lock(m_mutex);
	scoped_lua scoped(this, L, withDebug);
	LuaVarList result;

	string_array::const_iterator it;
	for (it = evals.begin(); it != evals.end(); ++it) {
		result.push_back(LuaEvalToVar(*it, stackFrame, withDebug));
	}

	scoped.check(0);
	return result;
}

LuaVarList Context::LuaEvalToMultiVar(const std::string &eval,
									  const LuaStackFrame &stackFrame,
									  bool withDebug) {
	lua_State *L = stackFrame.GetLua().GetState();
	if (L == NULL) L = GetLua();
	scoped_lock lock(m_mutex);
	scoped_lua scoped(this, L, withDebug);
	LuaVarList result;
	int beginningtop = lua_gettop(L);

	if (LuaEval(L, stackFrame.GetLevel(), eval, withDebug) != 0) {
		std::string error = llutil_tostring(L, -1);
		lua_pop(L, 1);
		result.push_back(LuaVar(LuaHandle(L), "<error>", ParseLuaError(error).message));
		scoped.check(0);
		return result;
	}

	// Convert the result to LuaVar objects.
	int top = lua_gettop(L);
	for (int idx = beginningtop + 1; idx <= top; ++idx) {
		result.push_back(LuaVar(LuaHandle(L), eval, idx));
	}

	lua_settop(L, beginningtop); // adjust the stack top
	scoped.check(0);
	return result;
}

LuaVar Context::LuaEvalToVar(const std::string &eval,
							 const LuaStackFrame &stackFrame,
							 bool withDebug) {
	lua_State *L = stackFrame.GetLua().GetState();
	if (L == NULL) L = GetLua();
	scoped_lock lock(m_mutex);
	scoped_lua scoped(this, L, withDebug);
	int beginningtop = lua_gettop(L);

	if (LuaEval(L, stackFrame.GetLevel(), eval, withDebug) != 0) {
		std::string error = lua_tostring(L, -1);
		lua_pop(L, 1);
		scoped.check(0);
		return LuaVar(LuaHandle(L), "<error>", ParseLuaError(error).message);
	}

	// Convert the result to a LuaVar object.
	int top = lua_gettop(L);
	if (top == beginningtop) {
		scoped.check(0);
		return LuaVar();
	}
	else {
		LuaVar result(LuaHandle(L), eval, beginningtop + 1);
		lua_settop(L, beginningtop); // adjust the stack top
		scoped.check(0);
		return result;
	}
}

} // end of namespace context
} // end of namespace lldebug

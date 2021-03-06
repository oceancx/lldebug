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

#ifndef __LLDEBUG_MEDIATOR_H__
#define __LLDEBUG_MEDIATOR_H__

#include "sysinfo.h"
#include "luainfo.h"
#include "queue_mt.h"
#include "net/remoteengine.h"
#include "visual/event.h"

namespace lldebug {
namespace visual {

class Application;
class MainFrame;

/**
 * @brief すべてのクラスに共通の情報を保持します。
 */
class Mediator {
public:
	virtual ~Mediator();

	/// Get the class instance.
	/// It is valid after the calling of 'Initialize' function.
	static Mediator *Get() {
		return ms_instance;
	}

	/// Increment the update count.
	void IncUpdateCount();

	/// Focus the error line.
	void FocusErrorLine(const std::string &key, int line);

	/// Focus the backtrace.
	void FocusBacktraceLine(const LuaBacktrace &bt);

	/// Output the log.
	void OutputLog(LogType type, const wxString &msg);

	/// Process the remote command.
	void ProcessRemoteCommand(const Command &command);

	/// Process the all remote commanda.
	void ProcessAllRemoteCommands();

	/// Get the RemoteEngine object.
	shared_ptr<RemoteEngine> GetEngine() {
		return m_engine;
	}

	/// Get the main frame.
	MainFrame *GetFrame() {
		return m_frame;
	}

	/// Get the BreakpointList object.
	BreakpointList &GetBreakpoints() {
		return m_breakpoints;
	}

	/// Get the SourceManager object.
	SourceManager &GetSourceManager() {
		return m_sourceManager;
	}

	/// Get the source contents.
	const Source *GetSource(const std::string &key) {
		return m_sourceManager.Get(key);
	}

	/// Find the breakpoint.
	Breakpoint FindBreakpoint(const std::string &key, int line) {
		return m_breakpoints.Find(key, line);
	}

	/// Find the next breakpoint.
	Breakpoint NextBreakpoint(const Breakpoint &bp) {
		return m_breakpoints.Next(bp);
	}

	/// Set the breakpoint.
	void SetBreakpoint(const Breakpoint &bp) {
		m_breakpoints.Set(bp);
	}

	/// Toggle on/off of the breakpoint.
	void ToggleBreakpoint(const std::string &key, int line) {
		m_breakpoints.Toggle(key, line);
	}

	/// Get the stack frame for the local vars.
	const LuaStackFrame &GetStackFrame() {
		return m_stackFrame;
	}

	/// Set the stack frame for the local vars.
	void SetStackFrame(const LuaStackFrame &stackFrame) {
		m_stackFrame = stackFrame;
	}

	/// Get the count of 'UpdateSource'.
	int GetUpdateCount() {
		return m_updateCount;
	}

private:
	void OutputLogInternal(const LogData &logData, bool sendRemote);
	void OnRemoteCommand(const Command &command);

private:
	friend class Application;
	explicit Mediator();

	/// This function can be called from only 'Application' class.
	int Initialize(unsigned short port);

private:
	friend class MainFrame;
	void SetMainFrame(MainFrame *frame);

private:
	static Mediator *ms_instance;

	shared_ptr<RemoteEngine> m_engine;
	MainFrame *m_frame;
	BreakpointList m_breakpoints;
	SourceManager m_sourceManager;
	queue_mt<Command> m_readCommands;
	unsigned short m_port;

	LuaStackFrame m_stackFrame;
	int m_updateCount;
};

} // end of namespace visual
} // end of namespace lldebug

#endif

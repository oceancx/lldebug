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

#ifndef __LLDEBUG_INTERACTIVEVIEW_H__
#define __LLDEBUG_INTERACTIVEVIEW_H__

#include "visual/event.h"

namespace lldebug {
namespace visual {

/**
 * @brief ソースコードを表示するコントロールです。
 */
class InteractiveView : public wxPanel {
public:
	explicit InteractiveView(wxWindow *parent);
	virtual ~InteractiveView();

	void OutputLog(const wxString &str);

private:
	void CreateGUIControls();
	void OnChangedState(wxDebugEvent &event);
	void OnOutputInteractiveView(wxDebugEvent &event);
	void Run();

	DECLARE_EVENT_TABLE();

private:
	wxTextCtrl *m_text;

	class TextInput;
	friend class TextInput;
	TextInput *m_input;

	class RunButton;
	friend class RunButton;
	RunButton *m_run;
};

} // end of namespace visual
} // end of namespace lldebug

#endif

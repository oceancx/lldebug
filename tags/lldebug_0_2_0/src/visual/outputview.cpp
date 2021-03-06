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
#include "visual/mediator.h"
#include "visual/outputview.h"
#include "visual/strutils.h"
#include "wx/wxscintilla.h"

namespace lldebug {
namespace visual {

class OutputView::InnerTextCtrl : public wxScintilla {
private:
	enum {
		MARGIN_INFO = 1,
		MARKNUM_ERROR = 1,
	};

	struct ViewData {
		explicit ViewData(const std::string &key_=std::string(""), int line_=-1)
			: key(key_), line(line_) {
		}
		std::string key;
		int line;
	};
	typedef std::map<int, ViewData> DataMap;
	DataMap m_dataMap;

	DECLARE_EVENT_TABLE();

public:
	explicit InnerTextCtrl(wxWindow *parent)
		: wxScintilla(parent, wxID_ANY) {
		CreateGUIControls();
	}

	virtual ~InnerTextCtrl() {
	}

	void CreateGUIControls() {
		SetReadOnly(true);
		SetViewEOL(false);
		SetWrapMode(wxSCI_WRAP_NONE);
		SetEdgeMode(wxSCI_EDGE_NONE);
		SetViewWhiteSpace(wxSCI_WS_INVISIBLE);
		SetLayoutCache(wxSCI_CACHE_PAGE);
		SetLexer(wxSCI_LEX_NULL);

		/// Setup the selectable error marker.
		MarkerDefine(MARKNUM_ERROR, wxSCI_MARK_ARROWS);
		MarkerSetForeground(MARKNUM_ERROR, wxColour(_T("RED")));

		// Setup the infomation margin.
		StyleSetForeground(wxSCI_STYLE_DEFAULT,
			wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
		SetMarginType(MARGIN_INFO, wxSCI_MARGIN_FORE);
		SetMarginWidth(MARGIN_INFO, 16);
		SetMarginSensitive(MARGIN_INFO, false);
		SetMarginMask(MARGIN_INFO, (1 << MARKNUM_ERROR));
	}

	/// Add raw text that is std::string.
	void AddTextRawStd(const std::string &str) {
		if (str.empty()) {
			return;
		}

		AddTextRaw(str.c_str());
	}

	/// Add raw text that is std::string.
	void AddTextStd(const std::string &str) {
		if (str.empty()) {
			return;
		}

		AddText(wxConvFromCtxEnc(str));
	}

	/// Output log.
	void OutputLog(const LogData &logData) {
		SetReadOnly(false);

		if (!logData.IsRemote()) {
			AddTextRaw("Frame: ");
		}

		const Source *source = Mediator::Get()->GetSource(logData.GetKey());
		if (source != NULL) {
			ViewData viewData(logData.GetKey(), logData.GetLine());
			m_dataMap[GetLineCount() - 1] = viewData;

			if (logData.GetType() == LOGTYPE_ERROR) {
				MarkerAdd(GetLineCount() - 1, MARKNUM_ERROR);
			}

			const std::string &path = source->GetPath();
			AddTextStd(!path.empty() ? path : source->GetTitle());
			AddTextRaw("(");
			AddTextRawStd(boost::lexical_cast<std::string>(logData.GetLine()));
			AddTextRaw("): ");
		}

		AddTextStd(logData.GetLog());
		AddTextRaw("\n");
		SetReadOnly(true);
	}

private:
	void OnEndDebug(wxDebugEvent &event) {
		event.Skip();

		SetReadOnly(false);
		m_dataMap.clear();
		MarkerDeleteAll(MARKNUM_ERROR);
		SetTextRaw("");
		SetReadOnly(true);
	}

	void OnDClick(wxScintillaEvent &event) {
		event.Skip();

		// Out of selectable range.
		if (event.GetPosition() < 0) {
			return;
		}

		// Goto the specify source line.
		int line = LineFromPosition(event.GetPosition());
		DataMap::iterator it = m_dataMap.find(line);
		if (it != m_dataMap.end()) {
			const ViewData &data = it->second;
			Mediator::Get()->FocusErrorLine(data.key, data.line);

			SetSelection(
				PositionFromLine(line),
				GetLineEndPosition(line));
		}
	}
};

BEGIN_EVENT_TABLE(OutputView::InnerTextCtrl, wxScintilla)
	EVT_DEBUG_END_DEBUG(wxID_ANY, OutputView::InnerTextCtrl::OnEndDebug)
	EVT_SCI_DOUBLECLICK(wxID_ANY, OutputView::InnerTextCtrl::OnDClick)
END_EVENT_TABLE()


/*-----------------------------------------------------------------*/
BEGIN_EVENT_TABLE(OutputView, wxListBox)
	EVT_SIZE(OutputView::OnSize)
	EVT_DEBUG_OUTPUT_LOG(wxID_ANY, OutputView::OnOutputLog)
END_EVENT_TABLE()

OutputView::OutputView(wxWindow *parent)
	: wxPanel(parent, ID_OUTPUTVIEW) {
	m_text = new InnerTextCtrl(this);

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_text, 1, wxEXPAND);
	SetSizer(sizer);
	sizer->SetSizeHints(this);
}

OutputView::~OutputView() {
}

void OutputView::OnSize(wxSizeEvent &event) {
	m_text->SetSize(GetClientSize());
}

void OutputView::OutputLog(LogType logType, const wxString &str, const std::string &key, int line) {
	m_text->OutputLog(LogData(logType, wxConvToCtxEnc(str), key, line));
}

void OutputView::OnOutputLog(wxDebugEvent &event) {
	m_text->OutputLog(event.GetLogData());
}

} // end of namespace visual
} // end of namespace lldebug

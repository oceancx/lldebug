//---------------------------------------------------------------------------
//
// Name:        MainFrame.cpp
// Author:      雅博
// Created:     2007/11/23 0:05:33
// Description: MainFrame class implementation
//
//---------------------------------------------------------------------------

#include "lldebug_prec.h"
#include "lldebug_sourceview.h"
#include "lldebug_context.h"
#include "lldebug_langsettings.h"
#include "wx/wxscintilla.h"

namespace lldebug {

#if 0
     wxT("FOREST GREEN"), wxT("WHITE"),
     wxT("KHAKI"), wxT("WHITE"),
     wxT("BROWN"), wxT("WHITE"),
     wxT("ORANGE"), wxT("WHITE"),
     wxT("VIOLET"), wxT("WHITE"),
     wxT("BLUE"), wxT("WHITE"),
     wxT("SIENNA"), wxT("WHITE"),
     wxT("ORCHID"), wxT("WHITE"),
     wxT("GREY"), wxT("WHITE"),
     wxT("DARK GREY"), wxT("WHITE"),
#endif

/**
 * @brief ソースコードを表示するコントロールです。
 */
class SourceViewPage : public wxScintilla {
	enum {
		MARGIN_LINENUM = 0,
		MARGIN_DEBUG = 1,
		MARGIN_FOLDING = 2,
		MARGIN_DIVIDER = 3,

		MARKNUM_BREAKPOINT = 1,
		MARKNUM_RUNNING = 2,
		MARKNUM_BACKTRACE = 3,
	};

public:
	explicit SourceViewPage(SourceView *parent, Context *ctx,
							const Source *source)
		: wxScintilla(parent, wxID_ANY)
		, m_parent(parent), m_ctx(ctx)
		, m_initialized(false), m_wasTitleChanged(false)
		, m_currentLine(-1), m_markedLine(-1) {
		CreateGUIControls();
		Initialize(source);
	}

	virtual ~SourceViewPage() {
	}

private:
	void CreateGUIControls() {
		scoped_lock lock(m_mutex);

		int lineNumMargin = TextWidth(wxSCI_STYLE_LINENUMBER, wxT("_9999"));

		// default font for all styles
		SetViewEOL(false);
		SetMarginType(MARGIN_LINENUM, wxSCI_MARGIN_NUMBER);
		SetMarginWidth(MARGIN_LINENUM, true ? lineNumMargin : 0);
		SetEdgeMode(false ? wxSCI_EDGE_LINE : wxSCI_EDGE_NONE);
		SetViewWhiteSpace(false ? wxSCI_WS_VISIBLEALWAYS : wxSCI_WS_INVISIBLE);
		SetOvertype(false);
		SetReadOnly(false);
		SetWrapMode(true ? wxSCI_WRAP_WORD : wxSCI_WRAP_NONE);

		StyleSetForeground(wxSCI_STYLE_DEFAULT,
			wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
		StyleSetBackground(wxSCI_STYLE_DEFAULT, wxColour(wxT("WHITE")));
		StyleSetForeground(wxSCI_STYLE_LINENUMBER, wxColour(wxT("BLACK")));
		StyleSetBackground(wxSCI_STYLE_LINENUMBER, wxColour(wxT("WHITE")));

		wxColour foldColour = wxColour(wxT("DARK GREY"));
		MarkerDefine(wxSCI_MARKNUM_FOLDER, wxSCI_MARK_BOXPLUS);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDER, foldColour);
		MarkerSetForeground(wxSCI_MARKNUM_FOLDER, wxColour(wxT("WHITE")));
		MarkerDefine(wxSCI_MARKNUM_FOLDEROPEN, wxSCI_MARK_BOXMINUS);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDEROPEN, foldColour);
		MarkerSetForeground(wxSCI_MARKNUM_FOLDEROPEN, wxColour(wxT("WHITE")));

		wxColour lineColour = wxColour(wxT("GREEN YELLOW"));
		MarkerDefine(wxSCI_MARKNUM_FOLDERSUB, wxSCI_MARK_VLINE);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDERSUB, lineColour);
		MarkerDefine(wxSCI_MARKNUM_FOLDERMIDTAIL, wxSCI_MARK_TCORNERCURVE);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDERMIDTAIL, lineColour);
		MarkerDefine(wxSCI_MARKNUM_FOLDERTAIL, wxSCI_MARK_LCORNERCURVE);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDERTAIL, lineColour);

		MarkerDefine(wxSCI_MARKNUM_FOLDEROPENMID, wxSCI_MARK_ARROWDOWN);
		MarkerDefine(wxSCI_MARKNUM_FOLDEREND, wxSCI_MARK_ARROW);
		MarkerSetForeground(wxSCI_MARKNUM_FOLDEROPENMID, foldColour);
		MarkerSetForeground(wxSCI_MARKNUM_FOLDEREND, foldColour);
		MarkerSetBackground(wxSCI_MARKNUM_FOLDEROPENMID, wxColour(wxT("WHITE")));
		MarkerSetBackground(wxSCI_MARKNUM_FOLDEREND, wxColour(wxT("WHITE")));

		MarkerDefine(MARKNUM_BREAKPOINT, wxSCI_MARK_CIRCLE);
		MarkerSetForeground(MARKNUM_BREAKPOINT, wxColour(_T("ORANGE")));
		MarkerSetBackground(MARKNUM_BREAKPOINT, wxColour(_T("RED")));

		MarkerDefine(MARKNUM_RUNNING, wxSCI_MARK_SHORTARROW);
		MarkerSetForeground(MARKNUM_RUNNING, wxColour(_T("RED")));
		MarkerSetBackground(MARKNUM_RUNNING, wxColour(_T("YELLOW")));

		MarkerDefine(MARKNUM_BACKTRACE, wxSCI_MARK_BACKGROUND);
		MarkerSetForeground(MARKNUM_BACKTRACE, wxColour(_T("YELLOW")));
		MarkerSetBackground(MARKNUM_BACKTRACE, wxColour(_T("GREEN")));

		/*int INDIC_TEST = 1;
		IndicatorSetStyle(INDIC_TEST, wxSCI_INDIC_DIAGONAL);
		IndicatorSetForeground(INDIC_TEST, *wxBLACK);
		SetIndicatorCurrent(INDIC_TEST);*/

		// set lexer and language
		SetLexer(wxSCI_LEX_LUA);

		// initialize settings
		wxFont font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
			false, wxT("MS Gothic"));
        int keywordnr = 0;
		for (size_t i = 0; s_stylePrefs[i].style != STYLE_END; ++i) {
			const StyleInfo &curType = s_stylePrefs[i];
			int style = curType.style;

            if (curType.foreground != NULL) {
                StyleSetForeground(style, wxColour(curType.foreground));
            }
            if (curType.background != NULL) {
                StyleSetBackground(style, wxColour(curType.background));
            }
			StyleSetFont(style, font);
            StyleSetBold(style, (curType.fontStyle & FONTSTYLE_BOLD) > 0);
            StyleSetItalic(style, (curType.fontStyle & FONTSTYLE_ITALIC) > 0);
            StyleSetUnderline(style, (curType.fontStyle & FONTSTYLE_UNDERL) > 0);
            StyleSetVisible(style, (curType.fontStyle & FONTSTYLE_HIDDEN) == 0);
            StyleSetCase(style, curType.letterCase);
			StyleSetHotSpot(style, curType.hotspot);
            if (curType.words != NULL) {
                SetKeyWords(keywordnr, curType.words);
                ++keywordnr;
            }
        }

		// debug info
		SetMarginType(MARGIN_DEBUG, wxSCI_MARGIN_FORE);
		SetMarginWidth(MARGIN_DEBUG, 16);
		SetMarginSensitive(MARGIN_DEBUG, true);
		SetMarginMask(MARGIN_DEBUG,
			(1 << MARKNUM_BREAKPOINT) | (1 << MARKNUM_RUNNING) | (1 << MARKNUM_BACKTRACE));

		// set margin as unused
		SetMarginType(MARGIN_DIVIDER, wxSCI_MARGIN_BACK);
		SetMarginWidth(MARGIN_DIVIDER, 4);
		SetMarginSensitive(MARGIN_DIVIDER, false);
		SetMarginMask(MARGIN_DIVIDER, 0);

		// folding
		SetMarginType(MARGIN_FOLDING, wxSCI_MARGIN_SYMBOL);
		SetMarginMask(MARGIN_FOLDING, wxSCI_MASK_FOLDERS);
		SetFoldMarginColour(true, wxColour(_T("WHITE")));
		SetFoldMarginHiColour(true, wxColour(_T("WHITE")));

		if (true) {
			SetMarginWidth(MARGIN_FOLDING, 12);
			SetMarginSensitive(MARGIN_FOLDING, true);
			SetProperty(wxT("fold"), wxT("1"));
			SetProperty(wxT("fold.comment"), wxT("1"));
			SetProperty(wxT("fold.compact"), wxT("1"));
		}
		else {
			SetMarginWidth(MARGIN_FOLDING, 0);
			SetMarginSensitive(MARGIN_FOLDING, false);
		}

		SetFoldFlags(wxSCI_FOLDFLAG_LINEBEFORE_CONTRACTED |
			wxSCI_FOLDFLAG_LINEAFTER_CONTRACTED);

		SetVisiblePolicy(wxSCI_VISIBLE_STRICT | wxSCI_VISIBLE_SLOP, 1);
		SetXCaretPolicy(wxSCI_CARET_EVEN | wxSCI_VISIBLE_STRICT | wxSCI_CARET_SLOP, 1);
		SetYCaretPolicy(wxSCI_CARET_EVEN | wxSCI_VISIBLE_STRICT | wxSCI_CARET_SLOP, 1);

		SetHotspotActiveUnderline(true);

		// set spaces and indention
		SetTabWidth(4);
		SetUseTabs(true);
		SetTabIndents(true);
		SetBackSpaceUnIndents(true);
		SetIndent(true ? 2 : 0);
		SetIndentationGuides(false);
		SetLayoutCache(wxSCI_CACHE_PAGE);
	}

	void Initialize(const Source *source) {
		scoped_lock lock(m_mutex);
		wxASSERT(source != NULL);

		std::string str;
		for (string_array::size_type i = 0; i < source->GetNumberOfLines(); ++i) {
			// The encoding is UTF8.
			str += source->GetSourceLine(i);

			// 原則的に、最後の行には改行を入れません。
			if (i < source->GetNumberOfLines()) {
				str += "\n";
			}
		}

		// AddTextRaw accepts the UTF8 string only.
		AddTextRaw(str.c_str());

		for (size_t i = 0; i < m_ctx->GetBreakPointSize(); ++i) {
			const BreakPoint &bp = m_ctx->GetBreakPoint(i);
			MarkerAdd(bp.GetLine(), MARKNUM_BREAKPOINT);
		}

		// タイトルはユニコードにエンコーディングを変換します。
		m_key = source->GetKey();
		m_title = wxConvFromUTF8(source->GetTitle());
		m_currentLine = -1;
		m_initialized = true;
	}

	void OnMarginClick(wxScintillaEvent &event) {
		scoped_lock lock(m_mutex);

		if (event.GetMargin() == MARGIN_FOLDING) {
			int lineClick = LineFromPosition(event.GetPosition());
			int levelClick = GetFoldLevel(lineClick);
			if ((levelClick & wxSCI_FOLDLEVELHEADERFLAG) > 0) {
				ToggleFold(lineClick);
			}
		}
		else if (event.GetMargin() == MARGIN_DEBUG) {
			int lineClick = LineFromPosition(event.GetPosition());
			ToggleBreakPointFromLine(lineClick);
		}
	}

	void OnKeyDown(wxKeyEvent &event) {
		scoped_lock lock(m_mutex);
		event.Skip();

		if (event.ControlDown() && event.GetKeyCode() == 'S') {
			SaveSource();
		}
	}

	void ChangeModified(bool modified) {
		if (!m_initialized || modified == m_wasTitleChanged) {
			return;
		}

		size_t sel = m_parent->GetPageIndex(this);
		if (sel != wxNOT_FOUND) {
			if (modified) {
				/* Add '*' to the source title. */
				m_parent->SetPageText(sel, m_title + wxT("*"));
			}
			else {
				/* Set the source title. */
				m_parent->SetPageText(sel, m_title);
			}

			m_wasTitleChanged = modified;
		}
	}

	void OnModified(wxScintillaEvent &event) {
		scoped_lock lock(m_mutex);
		event.Skip();

		if (event.GetModificationType()
			& (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT)) {
			ChangeModified(true);
		}
	}

	void OnCharAdded(wxScintillaEvent &event) {
		scoped_lock lock(m_mutex);

		// Change this if support for mac files with \r is needed
		if (event.GetKey() == '\n' || event.GetKey() == '\r') {
			int currentLine = GetCurrentLine();
			if (currentLine <= 0) {
				return;
			}

			// width of one indent character
			int indentWidth = (GetUseTabs() ? GetTabWidth() : 1);
			if (indentWidth == 0) {
				return;
			}

			// indent as prev line level
			int indentSize = GetLineIndentation(currentLine - 1);
			SetLineIndentation(currentLine, indentSize);

			// position = (line start pos) + (tabs count) + (space count)
			GotoPos(PositionFromLine(currentLine)
				+ (indentSize / indentWidth)
				+ (indentSize % indentWidth));

			// notify that the test was changed
			ChangeModified(true);
		}
	}

	void OnHotSpotClick(wxScintillaEvent &event) {
		scoped_lock lock(m_mutex);
		//int pos = event.GetPosition();
		//CallTipShow(pos, event.GetText());
	}

public:
	const std::string &GetKey() const {
		return m_key;
	}

	const wxString &GetTitle() const {
		return m_title;
	}

	int SetCurrentLine(int line, bool isCurrentRunning) {
		scoped_lock lock(m_mutex);
		wxASSERT((line < 0) || (0 <= line && line < GetLineCount()));

		if (isCurrentRunning && m_currentLine >= 0) {
			MarkerDelete(m_currentLine, MARKNUM_RUNNING);
			m_currentLine = -1;
		}
		
		// Hide mark always.
		if (m_markedLine >= 0) {
			MarkerDeleteAll(MARKNUM_BACKTRACE);
			m_markedLine = -1;
		}

		// 現在の行を設定します。
		if (line >= 0) {
			EnsureVisible(line);
			int pos = PositionFromLine(line);
			SetSelection(pos, pos);

			if (isCurrentRunning) {
				MarkerAdd(line, MARKNUM_RUNNING);
			}
			else {
				MarkerAdd(line, MARKNUM_BACKTRACE);
			}
		}

		if (isCurrentRunning) {
			m_currentLine = line;
		}
		else {
			m_markedLine = line;
		}

		SetFocus();
		return 0;
	}

	void ToggleBreakPointFromLine(int line) {
		scoped_lock lock(m_mutex);
		line = median(line, 0, GetLineCount());
		m_ctx->ToggleBreakPoint(m_key, line);

		if (m_ctx->FindBreakPoint(m_key, line) != NULL) {
			MarkerAdd(line, MARKNUM_BREAKPOINT);
		}
		else {
			MarkerDelete(line, MARKNUM_BREAKPOINT);
		}
	}

	void ToggleBreakPoint() {
		scoped_lock lock(m_mutex);
		int from, to;
		GetSelection(&from, &to);
		ToggleBreakPointFromLine(LineFromPosition(to));
	}

	void ChangeEnable(bool enable) {
		scoped_lock lock(m_mutex);

		if (!enable) {
			SetCurrentLine(-1, true);
		}
	}

	void SaveSource() {
		scoped_lock lock(m_mutex);
		string_array array;

		for (int i = 0; i < GetLineCount(); ++i) {
			wxCharBuffer cbuffer = GetLineRaw(i);
			std::string buffer = (cbuffer != NULL ? cbuffer : "");

			// trim newlines
			while (!buffer.empty()) {
				char c = *(--buffer.end());
				if (c == '\n' || c == '\r') {
					buffer.erase(buffer.length() - 1);
				}
				else {
					break;
				}
			}

			array.push_back(buffer);
		}

		// Erase the last line if it has newline only.
		if (!array.empty() && array.back().empty()) {
			array.pop_back();
		}

		m_ctx->SaveSource(m_key, array);
		ChangeModified(false);
	}

private:
	SourceView *m_parent;
	Context *m_ctx;
	mutex m_mutex;
	bool m_initialized;
	bool m_wasTitleChanged;

	std::string m_key;
	wxString m_title;
	int m_currentLine;
	int m_markedLine;

	DECLARE_EVENT_TABLE();
};

BEGIN_EVENT_TABLE(SourceViewPage, wxScintilla)
	EVT_KEY_DOWN(SourceViewPage::OnKeyDown)
	EVT_SCI_MODIFIED(wxID_ANY, SourceViewPage::OnModified)
	EVT_SCI_MARGINCLICK(wxID_ANY, SourceViewPage::OnMarginClick)
	EVT_SCI_CHARADDED(wxID_ANY, SourceViewPage::OnCharAdded)
	EVT_SCI_HOTSPOT_CLICK(wxID_ANY, SourceViewPage::OnHotSpotClick)
END_EVENT_TABLE()


/*-----------------------------------------------------------------*/
BEGIN_EVENT_TABLE(SourceView, wxAuiNotebook)
	EVT_LLDEBUG_CHANGED_STATE(ID_SOURCEVIEW, SourceView::OnChangedState)
	EVT_LLDEBUG_UPDATE_SOURCE(ID_SOURCEVIEW, SourceView::OnUpdateSource)
END_EVENT_TABLE()

SourceView::SourceView(Context *ctx, wxWindow *parent)
	: wxAuiNotebook(parent, ID_SOURCEVIEW + ctx->GetId()
		, wxDefaultPosition, wxDefaultSize
		, wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS)
	, m_ctx(ctx) {
	CreateGUIControls();
}

SourceView::~SourceView() {
}

void SourceView::CreateGUIControls() {
	scoped_lock lock(m_mutex);
}

size_t SourceView::FindPageFromKey(const std::string &key) {
	scoped_lock lock(m_mutex);

	for (size_t i = 0; i < GetPageCount(); ++i) {
		SourceViewPage *page = GetPage(i);

		if (page->GetKey() == key) {
			return i;
		}
	}

	return wxNOT_FOUND;
}

SourceViewPage *SourceView::GetPage(size_t i) {
	wxWindow *page = wxAuiNotebook::GetPage(i);
	return dynamic_cast<SourceViewPage *>(page);
}

SourceViewPage *SourceView::GetSelected() {
	size_t sel = GetSelection();

	if (sel == wxNOT_FOUND) {
		return NULL;
	}

	return GetPage(sel);
}

void SourceView::OnChangedState(wxChangedStateEvent &event) {
	scoped_lock lock(m_mutex);

	// 実行中は使えないようにします。
	SourceViewPage *page = GetSelected();
	if (page != NULL) {
		page->ChangeEnable(event.GetValue());
	}
}

void SourceView::ToggleBreakPoint() {
	scoped_lock lock(m_mutex);
	SourceViewPage *page = GetSelected();

	if (page != NULL) {
		page->ToggleBreakPoint();
	}
}

void SourceView::OnUpdateSource(wxSourceLineEvent &event) {
	scoped_lock lock(m_mutex);
	SourceViewPage *page = NULL;
	
	size_t i = FindPageFromKey(event.GetKey());
	if (i == wxNOT_FOUND) {
		if (event.IsCurrentRunning()) {
			const Source *source = m_ctx->GetSource(event.GetKey());
			wxASSERT(source != NULL);

			// 新しいページを作成します。
			page = new SourceViewPage(this, m_ctx, source);
			AddPage(page, page->GetTitle(), true);
		}
	}
	else {
		page = GetPage(i);
		SetSelection(i);
	}

	// If exist current line, page must not be NULL.
	//wxASSERT((page != NULL) == (event.GetLine() >= 0));
	if (page != NULL) {
		page->SetCurrentLine(event.GetLine() - 1, event.IsCurrentRunning());
	}
}

}

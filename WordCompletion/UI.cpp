//////////////////////////////////////////////////////////////////////
// This file was auto-generated by codelite's wxCrafter Plugin
// wxCrafter project file: UI.wxcp
// Do not modify this file by hand!
//////////////////////////////////////////////////////////////////////

#include "UI.h"
#include "WordCompletionSettings.h"


// Declare the bitmap loading function
extern void wxC69AFInitBitmapResources();

static bool bBitmapLoaded = false;


WordCompletionSettingsBaseDlg::WordCompletionSettingsBaseDlg(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    : wxDialog(parent, id, title, pos, size, style)
{
    if ( !bBitmapLoaded ) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxC69AFInitBitmapResources();
        bBitmapLoaded = true;
    }
    
    wxBoxSizer* boxSizer2 = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizer2);
    
    wxArrayString m_pgMgrArr;
    wxUnusedVar(m_pgMgrArr);
    wxArrayInt m_pgMgrIntArr;
    wxUnusedVar(m_pgMgrIntArr);
    m_pgMgr = new wxPropertyGridManager(this, wxID_ANY, wxDefaultPosition, wxSize(-1,-1), wxPG_DESCRIPTION|wxPG_SPLITTER_AUTO_CENTER|wxPG_BOLD_MODIFIED);
    
    boxSizer2->Add(m_pgMgr, 1, wxALL|wxEXPAND, 5);
    
    m_pgMgrArr.Clear();
    m_pgMgrIntArr.Clear();
    m_pgMgrArr.Add(_("Starts With"));
    m_pgMgrArr.Add(_("Contains"));
    m_pgPropComparisonMethod = m_pgMgr->Append(  new wxEnumProperty( _("Comparison Method"), wxPG_LABEL, m_pgMgrArr, m_pgMgrIntArr, 0) );
    m_pgPropComparisonMethod->SetHelpString(_("Select the word completion comparison method:\n\"Starts With\" - suggest all words that starts with the partial word that the user typed\n\"Contains\" - suggest all words that contains the partial word that the user typed"));
    
    m_stdBtnSizer4 = new wxStdDialogButtonSizer();
    
    boxSizer2->Add(m_stdBtnSizer4, 0, wxALL|wxALIGN_CENTER_HORIZONTAL, 5);
    
    m_button6 = new wxButton(this, wxID_OK, wxT(""), wxDefaultPosition, wxSize(-1, -1), 0);
    m_button6->SetDefault();
    m_stdBtnSizer4->AddButton(m_button6);
    
    m_button8 = new wxButton(this, wxID_CANCEL, wxT(""), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stdBtnSizer4->AddButton(m_button8);
    m_stdBtnSizer4->Realize();
    
    SetName(wxT("WordCompletionSettingsBaseDlg"));
    SetSizeHints(-1,-1);
    if ( GetSizer() ) {
         GetSizer()->Fit(this);
    }
    CentreOnParent(wxBOTH);
#if wxVERSION_NUMBER >= 2900
    if(!wxPersistenceManager::Get().Find(this)) {
        wxPersistenceManager::Get().RegisterAndRestore(this);
    } else {
        wxPersistenceManager::Get().Restore(this);
    }
#endif
    // Connect events
    m_pgMgr->Connect(wxEVT_PG_CHANGED, wxPropertyGridEventHandler(WordCompletionSettingsBaseDlg::OnValueChanged), NULL, this);
    
}

WordCompletionSettingsBaseDlg::~WordCompletionSettingsBaseDlg()
{
    m_pgMgr->Disconnect(wxEVT_PG_CHANGED, wxPropertyGridEventHandler(WordCompletionSettingsBaseDlg::OnValueChanged), NULL, this);
    
}

WordCompletionImages::WordCompletionImages()
    : wxImageList(16, 16, true)
{
    if ( !bBitmapLoaded ) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxC69AFInitBitmapResources();
        bBitmapLoaded = true;
    }
    
    {
        wxBitmap bmp;
        wxIcon icn;
        bmp = wxXmlResource::Get()->LoadBitmap(wxT("m_bmpWord"));
        icn.CopyFromBitmap( bmp );
        this->Add( icn );
        m_bitmaps.insert( std::make_pair(wxT("m_bmpWord"), bmp ) );
    }
    
}

WordCompletionImages::~WordCompletionImages()
{
}

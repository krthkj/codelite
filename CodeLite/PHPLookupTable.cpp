#include "PHPLookupTable.h"
#include <wx/filename.h>
#include "file_logger.h"
#include "PHPEntityNamespace.h"
#include "PHPEntityClass.h"
#include "PHPEntityVariable.h"
#include "PHPEntityFunction.h"
#include "event_notifier.h"

wxDEFINE_EVENT(wxPHP_PARSE_STARTED, clParseEvent);
wxDEFINE_EVENT(wxPHP_PARSE_ENDED, clParseEvent);
wxDEFINE_EVENT(wxPHP_PARSE_PROGRESS, clParseEvent);

static wxString PHP_SCHEMA_VERSION = "7.0.0";

//------------------------------------------------
// Metadata table
//------------------------------------------------
const static wxString CREATE_METADATA_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS METADATA_TABLE(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "SCHEMA_NAME TEXT, " // The scope type: 0 for namespace, 1 for class
    "SCHEMA_VERSION TEXT)";
const static wxString CREATE_METADATA_TABLE_SQL_IDX1 =
    "CREATE UNIQUE INDEX IF NOT EXISTS METADATA_TABLE_IDX_1 ON METADATA_TABLE(SCHEMA_NAME)";
//------------------------------------------------
// Scope table
//------------------------------------------------
const static wxString CREATE_SCOPE_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS SCOPE_TABLE(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "SCOPE_TYPE INTEGER, " // The scope type: 0 for namespace, 1 for class
    "SCOPE_ID INTEGER NOT NULL DEFAULT -1, "
    "NAME TEXT, "     // no scope, just the class name
    "FULLNAME TEXT, " // full path
    "EXTENDS TEXT DEFAULT '', "
    "IMPLEMENTS TEXT DEFAULT '', "
    "USING_TRAITS TEXT DEFAULT '', "
    "DOC_COMMENT TEXT DEFAULT '', "
    "LINE_NUMBER INTEGER NOT NULL DEFAULT 0, "
    "FILE_NAME TEXT DEFAULT '')";

const static wxString CREATE_SCOPE_TABLE_SQL_IDX1 =
    "CREATE INDEX IF NOT EXISTS SCOPE_TABLE_IDX_1 ON SCOPE_TABLE(SCOPE_ID)";
const static wxString CREATE_SCOPE_TABLE_SQL_IDX2 =
    "CREATE INDEX IF NOT EXISTS SCOPE_TABLE_IDX_2 ON SCOPE_TABLE(FILE_NAME)";
const static wxString CREATE_SCOPE_TABLE_SQL_IDX3 =
    "CREATE UNIQUE INDEX IF NOT EXISTS SCOPE_TABLE_IDX_3 ON SCOPE_TABLE(NAME)";
const static wxString CREATE_SCOPE_TABLE_SQL_IDX4 =
    "CREATE INDEX IF NOT EXISTS SCOPE_TABLE_IDX_4 ON SCOPE_TABLE(SCOPE_TYPE)";
const static wxString CREATE_SCOPE_TABLE_SQL_IDX5 =
    "CREATE INDEX IF NOT EXISTS SCOPE_TABLE_IDX_5 ON SCOPE_TABLE(FULLNAME)";

//------------------------------------------------
// Function table
//------------------------------------------------
const static wxString CREATE_FUNCTION_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS FUNCTION_TABLE(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "SCOPE_ID INTEGER NOT NULL DEFAULT -1, "
    "NAME TEXT, "         // no scope, just the function name
    "FULLNAME TEXT, "     // Fullname with scope
    "SCOPE TEXT, "        // Usually, this means the namespace\class
    "SIGNATURE TEXT, "    // Formatted signature
    "RETURN_VALUE TEXT, " // Fullname (including namespace)
    "FLAGS INTEGER DEFAULT 0, "
    "DOC_COMMENT TEXT, "
    "LINE_NUMBER INTEGER NOT NULL DEFAULT 0, "
    "FILE_NAME TEXT )";

const static wxString CREATE_FUNCTION_TABLE_SQL_IDX1 =
    "CREATE INDEX IF NOT EXISTS FUNCTION_TABLE_IDX_1 ON FUNCTION_TABLE(SCOPE_ID)";
const static wxString CREATE_FUNCTION_TABLE_SQL_IDX2 =
    "CREATE INDEX IF NOT EXISTS FUNCTION_TABLE_IDX_3 ON FUNCTION_TABLE(FILE_NAME)";
const static wxString CREATE_FUNCTION_TABLE_SQL_IDX3 =
    "CREATE UNIQUE INDEX IF NOT EXISTS FUNCTION_TABLE_IDX_4 ON FUNCTION_TABLE(FULLNAME)";

//------------------------------------------------
// Variables table
//------------------------------------------------
const static wxString CREATE_VARIABLES_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS VARIABLES_TABLE(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "SCOPE_ID INTEGER NOT NULL DEFAULT -1, " // for global variable or class member this will be the scope_id parent id
    "FUNCTION_ID INTEGER NOT NULL DEFAULT -1, " // for function argument
    "NAME TEXT, "                               // no scope, just the function name
    "FULLNAME TEXT, "                           // Fullname with scope
    "SCOPE TEXT, "                              // Usually, this means the namespace\class
    "TYPEHINT TEXT, "                           // the Variable type hint
    "FLAGS INTEGER DEFAULT 0, "
    "DOC_COMMENT TEXT, "
    "LINE_NUMBER INTEGER NOT NULL DEFAULT 0, "
    "FILE_NAME TEXT )";

const static wxString CREATE_VARIABLES_TABLE_SQL_IDX1 =
    "CREATE INDEX IF NOT EXISTS VARIABLES_TABLE_IDX_1 ON VARIABLES_TABLE(SCOPE_ID)";
const static wxString CREATE_VARIABLES_TABLE_SQL_IDX2 =
    "CREATE UNIQUE INDEX IF NOT EXISTS VARIABLES_TABLE_IDX_2 ON VARIABLES_TABLE(SCOPE, NAME, FUNCTION_ID, SCOPE_ID)";
const static wxString CREATE_VARIABLES_TABLE_SQL_IDX3 =
    "CREATE INDEX IF NOT EXISTS VARIABLES_TABLE_IDX_3 ON VARIABLES_TABLE(FILE_NAME)";
const static wxString CREATE_VARIABLES_TABLE_SQL_IDX4 =
    "CREATE INDEX IF NOT EXISTS VARIABLES_TABLE_IDX_4 ON VARIABLES_TABLE(FUNCTION_ID)";

//------------------------------------------------
// Files table
//------------------------------------------------
const static wxString CREATE_FILES_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS FILES_TABLE(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "FILE_NAME TEXT, "                        // for global variable or class member this will be the scope_id parent id
    "LAST_UPDATED INTEGER NOT NULL DEFAULT 0" // for function argument
    ")";
const static wxString CREATE_FILES_TABLE_SQL_IDX1 =
    "CREATE UNIQUE INDEX IF NOT EXISTS FILES_TABLE_IDX_1 ON FILES_TABLE(FILE_NAME)";

PHPLookupTable::PHPLookupTable()
    : m_sizeLimit(250)
{
}

PHPLookupTable::~PHPLookupTable() {}

PHPEntityBase::Ptr_t PHPLookupTable::FindMemberOf(wxLongLong parentDbId, const wxString& exactName)
{
    // find the entity
    PHPEntityBase::Ptr_t scope = DoFindScope(parentDbId);
    if(scope && scope->Cast<PHPEntityClass>()) {
        std::vector<wxLongLong> parents;
        std::set<wxLongLong> parentsVisited;

        DoGetInheritanceParentIDs(scope, parents, parentsVisited);

        // Parents should now contain an ordered list of all the inheritance
        for(size_t i = 0; i < parents.size(); ++i) {
            PHPEntityBase::Ptr_t match = DoFindMemberOf(parents.at(i), exactName);
            if(match) {
                return match;
            }
        }
    } else {
        // namespace
        return DoFindMemberOf(parentDbId, exactName, true);
    }
    return PHPEntityBase::Ptr_t(NULL);
}

PHPEntityBase::Ptr_t PHPLookupTable::FindScope(const wxString& fullname)
{
    wxString scopeName = fullname;
    scopeName.Trim().Trim(false);
    if(scopeName.EndsWith("\\") && scopeName.length() > 1) {
        scopeName.RemoveLast();
    }
    return DoFindScope(scopeName);
}

void PHPLookupTable::Open(const wxString& workspacePath)
{
    wxFileName fnDBFile(workspacePath, "phpsymbols.db");

    // ensure that the database directory exists
    fnDBFile.AppendDir(".codelite");
    fnDBFile.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    try {
        wxFileName::Mkdir(fnDBFile.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        m_db.Open(fnDBFile.GetFullPath());
        CreateSchema();

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::Open: %s", e.GetMessage());
    }
}

void PHPLookupTable::CreateSchema()
{
    wxString schemaVersion;
    try {
        m_db.ExecuteUpdate("pragma synchronous = off");
        wxSQLite3Statement st =
            m_db.PrepareStatement("select SCHEMA_VERSION from METADATA_TABLE where SCHEMA_NAME=:SCHEMA_NAME");
        st.Bind(st.GetParamIndex(":SCHEMA_NAME"), "CODELITEPHP");
        wxSQLite3ResultSet res = st.ExecuteQuery();
        if(res.NextRow()) {
            schemaVersion = res.GetString("SCHEMA_VERSION");
        }
    } catch(wxSQLite3Exception& e) {
        wxUnusedVar(e);
    }

    if(schemaVersion != PHP_SCHEMA_VERSION) {
        // Drop the tables and recreate the schema from scratch
        m_db.ExecuteUpdate("drop table if exists SCHEMA_VERSION");
        m_db.ExecuteUpdate("drop table if exists SCOPE_TABLE");
        m_db.ExecuteUpdate("drop table if exists FUNCTION_TABLE");
        m_db.ExecuteUpdate("drop table if exists VARIABLES_TABL");
        m_db.ExecuteUpdate("drop table if exists FILES_TABLE");
    }

    try {

        // Metadata
        m_db.ExecuteUpdate(CREATE_METADATA_TABLE_SQL);
        m_db.ExecuteUpdate(CREATE_METADATA_TABLE_SQL_IDX1);

        // class / namespace table "scope"
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL);
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL_IDX1);
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL_IDX2);
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL_IDX3);
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL_IDX4);
        m_db.ExecuteUpdate(CREATE_SCOPE_TABLE_SQL_IDX5);

        // function table
        m_db.ExecuteUpdate(CREATE_FUNCTION_TABLE_SQL);
        m_db.ExecuteUpdate(CREATE_FUNCTION_TABLE_SQL_IDX1);
        m_db.ExecuteUpdate(CREATE_FUNCTION_TABLE_SQL_IDX2);
        m_db.ExecuteUpdate(CREATE_FUNCTION_TABLE_SQL_IDX3);

        // variables (function args, globals class members and consts)
        m_db.ExecuteUpdate(CREATE_VARIABLES_TABLE_SQL);
        m_db.ExecuteUpdate(CREATE_VARIABLES_TABLE_SQL_IDX1);
        m_db.ExecuteUpdate(CREATE_VARIABLES_TABLE_SQL_IDX2);
        m_db.ExecuteUpdate(CREATE_VARIABLES_TABLE_SQL_IDX3);
        m_db.ExecuteUpdate(CREATE_VARIABLES_TABLE_SQL_IDX4);

        // Files
        m_db.ExecuteUpdate(CREATE_FILES_TABLE_SQL);
        m_db.ExecuteUpdate(CREATE_FILES_TABLE_SQL_IDX1);

        // Update the schema version
        wxSQLite3Statement st =
            m_db.PrepareStatement("replace into METADATA_TABLE (ID, SCHEMA_NAME, SCHEMA_VERSION) VALUES (NULL, "
                                  ":SCHEMA_NAME, :SCHEMA_VERSION)");
        st.Bind(st.GetParamIndex(":SCHEMA_NAME"), "CODELITEPHP");
        st.Bind(st.GetParamIndex(":SCHEMA_VERSION"), PHP_SCHEMA_VERSION);
        st.ExecuteUpdate();

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::CreateSchema: %s", e.GetMessage());
    }
}

void PHPLookupTable::UpdateSourceFile(PHPSourceFile& source, bool autoCommit)
{
    try {
        if(autoCommit) m_db.Begin();

        // Delete all entries for this file
        DeleteFileEntries(source.GetFilename(), false);

        // Store new entries
        PHPEntityBase::Ptr_t topNamespace = source.Namespace();
        if(topNamespace) {
            topNamespace->StoreRecursive(m_db);
            UpdateFileLastParsedTimestamp(source.GetFilename());
        }
        if(autoCommit) m_db.Commit();

    } catch(wxSQLite3Exception& e) {
        if(autoCommit) m_db.Rollback();
        CL_WARNING("PHPLookupTable::SaveSourceFile: %s", e.GetMessage());
    }
}

PHPEntityBase::Ptr_t
PHPLookupTable::DoFindMemberOf(wxLongLong parentDbId, const wxString& exactName, bool parentIsNamespace)
{
    // Find members of of parentDbID
    try {
        PHPEntityBase::List_t matches;
        {
            wxString sql;
            sql << "SELECT * from FUNCTION_TABLE WHERE SCOPE_ID=" << parentDbId << " AND NAME='" << exactName << "'";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityFunction());
                match->FromResultSet(res);
                matches.push_back(match);
            }
        }

        if(matches.empty() && parentIsNamespace) {
            // search the scope table as well
            wxString sql;
            sql << "SELECT * from SCOPE_TABLE WHERE SCOPE_ID=" << parentDbId << " AND NAME='" << exactName << "'";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                ePhpScopeType st = kPhpScopeTypeAny;
                st =
                    res.GetInt("SCOPE_TYPE", 1) == kPhpScopeTypeNamespace ? kPhpScopeTypeNamespace : kPhpScopeTypeClass;

                PHPEntityBase::Ptr_t match = NewEntity("SCOPE_TABLE", st);
                if(match) {
                    match->FromResultSet(res);
                    matches.push_back(match);
                }
            }
        }

        if(matches.empty()) {
            // Could not find a match in the function table, check the variable table
            wxString sql;
            wxString nameWDollar, namwWODollar;
            nameWDollar = exactName;
            if(exactName.StartsWith("$")) {
                namwWODollar = exactName.Mid(1);
            } else {
                namwWODollar = exactName;
                nameWDollar.Prepend("$");
            }

            sql << "SELECT * from VARIABLES_TABLE WHERE SCOPE_ID=" << parentDbId << " AND NAME IN ('" << nameWDollar
                << "', '" << namwWODollar << "')";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityVariable());
                match->FromResultSet(res);
                matches.push_back(match);
            }
            if(matches.empty() || matches.size() > 1) {
                return PHPEntityBase::Ptr_t(NULL);
            } else {
                return (*matches.begin());
            }
        } else if(matches.size() > 1) {
            // we found more than 1 match in the function table
            // return NULL
            return PHPEntityBase::Ptr_t(NULL);
        } else {
            // exactly one match was found in the function table
            // return it
            return (*matches.begin());
        }

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::DoFindMemberOf: %s", e.GetMessage());
    }
    return PHPEntityBase::Ptr_t(NULL);
}

void PHPLookupTable::DoGetInheritanceParentIDs(PHPEntityBase::Ptr_t cls,
                                               std::vector<wxLongLong>& parents,
                                               std::set<wxLongLong>& parentsVisited)
{
    parents.push_back(cls->GetDbId());
    parentsVisited.insert(cls->GetDbId());
    wxArrayString parentsArr = cls->Cast<PHPEntityClass>()->GetInheritanceArray();
    for(size_t i = 0; i < parentsArr.GetCount(); ++i) {
        PHPEntityBase::Ptr_t parent = FindClass(parentsArr.Item(i));
        if(parent && !parentsVisited.count(parent->GetDbId())) {
            DoGetInheritanceParentIDs(parent, parents, parentsVisited);
        }
    }
}

PHPEntityBase::Ptr_t PHPLookupTable::DoFindScope(const wxString& fullname, ePhpScopeType scopeType)
{
    // locate the scope
    try {
        wxString sql;

        // limit by 2 for performance reason
        // we will return NULL incase the number of matches is greater than 1...
        sql << "SELECT * from SCOPE_TABLE WHERE FULLNAME='" << fullname << "'";
        if(scopeType != kPhpScopeTypeAny) {
            sql << " AND SCOPE_TYPE = " << static_cast<int>(scopeType);
        }
        sql << " LIMIT 2 ";

        wxSQLite3Statement st = m_db.PrepareStatement(sql);
        wxSQLite3ResultSet res = st.ExecuteQuery();
        PHPEntityBase::Ptr_t match(NULL);

        while(res.NextRow()) {
            if(match) {
                // only one match
                return PHPEntityBase::Ptr_t(NULL);
            }

            int scopeType = res.GetInt("SCOPE_TYPE", 1);
            if(scopeType == 0) {
                // namespace
                match.Reset(new PHPEntityNamespace());
            } else {
                // class
                match.Reset(new PHPEntityClass());
            }
            match->FromResultSet(res);
        }
        return match;

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::FindScope: %s", e.GetMessage());
    }
    return PHPEntityBase::Ptr_t(NULL);
}

PHPEntityBase::Ptr_t PHPLookupTable::FindClass(const wxString& fullname)
{
    return DoFindScope(fullname, kPhpScopeTypeClass);
}

PHPEntityBase::Ptr_t PHPLookupTable::DoFindScope(wxLongLong id, ePhpScopeType scopeType)
{
    // locate the scope
    try {
        wxString sql;

        // limit by 2 for performance reason
        // we will return NULL incase the number of matches is greater than 1...
        sql << "SELECT * from SCOPE_TABLE WHERE ID=" << id;
        if(scopeType != kPhpScopeTypeAny) {
            sql << " AND SCOPE_TYPE = " << static_cast<int>(scopeType);
        }
        sql << " LIMIT 1";

        wxSQLite3Statement st = m_db.PrepareStatement(sql);
        wxSQLite3ResultSet res = st.ExecuteQuery();

        if(res.NextRow()) {
            PHPEntityBase::Ptr_t match(NULL);
            int scopeType = res.GetInt("SCOPE_TYPE", 1);
            if(scopeType == kPhpScopeTypeNamespace) {
                // namespace
                match.Reset(new PHPEntityNamespace());
            } else {
                // class
                match.Reset(new PHPEntityClass());
            }
            match->FromResultSet(res);
            return match;
        }
    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::DoFindScope: %s", e.GetMessage());
    }
    return PHPEntityBase::Ptr_t(NULL);
}

PHPEntityBase::Ptr_t PHPLookupTable::FindClass(wxLongLong id) { return DoFindScope(id, kPhpScopeTypeClass); }

PHPEntityBase::List_t PHPLookupTable::FindChildren(wxLongLong parentId, size_t flags, const wxString& nameHint)
{
    PHPEntityBase::List_t matches;
    PHPEntityBase::Ptr_t scope = DoFindScope(parentId);
    if(scope && scope->Is(kEntityTypeClass)) {
        std::vector<wxLongLong> parents;
        std::set<wxLongLong> parentsVisited;

        DoGetInheritanceParentIDs(scope, parents, parentsVisited);
        for(size_t i = 0; i < parents.size(); ++i) {
            DoFindChildren(matches, parents.at(i), flags, nameHint);
        }
    } else if(scope && scope->Is(kEntityTypeNamespace)) {
        DoFindChildren(matches, parentId, flags | kLookupFlags_NameHintIsScope, nameHint);
    }
    return matches;
}

PHPEntityBase::List_t PHPLookupTable::LoadFunctionArguments(wxLongLong parentId)
{
    PHPEntityBase::List_t matches;

    try {
        {
            // load functions
            wxString sql;
            sql << "SELECT * from VARIABLES_TABLE WHERE FUNCTION_ID=" << parentId << " ORDER BY ID ASC";

            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityVariable());
                match->FromResultSet(res);
                matches.push_back(match);
            }
        }
    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::LoadFunctionArguments: %s", e.GetMessage());
    }
    return matches;
}

wxString PHPLookupTable::EscapeWildCards(const wxString& str)
{
    wxString s(str);
    s.Replace(wxT("_"), wxT("^_"));
    return s;
}

void PHPLookupTable::DoAddLimit(wxString& sql) { sql << " LIMIT " << m_sizeLimit; }

void PHPLookupTable::UpdateSourceFiles(const wxArrayString& files, eUpdateMode updateMode, bool parseFuncBodies)
{
    try {

        {
            clParseEvent event(wxPHP_PARSE_STARTED);
            event.SetTotalFiles(files.GetCount());
            event.SetCurfileIndex(0);
            EventNotifier::Get()->AddPendingEvent(event);
        }

        m_db.Begin();
        for(size_t i = 0; i < files.GetCount(); ++i) {
            {
                clParseEvent event(wxPHP_PARSE_PROGRESS);
                event.SetTotalFiles(files.GetCount());
                event.SetCurfileIndex(i);
                event.SetFileName(files.Item(i));
                EventNotifier::Get()->AddPendingEvent(event);
            }

            wxFileName fnFile(files.Item(i));
            bool reParseNeeded(true);
            if(updateMode == kUpdateMode_Fast) {
                // Check to see if we need to re-parse this file
                // and store it to the database

                if(!fnFile.Exists()) {
                    reParseNeeded = false;
                } else {
                    time_t lastModifiedOnDisk = fnFile.GetModificationTime().GetTicks();
                    wxLongLong lastModifiedInDB = GetFileLastParsedTimestamp(fnFile);
                    if(lastModifiedOnDisk <= lastModifiedInDB.ToLong()) {
                        reParseNeeded = false;
                    }
                }
            }

            // Ensure that the file exists
            if(!fnFile.Exists()) {
                reParseNeeded = false;
            }

            // Parse only valid PHP files
            if((fnFile.GetExt() != "php") && (fnFile.GetExt() != "inc") && (fnFile.GetExt() != "phtml"))
                reParseNeeded = false;

            if(reParseNeeded) {
                wxFileName fnSourceFile(files.Item(i));
                PHPSourceFile sourceFile(fnSourceFile);
                sourceFile.SetFilename(fnSourceFile);
                sourceFile.SetParseFunctionBody(parseFuncBodies);
                sourceFile.Parse();
                UpdateSourceFile(sourceFile, false);
            }
        }
        m_db.Commit();

        {
            clParseEvent event(wxPHP_PARSE_ENDED);
            event.SetTotalFiles(files.GetCount());
            event.SetCurfileIndex(files.GetCount());
            EventNotifier::Get()->AddPendingEvent(event);
        }

    } catch(wxSQLite3Exception& e) {
        m_db.Rollback();
        CL_WARNING("PHPLookupTable::UpdateSourceFiles: %s", e.GetMessage());
    }
}

void PHPLookupTable::DoAddNameFilter(wxString& sql, const wxString& nameHint, size_t flags)
{
    wxString name = nameHint;
    name.Trim().Trim(false);

    if(name.IsEmpty()) {
        sql.Trim();
        if(sql.EndsWith("AND") || sql.EndsWith("and")) {
            sql.RemoveLast(3);
        }
        sql << " ";
        return;
    }

    if(flags & kLookupFlags_ExactMatch && !name.IsEmpty()) {
        sql << " NAME = '" << name << "'";

    } else if(flags & kLookupFlags_Contains && !name.IsEmpty()) {
        sql << " NAME LIKE '%%" << EscapeWildCards(name) << "%%' ESCAPE '^'";

    } else if(flags & kLookupFlags_StartsWith && !name.IsEmpty()) {
        sql << " NAME LIKE '" << EscapeWildCards(name) << "%%' ESCAPE '^'";
    }
}

void PHPLookupTable::LoadAllByFilter(PHPEntityBase::List_t& matches, const wxString& nameHint, eLookupFlags flags)
{
    try {
        LoadFromTableByNameHint(matches, "SCOPE_TABLE", nameHint, flags);
        LoadFromTableByNameHint(matches, "FUNCTION_TABLE", nameHint, flags);
    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::LoadAllByFilter: %s", e.GetMessage());
    }
}

PHPEntityBase::Ptr_t PHPLookupTable::NewEntity(const wxString& tableName, ePhpScopeType scopeType)
{
    if(tableName == "FUNCTION_TABLE") {
        return PHPEntityBase::Ptr_t(new PHPEntityFunction());
    } else if(tableName == "VARIABLES_TABLE") {
        return PHPEntityBase::Ptr_t(new PHPEntityVariable());
    } else if(tableName == "SCOPE_TABLE" && scopeType == kPhpScopeTypeNamespace) {
        return PHPEntityBase::Ptr_t(new PHPEntityNamespace());
    } else if(tableName == "SCOPE_TABLE" && scopeType == kPhpScopeTypeClass) {
        return PHPEntityBase::Ptr_t(new PHPEntityClass());
    } else {
        return PHPEntityBase::Ptr_t(NULL);
    }
}

void PHPLookupTable::LoadFromTableByNameHint(PHPEntityBase::List_t& matches,
                                             const wxString& tableName,
                                             const wxString& nameHint,
                                             eLookupFlags flags)
{
    wxString trimmedNameHint(nameHint);
    trimmedNameHint.Trim().Trim(false);
    if(trimmedNameHint.IsEmpty()) return;

    wxString sql;
    sql << "SELECT * from " << tableName << " WHERE ";
    DoAddNameFilter(sql, trimmedNameHint, flags);
    DoAddLimit(sql);

    wxSQLite3Statement st = m_db.PrepareStatement(sql);
    wxSQLite3ResultSet res = st.ExecuteQuery();

    while(res.NextRow()) {
        ePhpScopeType st = kPhpScopeTypeAny;
        if(tableName == "SCOPE_TABLE") {
            st = res.GetInt("SCOPE_TYPE", 1) == kPhpScopeTypeNamespace ? kPhpScopeTypeNamespace : kPhpScopeTypeClass;
        }

        PHPEntityBase::Ptr_t match = NewEntity(tableName, st);
        if(match) {
            match->FromResultSet(res);
            matches.push_back(match);
        }
    }
}

void PHPLookupTable::DeleteFileEntries(const wxFileName& filename, bool autoCommit)
{
    try {
        if(autoCommit) m_db.Begin();
        {
            wxString sql;
            sql << "delete from SCOPE_TABLE where FILE_NAME=:FILE_NAME";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from FUNCTION_TABLE where FILE_NAME=:FILE_NAME";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from VARIABLES_TABLE where FILE_NAME=:FILE_NAME";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from FILES_TABLE where FILE_NAME=:FILE_NAME";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
            st.ExecuteUpdate();
        }

        if(autoCommit) m_db.Commit();
    } catch(wxSQLite3Exception& e) {
        if(autoCommit) m_db.Rollback();
        CL_WARNING("PHPLookupTable::DeleteFileEntries: %s", e.GetMessage());
    }
}

void PHPLookupTable::Close()
{
    try {
        if(m_db.IsOpen()) {
            m_db.Close();
        }
    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::Close: %s", e.GetMessage());
    }
}

bool PHPLookupTable::IsOpened() const { return m_db.IsOpen(); }

void PHPLookupTable::DoFindChildren(PHPEntityBase::List_t& matches,
                                    wxLongLong parentId,
                                    size_t flags,
                                    const wxString& nameHint)
{
    // Find members of of parentDbID
    try {
        {
            // Load classes
            wxString sql;
            sql << "SELECT * from SCOPE_TABLE WHERE SCOPE_ID=" << parentId << " AND SCOPE_TYPE = 1 AND ";
            DoAddNameFilter(sql, nameHint, flags);
            DoAddLimit(sql);

            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityClass());
                match->FromResultSet(res);
                matches.push_back(match);
            }
        }

        {
            // load functions
            wxString sql;
            sql << "SELECT * from FUNCTION_TABLE WHERE SCOPE_ID=" << parentId << " AND ";
            DoAddNameFilter(sql, nameHint, flags);
            DoAddLimit(sql);

            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityFunction());
                match->FromResultSet(res);
                bool isStatic = match->Cast<PHPEntityFunction>()->HasFlag(PHPEntityFunction::kStatic);
                if(isStatic & CollectingStatics(flags)) {
                    matches.push_back(match);

                } else if(!isStatic && !CollectingStatics(flags)) {
                    matches.push_back(match);
                }
            }
        }

        {
            // Add members from the variables table
            wxString sql;
            sql << "SELECT * from VARIABLES_TABLE WHERE SCOPE_ID=" << parentId << " AND ";
            DoAddNameFilter(sql, nameHint, flags);
            DoAddLimit(sql);

            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            wxSQLite3ResultSet res = st.ExecuteQuery();

            while(res.NextRow()) {
                PHPEntityBase::Ptr_t match(new PHPEntityVariable());
                match->FromResultSet(res);

                bool isConst = match->Cast<PHPEntityVariable>()->IsConst();
                bool isStatic = match->Cast<PHPEntityVariable>()->IsStatic();
                if((isStatic || isConst) && CollectingStatics(flags)) {
                    matches.push_back(match);

                } else if(!isStatic && !isConst && !CollectingStatics(flags)) {
                    matches.push_back(match);
                }
            }
        }

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::FindChildren: %s", e.GetMessage());
    }
}

wxLongLong PHPLookupTable::GetFileLastParsedTimestamp(const wxFileName& filename)
{
    try {
        wxSQLite3Statement st =
            m_db.PrepareStatement("SELECT LAST_UPDATED FROM FILES_TABLE WHERE FILE_NAME=:FILE_NAME");
        st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
        wxSQLite3ResultSet res = st.ExecuteQuery();
        if(res.NextRow()) {
            return res.GetInt64("LAST_UPDATED");
        }
    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::FindChildren: %s", e.GetMessage());
    }
    return 0;
}

void PHPLookupTable::UpdateFileLastParsedTimestamp(const wxFileName& filename)
{
    try {
        wxSQLite3Statement st = m_db.PrepareStatement(
            "REPLACE INTO FILES_TABLE (ID, FILE_NAME, LAST_UPDATED) VALUES (NULL, :FILE_NAME, :LAST_UPDATED)");
        st.Bind(st.GetParamIndex(":FILE_NAME"), filename.GetFullPath());
        st.Bind(st.GetParamIndex(":LAST_UPDATED"), (wxLongLong)time(NULL));
        st.ExecuteUpdate();

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::UpdateFileLastParsedTimestamp: %s", e.GetMessage());
    }
}

void PHPLookupTable::ClearAll()
{
    try {
        m_db.Begin();
        {
            wxString sql;
            sql << "delete from SCOPE_TABLE";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from FUNCTION_TABLE";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from VARIABLES_TABLE";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.ExecuteUpdate();
        }

        {
            wxString sql;
            sql << "delete from FILES_TABLE";
            wxSQLite3Statement st = m_db.PrepareStatement(sql);
            st.ExecuteUpdate();
        }

        m_db.Commit();
    } catch(wxSQLite3Exception& e) {
        m_db.Rollback();
        CL_WARNING("PHPLookupTable::ClearAll: %s", e.GetMessage());
    }
}

PHPEntityBase::Ptr_t PHPLookupTable::FindFunction(const wxString& fullname)
{
    // locate the scope
    try {
        wxString sql;

        // limit by 2 for performance reason
        // we will return NULL incase the number of matches is greater than 1...
        sql << "SELECT * from FUNCTION_TABLE WHERE FULLNAME='" << fullname << "'";
        sql << " LIMIT 2";

        wxSQLite3Statement st = m_db.PrepareStatement(sql);
        wxSQLite3ResultSet res = st.ExecuteQuery();
        PHPEntityBase::Ptr_t match(NULL);

        while(res.NextRow()) {
            if(match) {
                // only one match
                return PHPEntityBase::Ptr_t(NULL);
            }

            match.Reset(new PHPEntityFunction());
            match->FromResultSet(res);
        }
        return match;

    } catch(wxSQLite3Exception& e) {
        CL_WARNING("PHPLookupTable::FindFunction: %s", e.GetMessage());
    }
    return PHPEntityBase::Ptr_t(NULL);
}

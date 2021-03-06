//
// This library is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <InterceptPP/InterceptPP.h>
#include <InterceptPP/ConsoleLogger.h>
#include <iostream>

using namespace std;
using namespace InterceptPP;

typedef enum {
    SIGNATURE_UI_STATUS_LABEL_SET = 0,
    SIGNATURE_WIZ_STATUS_LABEL_SET
};

static SignatureSpec signatureSpecs[] = {
    // SIGNATURE_UI_STATUS_LABEL_SET
    {
        "wcesmgr.exe",
		0,
        "56"                    // push    esi
        "FF 74 24 08"           // push    [esp+text]
        "8B F1"                 // mov     esi, ecx
        "E8 ?? ?? ?? 00"        // call    ?SetWindowTextA@CWnd@@QAEXPBD@Z ; CWnd::SetWindowTextA(char const *)
        "68 C8 00 00 00"        // push    200             ; iMaxLength
        "FF 74 24 0C"           // push    dword ptr [esp+4+text] ; lpString2
        "8D 46 54"              // lea     eax, [esi+54h]
        "50"                    // push    eax             ; lpString1
        "FF 15 ?? ?? ?? 00"     // call    ds:lstrcpynA
        "6A 01"                 // push    1               ; bErase
        "6A 00"                 // push    0               ; lpRect
        "FF 76 20"              // push    dword ptr [esi+20h] ; hWnd
        "FF 15 ?? ?? ?? 00"     // call    ds:InvalidateRect
        "FF 76 20"              // push    dword ptr [esi+20h] ; hWnd
        "FF 15 ?? ?? ?? 00"     // call    ds:UpdateWindow
        "5E"                    // pop     esi
        "C2 04 00",             // retn    4
    },

    // SIGNATURE_WIZ_STATUS_LABEL_SET
    {
        "wcesmgr.exe",
		0,
        "83 C1 74"              // add     ecx, 74h
        "51"                    // push    ecx
        "68 27 02 00 00"        // push    227h
        "FF 74 24 0C"           // push    [esp+8+pDX]
        "E8 ?? ?? ?? 00"        // call    ?DDX_Text@@YGXPAVCDataExchange@@HAAV?$CStringT@DV?$StrTraitMFC_DLL@DV?$ChTraitsCRT@D@ATL@@@@@ATL@@@Z ; DDX_Text(CDataExchange *,int,ATL::CStringT<char,StrTraitMFC_DLL<char,ATL::ChTraitsCRT<char>>> &)
        "C2 04 00",             // retn    4
    },
};

#define DEBUG 0

int main(int argc, char *argv[])
{
    InterceptPP::Initialize();
    InterceptPP::SetLogger(new Logging::ConsoleLogger());

    HookManager *mgr = HookManager::Instance();
#if !DEBUG
    try
    {
#endif
        mgr->LoadDefinitions(L"D:\\Projects\\oSpy\\trunk\\oSpy\\bin\\Debug\\config.xml");

        cout << TypeBuilder::Instance()->GetTypeCount() << " types loaded" << endl;
        cout << mgr->GetFunctionSpecCount() << " FunctionSpec objects loaded" << endl;
        cout << mgr->GetVTableSpecCount() << " VTableSpec objects loaded" << endl;
        cout << mgr->GetSignatureCount() << " Signature objects loaded" << endl;
        cout << mgr->GetDllModuleCount() << " DllModule objects loaded" << endl;
        cout << mgr->GetDllFunctionCount() << " DllFunction objects loaded" << endl;
        cout << mgr->GetVTableCount() << " VTable objects loaded" << endl;
#if !DEBUG
    }
    catch (ParserError &e)
    {
        GetLogger()->LogError("LoadDefinitions failed: %s", e.what());
    }
    catch (...)
    {
        GetLogger()->LogError("LoadDefinitions failed: unknown error");
    }
#endif

    //LoadLibrary("C:\\Projects\\oSpy\\trunk\\oSpy\\bin\\Debug\\oSpyAgent.dll");
    //Signature s(&signatureSpecs[SIGNATURE_UI_STATUS_LABEL_SET]);

    cout << "all good dude" << endl;
    OString str;
    cin >> str;

	return 0;
}
